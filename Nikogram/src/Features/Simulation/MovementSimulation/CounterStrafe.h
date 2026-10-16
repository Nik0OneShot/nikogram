#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace CounterStrafe
{
    inline constexpr int MinimumReversals=1; // One turn is observed motion; recurrence requires at least two.
    inline constexpr float HistorySeconds=2.1f;
    struct Sample { float time, x, y, vx, vy; };
    enum class Kind { None, Single, Counter, Snake };
    struct Estimate { bool valid = false; float ax = 0, ay = 0, center = 0, drift = 0, speed = 0, confidence = 0; float driftLimit=1e30f; Kind kind=Kind::None; bool repeated=false; float response=.15f,along=0.f; };
    struct Audit { const char* reason="not_evaluated"; float duration=0,variance=0,width=0,drift=0,agreement=0; int reversals=0,positive=0,negative=0; bool rapid=false;
        float axisX=0,axisY=0,low=0,high=0,age=0; unsigned rejectionMask=0;
        float meanDrift=0,interval=0,freshness=0,widthLimit=96; bool extended=false;
        float regularity=0,legMean=0; int legs=0; bool gapTrimmed=false; };

    inline float DriftOffset(const Estimate& e,float elapsed) { return std::clamp(e.drift*elapsed,-e.driftLimit,e.driftLimit); }
    inline float DriftVelocity(const Estimate& e,float elapsed) { return std::fabs(e.drift*elapsed)>=e.driftLimit?0.f:e.drift; }
    inline float Correction(const Estimate& e, float position, float velocity, float elapsed)
    {
        if(!e.valid || !e.repeated) return 0.f;
        const float error=e.center+DriftOffset(e,elapsed)-position;
        const float desired=DriftVelocity(e,elapsed)+std::clamp(error/e.response,-e.speed,e.speed);
        return desired-velocity;
    }

    inline float AlongCorrection(const Estimate& e,float velocity)
    {return e.valid&&e.kind==Kind::Snake?e.along-velocity:0.f;}

    // Diagnostic decomposition only. Zero-drift is a same-state sensitivity check,
    // not a resimulated trajectory or a proposed change to the live controller.
    struct CorrectionAudit {
        float driftOffset, centerError, rawSteering, steering, blend, desired, correction, zeroDriftCorrection;
        bool saturated, opposesVelocity;
    };
    inline CorrectionAudit Explain(const Estimate& e, float position, float velocity, float elapsed)
    {
        const float driftOffset=DriftOffset(e,elapsed), error=e.center+driftOffset-position;
        const float raw=error/e.response, steering=std::clamp(raw,-e.speed,e.speed);
        const float blend=e.repeated?1.f:0.f, desired=DriftVelocity(e,elapsed)+steering;
        auto zero=e; zero.drift=0;
        return {driftOffset,error,raw,steering,blend,desired,Correction(e,position,velocity,elapsed),
            Correction(zero,position,velocity,elapsed),std::fabs(raw)>e.speed,
            std::fabs(velocity)>5.f && desired*velocity<0.f};
    }

    // World-space history only. No eye/view angles, output smoothing, or forecast
    // reversal schedule. Recognition is rebuilt from received observations.
    inline Estimate Detect(const std::vector<Sample>& samples, Audit* audit=nullptr,
        bool counter=true, bool snake=true,float tickInterval=.015f)
    {
        if(audit) *audit={};
        const auto reject=[&](const char* reason)->Estimate {if(audit)audit->reason=reason;return {};};
        if((!counter&&!snake)||samples.size()<6) return reject("insufficient_samples");
        size_t first=0;
        while(first+6<samples.size() && samples.back().time-samples[first].time>HistorySeconds) ++first;
        std::vector<Sample> s(samples.begin()+first,samples.end());
        // A lost/late packet or a bad sample only invalidates history BEFORE it.
        // Keep the contiguous recent run instead of discarding 2.1 s of evidence.
        size_t keep=0;
        for(size_t i=s.size();i-->0;) {
            const auto& v=s[i];
            if(!std::isfinite(v.time)||!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.vx)||!std::isfinite(v.vy)) {keep=i+1;break;}
            if(i) {const float dt=v.time-s[i-1].time;if(!(dt>0) || dt>.12f){keep=i;break;}}
        }
        if(keep) {
            s.erase(s.begin(),s.begin()+std::ptrdiff_t(keep));
            if(audit)audit->gapTrimmed=true;
            if(s.size()<6) return reject("gap_trimmed_history");
        }
        const auto& now=s.back();
        const float duration=now.time-s.front().time;
        if(audit)audit->duration=duration;
        if(duration<.075f) return reject("history_duration");
        if(std::hypot(now.vx,now.vy)<8.f && std::hypot(s[s.size()-2].vx,s[s.size()-2].vy)<8.f
            && now.time-s[s.size()-2].time>=.012f) return reject("stopped_pattern");
        // Time-weighted covariance prevents bursty packet sampling biasing the axis.
        float mx=0,my=0,xx=0,xy=0,yy=0;
        for(size_t i=1;i<s.size();++i) {const float dt=s[i].time-s[i-1].time;mx+=s[i].vx*dt;my+=s[i].vy*dt;}
        mx/=duration;my/=duration;
        for(size_t i=1;i<s.size();++i) {const float dt=s[i].time-s[i-1].time,x=s[i].vx-mx,y=s[i].vy-my;xx+=x*x*dt;xy+=x*y*dt;yy+=y*y*dt;}
        float angle=.5f*std::atan2(2*xy,xx-yy),ax=std::cos(angle),ay=std::sin(angle);
        bool axisCoherent=(xx*ax*ax+2*xy*ax*ay+yy*ay*ay)>=.82f*(xx+yy);
        if(!axisCoherent) {
            // A preceding forward run can dominate velocity covariance after
            // lateral reversals begin. Try their observed change axis instead;
            // retain the same coherence, reversal and position checks below.
            float dx2=0,dxy=0,dy2=0;
            for(size_t i=1;i<s.size();++i) {
                const float dt=s[i].time-s[i-1].time,dx=s[i].vx-s[i-1].vx,dy=s[i].vy-s[i-1].vy;
                dx2+=dx*dx/dt;dxy+=dx*dy/dt;dy2+=dy*dy/dt;
            }
            angle=.5f*std::atan2(2*dxy,dx2-dy2);ax=std::cos(angle);ay=std::sin(angle);
            axisCoherent=(dx2*ax*ax+2*dxy*ax*ay+dy2*ay*ay)>=.82f*(dx2+dy2);
        }
        const float variance=(xx*ax*ax+2*xy*ax*ay+yy*ay*ay)/duration;
        if(audit){audit->axisX=ax;audit->axisY=ay;audit->variance=variance;}
        // A separate adjacent-tick hypothesis for actually observed tiny alternation.
        // Never infer this path from skipped snapshots or merely decelerating velocity.
        if(s.size()>=10 && std::isfinite(tickInterval) && tickInterval>0.f){
            const size_t first=s.size()-10;
            float rx=0,ry=0,rxx=0,rxy=0,ryy=0;
            for(size_t i=first;i<s.size();++i){rx+=s[i].vx;ry+=s[i].vy;}rx/=10.f;ry/=10.f;
            for(size_t i=first;i<s.size();++i){const float x=s[i].vx-rx,y=s[i].vy-ry;rxx+=x*x;rxy+=x*y;ryy+=y*y;}
            const float rapidAngle=.5f*std::atan2(2*rxy,rxx-ryy),ax=std::cos(rapidAngle),ay=std::sin(rapidAngle);
            bool coherent=(rxx*ax*ax+2*rxy*ax*ay+ryy*ay*ay)>=.95f*(rxx+ryy);int previous=0;float peak=0,phaseDot=0,phaseObserved=0,phasePredicted=0;
            const float tolerance=std::max(tickInterval*.04f,1.25f*(std::nextafter(now.time,std::numeric_limits<float>::infinity())-now.time));
            auto projectPosition=[&](size_t i){return s[i].x*ax+s[i].y*ay;};
            auto projectVelocity=[&](size_t i){return s[i].vx*ax+s[i].vy*ay;};
            for(size_t i=first;i<s.size();++i){const float v=projectVelocity(i);const int sign=v>4.f?1:v<-4.f?-1:0;
                peak=std::max(peak,std::fabs(v));if(!sign||(previous&&sign==previous)){coherent=false;break;}previous=sign;
                if(i>first){const float delta=s[i].time-s[i-1].time,expected=v*delta,actual=projectPosition(i)-projectPosition(i-1);
                    phaseDot+=actual*expected;phaseObserved+=actual*actual;phasePredicted+=expected*expected;
                    if(std::fabs(delta-tickInterval)>tolerance||std::fabs(actual-expected)>std::fabs(expected)*.35f+.002f){coherent=false;break;}}
            }
            if(coherent){const size_t n=s.size();const float drift=.5f*((projectPosition(n-2)-projectPosition(first))/(s[n-2].time-s[first].time)+(projectPosition(n-1)-projectPosition(first+1))/(s[n-1].time-s[first+1].time));
                float low=1e30f,high=-1e30f,along=0;
                for(size_t i=first;i<n;++i){const float q=projectPosition(i)+drift*(now.time-s[i].time);low=std::min(low,q);high=std::max(high,q);along+=-s[i].vx*ay+s[i].vy*ax;}
                along/=10.f;const float forward=-now.vx*ay+now.vy*ax;const bool traveling=std::fabs(along)>35.f&&forward*along>0;
                if(high-low>=.1f && high-low<=peak*tickInterval*2.f && std::fabs(drift)<peak*.7f && ((traveling&&snake)||(!traveling&&counter))){
                    // Nine received alternations in a row on exact tick spacing: regular by construction.
                    Estimate e{true,ax,ay,(low+high)*.5f,drift,peak,.9f};e.kind=traveling?Kind::Snake:Kind::Counter;e.repeated=true;e.response=.065f;
                    e.along=std::copysign(std::min(std::fabs(forward),std::fabs(along)),forward);
                    if(audit){audit->reason=traveling?"accepted_rapid_snake":"accepted_rapid_counter";audit->reversals=9;audit->rapid=true;audit->low=low;audit->high=high;audit->width=high-low;audit->drift=drift;}
                    return e;
                }
            }
        }
        if(variance<900.f || !axisCoherent)return reject("weak_or_multiaxis_variation");
        const float threshold=std::max(20.f,std::sqrt(variance)*.22f);
        auto pos=[&](size_t i){return s[i].x*ax+s[i].y*ay;};
        auto vel=[&](size_t i){return s[i].vx*ax+s[i].vy*ay;};
        // Require two coherent samples and real displacement before confirming a
        // reversal. Hysteresis ignores neutral samples, but does not average aim.
        std::vector<size_t> turns;std::vector<float> phaseTime,phasePosition,phaseError;int sign=0,pending=0;size_t begin=0;
        for(size_t i=0;i<s.size();++i) {
            const float v=vel(i);const int next=v>threshold?1:v<-threshold?-1:0;
            if(!next){pending=0;continue;}
            if(next==sign){pending=0;continue;}
            if(next!=pending){pending=next;begin=i;continue;}
            if(i>begin && s[i].time-s[begin].time>=.012f && std::fabs(pos(i)-pos(begin))>=.35f) {
                if(sign){
                    turns.push_back(begin);
                    size_t crossing=begin;
                    while(crossing>0 && vel(crossing-1)*sign<=0.f)--crossing;
                    float time=s[begin].time,point=pos(begin),uncertainty=0;
                    if(crossing>0){const float v0=vel(crossing-1),v1=vel(crossing),span=s[crossing].time-s[crossing-1].time;
                        const float fraction=std::clamp(v0/(v0-v1),0.f,1.f);
                        time=s[crossing-1].time+fraction*span;
                        point=pos(crossing-1)+(pos(crossing)-pos(crossing-1))*fraction;
                        uncertainty=std::fabs(v1-v0)*span*.125f;
                    }
                    phaseTime.push_back(time);phasePosition.push_back(point);phaseError.push_back(uncertainty);
                }
                sign=next;pending=0;
            }
        }
        if(audit)audit->reversals=int(turns.size());
        if(turns.empty())return reject("no_confirmed_reversal");
        const float age=now.time-s[turns.back()].time;
        if(audit)audit->age=age;
        // Reject unresolved direction changes/neutral stops immediately. A new
        // received direction must not continue steering toward an old corridor.
        if(turns.size()==1 && counter && vel(s.size()-1)*sign<threshold*.25f)return reject("direction_changed_or_stopped");
        float dot=0,observed=0,predicted=0;
        for(size_t i=1;i<s.size();++i) {
            const float a=pos(i)-pos(i-1),b=(vel(i)+vel(i-1))*.5f*(s[i].time-s[i-1].time);
            dot+=a*b;observed+=a*a;predicted+=b*b;
        }
        const float agreement=observed>0&&predicted>0?dot/std::sqrt(observed*predicted):0;
        if(audit)audit->agreement=agreement;
        if(agreement<.8f || observed<.05f || predicted<.05f || observed>predicted*4 || predicted>observed*4)return reject("position_velocity_disagreement");
        if(turns.size()==1) {
            if(!counter)return reject("pattern_toggle_disabled");
            if(age>.15f)return reject("single_turn_finished");
            if(audit)audit->reason="single_reversal";
            // One reversal says nothing about a rhythm yet.
            Estimate e{true,ax,ay,pos(s.size()-1),0,std::fabs(vel(s.size()-1)),.5f};e.kind=Kind::Single;return e;
        }
        // Fit only completed legs. The live leg is validation data, never part
        // of the fitted corridor (otherwise breakout rejection is tautological).
        const size_t last=turns.back(),start=turns[turns.size()>5?turns.size()-5:0];
        // Sampling uncertainty belongs to the fitted recent legs and live leg,
        // not to unrelated older motion retained for slow-pattern recognition.
        float recentDt=0;
        for(size_t i=start+1;i<s.size();++i)recentDt=std::max(recentDt,s[i].time-s[i-1].time);
        const float interval=s[last].time-s[turns[turns.size()-2]].time;
        float drift=0;
        if(turns.size()>=3) {
            std::vector<float> cycles,uncertainties;
            for(size_t i=2;i<turns.size();++i)if(turns[i-2]>=start){
                const float span=phaseTime[i]-phaseTime[i-2];
                cycles.push_back((phasePosition[i]-phasePosition[i-2])/span);
                uncertainties.push_back((phaseError[i]+phaseError[i-2])/span);
            }
            std::sort(cycles.begin(),cycles.end());drift=cycles[cycles.size()/2];
            std::sort(uncertainties.begin(),uncertainties.end());
            if(std::fabs(drift)<uncertainties[uncertainties.size()/2])drift=0.f;
        }
        float low=1e30f,high=-1e30f,speed=0;
        for(size_t i=start;i<=last;++i) {
            const float value=pos(i)+drift*(now.time-s[i].time);
            low=std::min(low,value);high=std::max(high,value);speed=std::max(speed,std::fabs(vel(i)-drift));
        }
        const float width=high-low;
        const bool rapid=interval<=.14f;
        const float sameLeg=turns.size()>=3?s[turns[turns.size()-2]].time-s[turns[turns.size()-3]].time:interval;
        const float freshness=std::clamp(sameLeg*1.15f+std::min(2.f*recentDt,.04f),.06f,.85f);
        if(audit){audit->low=low;audit->high=high;audit->width=width;audit->drift=drift;audit->rapid=rapid;audit->interval=interval;audit->freshness=freshness;audit->meanDrift=mx*ax+my*ay;}
        if(age>freshness)return reject("pattern_ended");
        // Require at least four received intervals per observed full cycle.
        // A fixed 40ms ceiling rejected coherent 22Hz and uneven 33Hz updates.
        const float sampleLimit=turns.size()>=3?(s[last].time-s[turns[turns.size()-3]].time)*.25f:interval*.5f;
        // Account only for float representation: one gap plus a quarter-cycle
        // subtraction can contribute 1.25 ULP at a large simulation clock.
        const float clockTolerance=std::max(.00001f,1.25f*(std::nextafter(now.time,std::numeric_limits<float>::infinity())-now.time));
        if(rapid && recentDt>sampleLimit+clockTolerance)return reject("rapid_sampling_too_sparse");
        if(width<1.f || width>std::max(96.f,speed*interval*1.6f) || std::fabs(drift)>speed*.7f)return reject("invalid_corridor");
        const float margin=std::max(1.5f,speed*std::min(recentDt,.03f)*.5f);
        if(pos(s.size()-1)<low-margin || pos(s.size()-1)>high+margin)return reject("outside_corridor");
        const float forward=-now.vx*ay+now.vy*ax;
        float longitudinal=0,alongTime=0;
        for(size_t i=start+1;i<s.size();++i) {const float dt=s[i].time-s[i-1].time;longitudinal+=(-s[i].vx*ay+s[i].vy*ax)*dt;alongTime+=dt;}
        longitudinal/=std::max(alongTime,.001f);
        const bool traveling=std::fabs(longitudinal)>35.f && forward*longitudinal>0 && std::fabs(forward)>std::fabs(longitudinal)*.45f;
        if((traveling&&!snake)||(!traveling&&!counter))return reject("pattern_toggle_disabled");
        // Preserve the actual travel sign and cap speed by observed sustained
        // displacement. A transient diagonal-speed peak must not send backward
        // (or forward) forecasts farther than the measured travel trend.
        // Confidence = how repeatable the rhythm is, not position/velocity
        // agreement (received velocities are position chords, so that agreement
        // is ~1 by construction). Leg spread, number of completed legs and an
        // overdue live leg all lower it.
        float legMean=0,legSpread=0;int legs=0;
        {
            const size_t firstLeg=phaseTime.size()>5?phaseTime.size()-5:1;
            for(size_t i=std::max<size_t>(firstLeg,1);i<phaseTime.size();++i){legMean+=phaseTime[i]-phaseTime[i-1];++legs;}
            if(legs)legMean/=float(legs);
            for(size_t i=std::max<size_t>(firstLeg,1);i<phaseTime.size();++i){const float d=phaseTime[i]-phaseTime[i-1]-legMean;legSpread+=d*d;}
            legSpread=legs>1?std::sqrt(legSpread/float(legs-1)):0.f;
        }
        const float variation=legs>1&&legMean>0?legSpread/legMean:.25f;
        const float regularity=std::clamp(1.f-variation/.5f,0.f,1.f);
        const float countFactor=legs>0?1.f-.25f/float(legs):0.f;
        const float overdue=legMean>0&&age>legMean?std::clamp(1.f-(age-legMean)/(.15f*legMean+.03f),.25f,1.f):1.f;
        if(audit){audit->regularity=regularity;audit->legMean=legMean;audit->legs=legs;}
        Estimate e{true,ax,ay,(low+high)*.5f,drift,speed,std::clamp(regularity*countFactor*overdue,0.f,1.f)};
        e.kind=traveling?Kind::Snake:Kind::Counter;e.repeated=true;
        e.along=std::copysign(std::min(std::fabs(forward),std::fabs(longitudinal)),forward);
        e.response=rapid?.065f:std::clamp(interval*.5f,.09f,.2f);
        if(audit)audit->reason=traveling?"accepted_snake":"accepted_counter";
        return e;
    }
    inline Estimate DetectWindow(const std::vector<Sample>& s,Audit* audit=nullptr,bool extended=false)
    { return Detect(s,audit); }
}

