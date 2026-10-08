#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// No engine access. Candidate evaluation is pure; only the selected outgoing
// command commits velocity. All candidates share one frozen command snapshot.
namespace SmoothPolicy
{
    using Angle=std::array<float,2>;
    struct Settings
    {
        float responseMs=140.f, speed=180.f, acceleration=1800.f;
        float smoothAmount=10.f,assistAmount=0.f;
        bool responsiveAssist=false;
        bool operator==(const Settings&)const=default;
    };
    inline float Safe(float value,float fallback,float low,float high)
    {return std::isfinite(value)?std::clamp(value,low,high):fallback;}
    inline Settings Normalize(Settings s)
    {
        s.responseMs=Safe(s.responseMs,140.f,20.f,600.f);
        s.speed=Safe(s.speed,180.f,10.f,720.f);
        s.acceleration=Safe(s.acceleration,1800.f,100.f,10000.f);
        s.smoothAmount=Safe(s.smoothAmount,10.f,0.f,10.f);
        s.assistAmount=Safe(s.assistAmount,0.f,0.f,10.f);
        return s;
    }
    inline bool Finite(Angle a){return std::isfinite(a[0])&&std::isfinite(a[1]);}
    inline float Wrap(float angle){return std::isfinite(angle)?std::remainder(angle,360.f):0.f;}
    inline Angle Difference(Angle a,Angle b){return {a[0]-b[0],Wrap(a[1]-b[1])};}
    inline float Length(Angle a){return std::hypot(a[0],a[1]);}
    inline float SearchFOV(float configured,bool lead,bool visible)
    {return configured<=0.f?0.f:visible?configured:lead?180.f:configured;}
    inline bool WithinFOV(Angle origin,Angle destination,float limit)
    {
        if(!Finite(origin)||!Finite(destination)||!std::isfinite(limit)||limit<=0.f)return false;
        constexpr double radians=3.14159265358979323846/180.;
        const double a=origin[0]*radians,b=destination[0]*radians,yaw=Wrap(destination[1]-origin[1])*radians;
        const double dot=std::sin(a)*std::sin(b)+std::cos(a)*std::cos(b)*std::cos(yaw);
        return std::acos(std::clamp(dot,-1.,1.))/radians<std::min(limit,180.f);
    }
    inline bool PlacementBetter(float height,float distance,float bestHeight,float bestDistance)
    {
        if(!std::isfinite(height)||!std::isfinite(distance))return false;
        // Small projection noise is not a reason to switch between adjacent boxes.
        return height<bestHeight-.00001f||(std::abs(height-bestHeight)<=.00001f&&distance<bestDistance);
    }
    inline Angle Limit(Angle a,float maximum)
    {
        const float length=Length(a);
        if(length>maximum&&length>0.f){a[0]*=maximum/length;a[1]*=maximum/length;}
        return a;
    }
    struct Kernel{Settings settings;float dt=0,omega=0,decay=1,reserve=0;};
    inline Kernel Prepare(Settings raw,float dt)
    {
        const auto s=Normalize(raw);const float omega=4.f/(s.responseMs*.001f);
        return {s,dt,omega,std::exp(-omega*dt),s.acceleration*dt};
    }
    inline Angle StepPrepared(Angle current,Angle goal,Angle velocity,const Kernel& kernel,Angle mouse={})
    {
        const float dt=kernel.dt;
        if(!Finite(current)||!Finite(goal)||!Finite(velocity)||!std::isfinite(dt)||dt<=0.f)return current;
        const auto& s=kernel.settings;
        const auto delta=Difference(goal,current);
        const float distance=Length(delta);
        if(distance<.000001f)return current;
        const float mouseLength=s.assistAmount>0.f&&Finite(mouse)?Length(mouse):0.f;
        const float intent=mouseLength>0.f?std::clamp((mouse[0]*delta[0]+mouse[1]*delta[1])/(mouseLength*distance),0.f,1.f):0.f;
        // No mouse means no Assistive correction; pulling away gets no extra
        // magnetism. Both sliders at zero also discard old correction momentum.
        if(s.smoothAmount==0.f&&(s.assistAmount==0.f||intent==0.f))return current;
        const float omega=kernel.omega,decay=kernel.decay;
        Angle desired;
        for(int i=0;i<2;++i)
        {
            // Exact critically damped spring step before angular limits.
            const float error=-delta[i],term=(velocity[i]+omega*error)*dt;
            desired[i]=((error+term)*decay-error)/dt;
        }
        if(s.smoothAmount!=10.f||s.assistAmount!=0.f)
        {
            // Both contributions point at the same goal, then pass through ONE
            // speed/acceleration/braking limit. Neither rewrites the mouse input.
            const float assist=std::min(mouseLength,distance)*intent*(s.assistAmount*.1f)/dt;
            for(int i=0;i<2;++i)desired[i]=desired[i]*(s.smoothAmount*.1f)+delta[i]/distance*assist;
        }
        // Reserve braking distance as the goal approaches. A moving goal may
        // cross the camera; the final crossing guard below favours no overshoot
        // over preserving momentum in that discontinuous situation.
        // Reserve a full command of travel as well as continuous stopping
        // distance; half-command braking can reach a fixed goal too fast.
        const float reserve=kernel.reserve;
        const float braking=std::sqrt(2.f*s.acceleration*distance+reserve*reserve)-reserve;
        desired=Limit(desired,std::min(s.speed,braking));
        Angle change={desired[0]-velocity[0],desired[1]-velocity[1]};
        change=Limit(change,s.acceleration*dt);
        Angle step={(velocity[0]+change[0])*dt,(velocity[1]+change[1])*dt};
        step=Limit(step,s.speed*dt);
        for(int i=0;i<2;++i)
        {
            if(step[i]*delta[i]<=0.f)step[i]=0.f;
            else if(std::abs(step[i])>std::abs(delta[i]))step[i]=delta[i];
        }
        return {std::clamp(current[0]+step[0],-89.f,89.f),Wrap(current[1]+step[1])};
    }
    inline Angle Step(Angle current,Angle goal,Angle velocity,float dt,Settings raw)
    {return StepPrepared(current,goal,velocity,Prepare(raw,dt));}
    inline float Ease(float low,float high,float value)
    {const float t=std::clamp((value-low)/(high-low),0.f,1.f);return t*t*(3.f-2.f*t);}
    // A visible, enabled hitbox only. Visibility and range are the caller's job;
    // this value type never queries engine state or persists candidate probes.
    struct AssistChoice
    {
        Angle goal{};float score=0.f,height=0.f;bool valid=false,head=false;
        void Consider(Angle next,float distanceSquared,bool isHead,bool preferHead,float heightMiss=0.f)
        {
            if(!Finite(next)||!std::isfinite(distanceSquared)||distanceSquared<0.f||!std::isfinite(heightMiss)||heightMiss<0.f)return;
            if(valid&&!(preferHead&&isHead&&!head)
                &&((preferHead&&head&&!isHead)||!PlacementBetter(heightMiss,distanceSquared,height,score)))return;
            goal=next;score=distanceSquared;height=heightMiss;head=isHead;valid=true;
        }
    };
    inline Angle StepGuidedPrepared(Angle current,Angle smoothGoal,Angle assistGoal,Angle velocity,const Kernel& kernel,Angle mouse)
    {
        const auto& s=kernel.settings;const float dt=kernel.dt;
        // Zero Assistive is exactly the previously accepted Smooth algorithm.
        if(!s.responsiveAssist||s.assistAmount==0.f)return StepPrepared(current,smoothGoal,velocity,kernel,mouse);
        if(!Finite(current)||!Finite(smoothGoal)||!Finite(assistGoal)||!Finite(velocity)
            ||!Finite(mouse)||!std::isfinite(dt)||dt<=0.f)return current;
        const Angle smoothDelta=Difference(smoothGoal,current),assistDelta=Difference(assistGoal,current);
        const float smoothDistance=Length(smoothDelta),assistDistance=Length(assistDelta),input=Length(mouse);
        if(std::max(smoothDistance,assistDistance)<.000001f)return current;
        // Angular speed, not raw mouse counts: stable across sensitivity/tickrate.
        // Fine corrections get full help; fast flicks smoothly take full control.
        const float flickYield=1.f-Ease(120.f,540.f,input/dt);
        const auto alignment=[&](Angle delta,float distance){return input>0.f&&distance>.000001f
            ?std::clamp((mouse[0]*delta[0]+mouse[1]*delta[1])/(input*distance),-1.f,1.f):0.f;};
        const float smoothAlignment=alignment(smoothDelta,smoothDistance);
        const float smoothYield=input>0.f?(1.f-Ease(0.f,.35f,-smoothAlignment))*flickYield:1.f;
        const float assistAlignment=std::max(0.f,alignment(assistDelta,assistDistance));
        const float proximity=1.f-Ease(3.f,12.f,assistDistance);
        const float smoothWeight=s.smoothAmount*.1f*smoothYield;
        const float assistWeight=s.assistAmount*.15f*assistAlignment*flickYield*proximity;
        if(smoothWeight==0.f&&assistWeight==0.f)return current; // no stale pull while disengaging
        Angle desired{};
        if(smoothWeight>0.f&&smoothDistance>.000001f)
        {
            for(int i=0;i<2;++i)
            {
                const float error=-smoothDelta[i],term=(velocity[i]+kernel.omega*error)*dt;
                desired[i]=((error+term)*kernel.decay-error)/dt*smoothWeight;
            }
        }
        if(assistWeight>0.f&&assistDistance>.000001f)
        {
            const float extra=std::min(input,assistDistance)*assistWeight/dt;
            for(int i=0;i<2;++i)desired[i]+=assistDelta[i]/assistDistance*extra;
        }
        // Separate destinations, ONE speed/acceleration budget. No sensitivity
        // cvar writes, no per-probe integration, and no hard hitbox-centre lock.
        Angle boundDelta{};
        for(int i=0;i<2;++i)
        {
            const float a=smoothWeight>0.f?smoothDelta[i]:0.f,b=assistWeight>0.f?assistDelta[i]:0.f;
            boundDelta[i]=desired[i]>=0.f?std::max(a,b):std::min(a,b);
        }
        const float distance=Length(boundDelta),reserve=kernel.reserve;
        const float braking=std::sqrt(2.f*s.acceleration*distance+reserve*reserve)-reserve;
        desired=Limit(desired,std::min(s.speed,braking));
        Angle change=Limit({desired[0]-velocity[0],desired[1]-velocity[1]},s.acceleration*dt);
        Angle step=Limit({(velocity[0]+change[0])*dt,(velocity[1]+change[1])*dt},s.speed*dt);
        for(int i=0;i<2;++i)
        {
            // Yield immediately on reversal even when old correction has momentum.
            if(step[i]*desired[i]<=0.f||step[i]*boundDelta[i]<=0.f)step[i]=0.f;
            else if(std::abs(step[i])>std::abs(boundDelta[i]))step[i]=boundDelta[i];
        }
        return {std::clamp(current[0]+step[0],-89.f,89.f),Wrap(current[1]+step[1])};
    }
    struct Context
    {
        std::uint32_t local=0,weapon=0;
        int playerClass=0,bank=-1;
        Settings settings;
        int destination=0;
        int assistPreference=0;
        bool operator==(const Context&)const=default;
    };
    class Controller
    {
        int command=-1;
        Context context{};
        Kernel kernel{};
        Angle origin{}, priorVelocity{},velocity{},selected{},mouse{};
        std::uint32_t target=0,priorTarget=0,selectedTarget=0;
        float dt=0;
        bool enabled=false,used=false;
    public:
        void Reset(){*this=Controller{};}
        bool Enabled()const{return enabled;}
        bool WantsAssist()const{return enabled&&context.settings.responsiveAssist&&context.settings.assistAmount>0.f
            &&Length(mouse)>0.f&&Length(mouse)/dt<540.f;}
        Angle Velocity()const{return velocity;}
        std::uint32_t Target()const{return target;}
        void Begin(int number,Context next,bool active,Angle current,float interval,Angle input={})
        {
            next.settings=Normalize(next.settings);
            if(!active||!Finite(current)||!std::isfinite(interval)||interval<=0.f||interval>.1f){Reset();return;}
            // Re-prediction of the same command reuses its original snapshot.
            if(enabled&&number==command&&next==context)return;
            if(!enabled||number!=command+1||!(next==context)){velocity={};target=0;}
            command=number;context=next;origin=current;dt=interval;enabled=true;used=false;
            mouse=Finite(input)?input:Angle{};
            kernel=Prepare(context.settings,dt); // Exp/coefficient setup once, not per probe.
            priorVelocity=velocity;priorTarget=target;selectedTarget=0;
        }
        Angle Preview(std::uint32_t candidate,Angle goal)const
        {
            if(!enabled||!candidate)return origin;
            return StepGuidedPrepared(origin,goal,goal,candidate==priorTarget?priorVelocity:Angle{},kernel,mouse);
        }
        Angle Preview(std::uint32_t candidate,Angle smoothGoal,Angle assistGoal)const
        {
            if(!enabled||!candidate)return origin;
            return StepGuidedPrepared(origin,smoothGoal,assistGoal,candidate==priorTarget?priorVelocity:Angle{},kernel,mouse);
        }
        void Select(std::uint32_t candidate,Angle outgoing)
        {
            if(!enabled||!candidate||!Finite(outgoing))return;
            // Selection can be replaced by a secondary aim path, but it never
            // advances the frozen snapshot or integrates another time step.
            used=true;selected=outgoing;selectedTarget=candidate;
        }
        void Finish()
        {
            if(!enabled)return;
            if(!used){velocity={};target=0;return;}
            const auto delta=Difference(selected,origin);
            velocity={delta[0]/dt,delta[1]/dt};target=selectedTarget;
        }
    };
}
