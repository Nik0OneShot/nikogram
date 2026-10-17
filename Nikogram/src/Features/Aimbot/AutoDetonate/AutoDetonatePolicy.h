#pragma once
#include <algorithm>
#include <cmath>

namespace AutoDetonatePolicy
{
    // The local prediction clock may already include command travel. Never add
    // another RTT to it. The channel clock is an estimate, bounded to the same
    // one-second budget as the existing projectile timing policy.
    inline float SafetyTime(float now,float clock,float outgoing,float choke)
    {
        if(!std::isfinite(now))return now;
        if(!std::isfinite(clock)||!std::isfinite(outgoing)||!std::isfinite(choke)
            ||clock<=0||std::abs(clock-now)>1.f)return now;
        return now+std::clamp(clock+std::clamp(outgoing,0.f,1.f)+std::clamp(choke,0.f,.35f)-now,0.f,1.f);
    }
    inline bool ArmData(float now,float created,float arm)
    {return std::isfinite(now)&&std::isfinite(created)&&created>0&&std::isfinite(arm)&&arm>=0;}
    inline bool Armed(float now,float created,float arm)
    {return ArmData(now,created,arm)&&now-created>=arm;}
    inline float ExposureTime(float now,float estimated,float created,float arm)
    {
        if(!ArmData(now,created,arm)||!std::isfinite(estimated)||created>std::max(now,estimated))return NAN;
        // Native quick-arm launchers latch an early pulse and explode at arming.
        // This is a safety-only inclusion, never permission to request early.
        float at=arm<=.5f?std::max({now,estimated,created+arm}):std::max(now,estimated);
        // Preserve the age>=arm contract across float addition/subtraction.
        if(arm<=.5f&&at-created<arm)at=std::nextafter(at,INFINITY);
        return at;
    }
    // Smart detonation. Per enemy: blast damage at the server-receive time
    // ("now") and a short lookahead ("next"), using native half falloff over
    // min(distance to centre, distance to origin). Detonate at once when any
    // enemy would die, has reached its closest approach, or is leaving; wait
    // only while every exposed enemy is still getting closer (or a flare is
    // about to hit one directly), and never longer than maxWait.
    struct Exposure { float now=0, next=0; bool lethal=false, directSoon=false; };
    enum class Timing { Detonate, Wait };
    inline float HalfFalloff(float base,float distance,float radius)
    {
        if(!std::isfinite(base)||!std::isfinite(distance)||!std::isfinite(radius)||base<=0||distance<0||radius<=0)return 0;
        return base*(1.f-.5f*std::clamp(distance/radius,0.f,1.f));
    }
    template<class Range>
    inline Timing Decide(const Range& exposures,bool worldSoon,float waited,float maxWait)
    {
        bool any=false,wait=false;
        for(const Exposure& e:exposures)
        {
            if(!(e.now>0)&&!e.directSoon)continue;
            any=true;
            if(e.lethal)return Timing::Detonate;
            if(e.directSoon){wait=true;continue;}
            if(e.next>e.now*1.02f)wait=true;
            else return Timing::Detonate; // closest approach reached, or leaving
        }
        if(!any||worldSoon||!wait)return Timing::Detonate;
        return std::isfinite(waited)&&waited>=0&&waited<maxWait?Timing::Wait:Timing::Detonate;
    }
    inline float AirScale(float age,float live,float ramp,float air)
    {
        if(!std::isfinite(age)||!std::isfinite(live)||!std::isfinite(ramp)||!std::isfinite(air)
            ||live<0||ramp<0||air<0)return NAN;
        const float t=ramp==0?(age>=live?1.f:0.f):std::clamp((age-live)/ramp,0.f,1.f);
        return air+(1.f-air)*t;
    }
}
