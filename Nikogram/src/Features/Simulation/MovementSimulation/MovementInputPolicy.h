#pragma once
#include <algorithm>
#include <cmath>

// Inputs inferred from observed motion remain hypotheses. These helpers never
// integrate position or replace the engine's collision/acceleration result.
namespace MovementInputPolicy
{
    struct Vector { float x=0, y=0; };
    struct Physics { float tick=0, maxSpeed=0, friction=0, stopSpeed=0, acceleration=0, surface=0; bool lostFooting=false; };
    // end: native end-of-interval velocity implied by the fitted input (only set
    // by the interval fit, where the received velocity is an interval mean).
    struct Input { bool valid=false; Vector wish{}; Vector end{}; bool hasEnd=false; };
    // Remote player origins arrive at 1/8 unit (SPROP_COORD_MP_LOWPRECISION,
    // PLAYER_ORIGIN_COMPRESSION). Received velocities are position chords, so a
    // one-tick chord carries up to +-quantum/dt of noise on each axis.
    inline constexpr float OriginQuantum=.125f;
    inline bool Finite(Vector v) { return std::isfinite(v.x) && std::isfinite(v.y); }
    inline float Length(Vector v) { return std::hypot(v.x,v.y); }
    inline bool Valid(const Physics& p)
    {
        return std::isfinite(p.tick) && p.tick>0 && std::isfinite(p.maxSpeed) && p.maxSpeed>0
            && std::isfinite(p.friction) && p.friction>=0 && std::isfinite(p.stopSpeed) && p.stopSpeed>=0
            && std::isfinite(p.acceleration) && p.acceleration>0 && std::isfinite(p.surface) && p.surface>0;
    }
    inline Vector AfterFriction(Vector velocity,const Physics& p)
    {
        const float speed=Length(velocity);
        if(speed<.1f) return velocity; // CGameMovement::Friction early return.
        const float next=std::max(0.f,speed-std::max(speed,p.stopSpeed)*p.friction*p.surface*p.tick);
        return {velocity.x*(next/speed),velocity.y*(next/speed)};
    }
    inline Input ForVelocity(Vector before,Vector desired,const Physics& p,bool ground)
    {
        if(!Valid(p) || !Finite(before) || !Finite(desired)) return {};
        if(ground) before=AfterFriction(before,p);
        const Vector delta{desired.x-before.x,desired.y-before.y};
        const float change=Length(delta);
        if(!std::isfinite(change)) return {};
        if(change<.0001f) return {true,{}};
        const Vector direction{delta.x/change,delta.y/change};
        const float projection=before.x*direction.x+before.y*direction.y;
        const float coefficient=p.acceleration*p.tick*p.surface;
        const float lowWish=100.f*p.friction*p.surface/p.acceleration;
        const float boostedBase=std::max(Length(before),p.stopSpeed)*p.friction*p.surface;
        if(!std::isfinite(projection) || !std::isfinite(coefficient) || coefficient<=0
            || !std::isfinite(lowWish) || !std::isfinite(boostedBase)) return {};
        // Candidate solutions for each native acceleration branch and the
        // directional add-speed cap. Check the branch actually taken; low wish
        // input can otherwise receive TF's traction boost and overshoot the goal.
        const float candidates[]={p.maxSpeed,change/coefficient,
            change/(p.tick*p.surface)-boostedBase,change+projection};
        float bestSpeed=0,bestChange=0;
        for(float speed:candidates)
        {
            speed=std::clamp(speed,0.f,p.maxSpeed);
            if(!std::isfinite(speed) || speed<=0) continue;
            const float acceleration=ground && !p.lostFooting && speed<lowWish
                ? (boostedBase+speed)*p.tick*p.surface : coefficient*speed;
            const float add=std::max(0.f,(ground?speed:std::min(speed,30.f))-projection);
            const float achieved=std::min(acceleration,add);
            if(std::isfinite(achieved) && achieved<=change+.00001f && achieved>bestChange)
                bestSpeed=speed,bestChange=achieved;
        }
        return {true,{direction.x*bestSpeed,direction.y*bestSpeed}};
    }

    struct GroundResult { Vector velocity{},displacement{}; };
    // Small inverse-model adapter only. Actual forecasts still use native
    // ProcessMovement and its collision result, never this flat-ground result.
    inline GroundResult GroundInterval(Vector velocity,Vector wish,int ticks,const Physics& p)
    {
        GroundResult out{velocity,{}};
        const float speed=Length(wish),capped=std::min(speed,p.maxSpeed);
        const Vector direction=speed>0?Vector{wish.x/speed,wish.y/speed}:Vector{};
        for(int i=0;i<ticks;++i) {
            auto& v=out.velocity;v=AfterFriction(v,p);
            if(capped>0) {
                const float add=std::max(0.f,capped-v.x*direction.x-v.y*direction.y);
                const float low=100.f*p.friction*p.surface/p.acceleration;
                const float acceleration=!p.lostFooting && capped<low
                    ? (std::max(Length(v),p.stopSpeed)*p.friction*p.surface+capped)*p.tick*p.surface
                    : p.acceleration*p.tick*capped*p.surface;
                const float change=std::min(add,acceleration);
                v.x+=direction.x*change;v.y+=direction.y*change;
                // CTFGameMovement::WalkMove clamps the planar speed after Accelerate.
                const float n=Length(v);
                if(n>p.maxSpeed&&n>0){v.x*=p.maxSpeed/n;v.y*=p.maxSpeed/n;}
            }
            out.displacement.x+=v.x*p.tick;out.displacement.y+=v.y*p.tick;
        }
        return out;
    }

    // Simple hypotheses are tried before the exact inverse: with 1/8-unit origins
    // the exact inverse of a steady run turns quantization noise into a wish
    // direction error of tens of degrees. Candidates: no input (friction only),
    // holding full speed along the received or previous direction, and the exact
    // inverse. The best candidate inside the noise bound wins.
    inline Input InferGround(Vector previous,Vector current,float sampleDelta,const Physics& p,float quantum=OriginQuantum)
    {
        if(!Valid(p) || !Finite(previous) || !Finite(current)
            || !std::isfinite(sampleDelta) || std::abs(sampleDelta-p.tick)>.0001f
            || !std::isfinite(quantum) || quantum<0) return {};
        const float noise=2.f*quantum/sampleDelta; // previous and current chord errors combined
        if(Length(previous)>p.maxSpeed+noise || Length(current)>p.maxSpeed+noise) return {};
        const auto clampSpeed=[&](Vector v){const float n=Length(v);if(n>p.maxSpeed&&n>0){v.x*=p.maxSpeed/n;v.y*=p.maxSpeed/n;}return v;};
        previous=clampSpeed(previous);
        const auto error=[&](Vector wish){const auto r=GroundInterval(previous,wish,1,p).velocity;return std::hypot(r.x-current.x,r.y-current.y);};
        Input best{};float bestError=noise;
        const auto consider=[&](Vector wish){const float e=error(wish);if(std::isfinite(e)&&e<=bestError){best={true,wish};bestError=e;}};
        for(const Vector d:{current,previous}) {
            const float n=Length(d);
            if(n>1.f) consider({d.x/n*p.maxSpeed,d.y/n*p.maxSpeed});
        }
        // Released keys vs still running: when the two hypotheses predict clearly
        // different velocities (friction loss vs chord noise) the better fit wins.
        // When they are closer than the noise (slow runners: friction removes only
        // ~6 u/s per tick) one tick cannot tell them apart; keep running unless
        // released keys fit clearly better.
        {const float passive=error({});
            if(std::isfinite(passive)&&passive<=noise){
                if(!best.valid)return {true,{}};
                const auto coast=GroundInterval(previous,{},1,p).velocity,run=GroundInterval(previous,best.wish,1,p).velocity;
                const float separation=std::hypot(coast.x-run.x,coast.y-run.y);
                if(passive+(separation>=.6f*noise?0.f:.5f*noise)<bestError)return {true,{}};
            }}
        if(best.valid) return best;
        const Vector target=clampSpeed(current);
        const auto friction=AfterFriction(previous,p);
        const Vector delta{target.x-friction.x,target.y-friction.y};
        const float change=Length(delta);
        if(!std::isfinite(change)) return {};
        if(change<.0001f) return {true,{}};
        const Vector direction{delta.x/change,delta.y/change};
        const float projection=friction.x*direction.x+friction.y*direction.y;
        const float speed=std::max(change/(p.acceleration*p.tick*p.surface),change+projection);
        // TF WalkMove has a special low-wishspeed traction branch. Do not infer
        // input with the ordinary acceleration inverse when that branch applies.
        const float lowWish=100.f*p.friction*p.surface/p.acceleration;
        if(!std::isfinite(speed) || speed>p.maxSpeed+noise || speed<lowWish) return {};
        Vector wish{direction.x*std::min(speed,p.maxSpeed),direction.y*std::min(speed,p.maxSpeed)};
        // The closed-form inverse ignores WalkMove's speed clamp, which is active
        // whenever a full-speed runner turns. Refine against the native step.
        float currentError=error(wish);
        for(int iteration=0;iteration<8 && currentError>noise*.5f;++iteration) {
            const auto base=GroundInterval(previous,wish,1,p).velocity;
            const auto x=GroundInterval(previous,{wish.x+1.f,wish.y},1,p).velocity;
            const auto y=GroundInterval(previous,{wish.x,wish.y+1.f},1,p).velocity;
            const float a=x.x-base.x,b=y.x-base.x,c=x.y-base.y,d=y.y-base.y,determinant=a*d-b*c;
            if(!std::isfinite(determinant)||std::abs(determinant)<.000001f)break;
            const float ex=target.x-base.x,ey=target.y-base.y;
            const auto next=clampSpeed({wish.x+(d*ex-b*ey)/determinant,wish.y+(a*ey-c*ex)/determinant});
            const float nextError=error(next);
            if(!Finite(next)||!(nextError<currentError))break;
            wish=next;currentError=nextError;
        }
        if(currentError>noise) return {};
        return {true,wish};
    }

    // Fit one constant wish across a received interval of 1-4 ticks. The received
    // velocity is the position chord, i.e. the MEAN velocity over the interval,
    // so the fit matches displacement (mean velocity), never the end velocity.
    // On success the native end-of-interval velocity is returned in Input::end.
    inline Input InferGroundInterval(Vector previous,Vector current,Vector displacement,
        float sampleDelta,const Physics& p,float clockTolerance=.0001f,float quantum=OriginQuantum)
    {
        if(!Valid(p)||!Finite(previous)||!Finite(current)||!Finite(displacement)
            ||!std::isfinite(sampleDelta)||!std::isfinite(clockTolerance)||!std::isfinite(quantum)||quantum<0
            ||sampleDelta<=0||sampleDelta>4.25f*p.tick)return {};
        const int ticks=int(std::round(sampleDelta/p.tick));
        if(ticks<1||ticks>4||std::abs(sampleDelta-ticks*p.tick)>std::clamp(clockTolerance,.0001f,p.tick*.25f))return {};
        const float velocityNoise=2.f*quantum/sampleDelta;
        if(Length(previous)>p.maxSpeed+velocityNoise||Length(current)>p.maxSpeed+velocityNoise)return {};
        const auto clamp=[&](Vector v){const float n=Length(v);if(n>p.maxSpeed&&n>0){v.x*=p.maxSpeed/n;v.y*=p.maxSpeed/n;}return v;};
        previous=clamp(previous);
        // Endpoint rounding (+-quantum per axis) plus the starting chord's noise
        // carried through the interval.
        const float tolerance=quantum*(1.5f+float(ticks))*1.41421356f;
        const auto error=[&](const GroundResult& a){return std::hypot(a.displacement.x-displacement.x,a.displacement.y-displacement.y);};
        const auto finish=[&](Vector wish,const GroundResult& a){Input out{true,wish};out.end=a.velocity;out.hasEnd=true;return out;};
        // Simple hypotheses first (see InferGround).
        {
            Input best{};float bestError=tolerance;
            const auto consider=[&](Vector wish){const auto a=GroundInterval(previous,wish,ticks,p);const float e=error(a);
                if(std::isfinite(e)&&e<=bestError){best=finish(wish,a);bestError=e;}};
            const Vector mean{displacement.x/sampleDelta,displacement.y/sampleDelta};
            for(const Vector d:{mean,previous}) {
                const float n=Length(d);
                if(n>1.f) consider({d.x/n*p.maxSpeed,d.y/n*p.maxSpeed});
            }
            // Released keys only when clearly better (see InferGround).
            const auto still=GroundInterval(previous,{},ticks,p);const float passive=error(still);
            if(std::isfinite(passive)&&passive<=tolerance){
                if(!best.valid)return finish({},still);
                const auto run=GroundInterval(previous,best.wish,ticks,p);
                const float separation=std::hypot(still.displacement.x-run.displacement.x,still.displacement.y-run.displacement.y);
                if(passive+(separation>=.6f*tolerance?0.f:.5f*tolerance)<bestError)return finish({},still);
            }
            if(best.valid) return best;
        }
        const auto meanOf=[&](const GroundResult& a){return Vector{a.displacement.x/sampleDelta,a.displacement.y/sampleDelta};};
        const Vector target{displacement.x/sampleDelta,displacement.y/sampleDelta};
        const auto passive=GroundInterval(previous,{},ticks,p);
        const float coefficient=p.acceleration*p.tick*p.surface*ticks;
        const auto passiveMean=meanOf(passive);
        Vector wish=clamp({(target.x-passiveMean.x)/coefficient*2.f,(target.y-passiveMean.y)/coefficient*2.f});
        auto result=GroundInterval(previous,wish,ticks,p);
        const auto steady=GroundInterval(previous,target,ticks,p);
        if(error(steady)<error(result)){wish=clamp(target);result=GroundInterval(previous,wish,ticks,p);}
        // Bounded two-dimensional inverse on the mean velocity: at most 8
        // iterations of 3 four-tick evaluations.
        for(int iteration=0;iteration<8;++iteration) {
            if(error(result)<=tolerance)return finish(wish,result);
            const auto base=meanOf(result);
            const auto x=meanOf(GroundInterval(previous,{wish.x+1.f,wish.y},ticks,p));
            const auto y=meanOf(GroundInterval(previous,{wish.x,wish.y+1.f},ticks,p));
            const float a=x.x-base.x,b=y.x-base.x;
            const float c=x.y-base.y,d=y.y-base.y;
            const float determinant=a*d-b*c;
            if(!std::isfinite(determinant)||std::abs(determinant)<.000001f)break;
            const float ex=target.x-base.x,ey=target.y-base.y;
            auto next=clamp({wish.x+(d*ex-b*ey)/determinant,wish.y+(a*ey-c*ex)/determinant});
            if(!Finite(next))break;
            const auto trial=GroundInterval(previous,next,ticks,p);
            if(error(trial)>=error(result))break;
            wish=next;result=trial;
        }
        return error(result)<=tolerance?finish(wish,result):Input{};
    }
    // Time (s) for a ground speed component to reach zero once the wish reverses
    // to full speed the other way (native friction + acceleration, flat ground).
    // At full speed this is ~6 ticks for every class; it is not proportional to speed.
    inline float ReversalTime(float speed,const Physics& p)
    {
        if(!Valid(p)||!std::isfinite(speed)||speed<=0)return 0;
        Vector v{std::min(speed,p.maxSpeed),0};
        for(int i=0;i<64;++i){
            const float before=v.x;
            v=GroundInterval(v,{-p.maxSpeed,0},1,p).velocity;
            if(v.x<=0){const float fraction=before>0?before/(before-v.x):0.f;return (float(i)+std::clamp(fraction,0.f,1.f))*p.tick;}
        }
        return 64*p.tick;
    }
    // Distance covered while actively braking (full opposite wish) to a stop on
    // flat ground: how players usually stop at an edge.
    inline float ActiveStopDistance(float speed,const Physics& p)
    {
        if(!Valid(p)||!std::isfinite(speed)||speed<=0)return 0;
        Vector v{std::min(speed,p.maxSpeed),0};float distance=0;
        for(int i=0;i<64;++i){
            const auto next=GroundInterval(v,{-p.maxSpeed,0},1,p).velocity;
            if(next.x<=0){const float fraction=v.x/(v.x-next.x);distance+=v.x*p.tick*.5f*std::clamp(fraction,0.f,1.f);return distance;}
            v=next;distance+=v.x*p.tick;
        }
        return distance;
    }
    inline float StopLookAhead(float speed,const Physics& p)
    {
        if(!Valid(p) || !std::isfinite(speed) || speed<=0) return 0;
        float distance=speed*p.tick+2.f;
        // Exact flat-ground, zero-input friction recurrence, bounded to the existing
        // probe range. No assumed fixed braking acceleration and no entity writes.
        for(int i=0;i<128 && speed>=.1f && distance<96.f;++i)
        {
            speed=AfterFriction({speed,0},p).x;
            distance+=speed*p.tick;
        }
        return std::clamp(distance,2.f,96.f);
    }
}
