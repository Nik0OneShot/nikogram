#pragma once
#include <cmath>
#include <cstdint>
#include <array>
#include <algorithm>
namespace AmmoConservationPolicy
{
    struct ChargeOwnership
    {
        int owner=0,weapon=0;
        bool automatedHold=false,manualCharge=false;
        void Observe(int who,int held,float begin,bool manual)
        {
            if (owner!=who || weapon!=held) {*this={};owner=who;weapon=held;}
            if (!std::isfinite(begin) || begin<0) {automatedHold=false;manualCharge=true;return;}
            if (begin==0 && !manual) manualCharge=false;
            if (manual) {manualCharge=true;automatedHold=false;}
        }
        bool Owned(float begin) const
        {return std::isfinite(begin) && begin>=0 && !manualCharge && (begin==0 || automatedHold);}
        void Record(bool outgoingHold,bool automatic)
        {automatedHold=automatic && outgoingHold && !manualCharge;}
    };
    inline ChargeOwnership chargeOwnership;
    inline bool StickyHoldSafe(float begin,float now,float maximum,float delay)
    {
        if (!std::isfinite(begin) || !std::isfinite(now) || !std::isfinite(maximum) || !std::isfinite(delay)
            || begin<0 || delay<=0 || delay>.15f || maximum<.5f || maximum>10.f) return false;
        if (begin==0) return true;
        const float elapsed=now-begin;
        // Includes the whole 100-ms action window plus a scheduling margin.
        return elapsed>=0 && elapsed+delay+.25f<maximum;
    }
    // A projectile center must enter the target's interior, not merely graze
    // an expanded collision hull. All inputs are command-local predictions.
    inline float CoreFraction(const std::array<float,3>& from, const std::array<float,3>& to,
        const std::array<float,3>& mins, const std::array<float,3>& maxs, float inset)
    {
        if (!std::isfinite(inset) || inset<0) return -1.f;
        float enter=0,exit=1;
        for (int axis=0;axis<3;++axis)
        {
            if (!std::isfinite(from[axis]) || !std::isfinite(to[axis]) || !std::isfinite(mins[axis]) || !std::isfinite(maxs[axis])) return -1.f;
            const float low=mins[axis]+inset,high=maxs[axis]-inset,d=to[axis]-from[axis];
            if (!std::isfinite(d) || low>=high) return -1.f;
            if (std::abs(d)<.00001f) {if (from[axis]<low || from[axis]>high) return -1.f;continue;}
            const float a=(low-from[axis])/d,b=(high-from[axis])/d;
            enter=std::max(enter,std::min(a,b));exit=std::min(exit,std::max(a,b));
            if (enter>exit) return -1.f;
        }
        return enter;
    }
    inline bool FreshPill(float birthAge, float fuse, float creation, float lastFire)
    {
        // Client birth is NOT a fuse clock. This only restricts direct-contact
        // evidence to recently observed shots correlated with our firing time.
        return std::isfinite(birthAge) && std::isfinite(fuse) && std::isfinite(creation) && std::isfinite(lastFire)
            && birthAge>=0 && birthAge<=.9f && fuse>=1.2f && fuse<=3.f
            && creation>0 && lastFire>0 && std::abs(creation-lastFire)<=.25f;
    }
    inline bool ImpactWindow(float delay, float flight, float observationAge)
    {
        return std::isfinite(delay) && std::isfinite(flight) && std::isfinite(observationAge)
            && delay > 0 && flight >= 0 && flight <= .1f && observationAge >= 0 && observationAge <= .1f
            && delay + flight + observationAge <= .15f;
    }
    inline bool Covered(float estimate, int health)
    {
        // Estimates are already lower bounds. Do not require another overkill
        // multiplier, and never treat a near-dead survivor as a confirmed kill.
        return health > 0 && std::isfinite(estimate) && std::floor(estimate) >= health;
    }
    using Point=std::array<float,3>;
    inline bool FinitePoint(const Point& p)
    {return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);}
    inline float PointDistance(const Point& a,const Point& b)
    {float d=0;for(int i=0;i<3;++i)d+=(a[i]-b[i])*(a[i]-b[i]);return std::sqrt(d);}
    struct StationaryTrack
    {
        uint32_t identity=0;float begin=0,last=0;Point anchor={};bool valid=false;
        void Observe(uint32_t id,float sim,const Point& origin,float speed,bool grounded)
        {
            if(!id || !std::isfinite(sim) || !FinitePoint(origin) || !std::isfinite(speed)
                || speed>1 || speed<0 || !grounded) {valid=false;return;}
            if(!valid || identity!=id || sim<last || sim-last>.1f || PointDistance(anchor,origin)>1.f)
            {identity=id;begin=sim;anchor=origin;valid=true;}
            last=sim;
        }
        bool Ready(uint32_t id,float sim,const Point& origin) const
        {return valid && identity==id && std::isfinite(sim) && std::abs(sim-last)<.001f
            && last-begin>=.2f && FinitePoint(origin) && PointDistance(anchor,origin)<=1.f;}
    };
    inline constexpr float StationaryRocketWindow=.9f;
    inline bool StationaryImpactWindow(float response,float flight,float observationAge)
    {
        return std::isfinite(response) && std::isfinite(flight) && std::isfinite(observationAge)
            && response>0 && response<=.15f && flight>=0 && observationAge>=0 && observationAge<=.1f
            && response+flight+observationAge<=StationaryRocketWindow;
    }
    inline float StationaryEnvelope(float total)
    {
        // Confidence allowance for recently stationary players, NOT a physical
        // bound on possible future acceleration. Recheck every command and stop
        // withholding as soon as the observed stationary assumption fails.
        return 4.f+32.f*total*total;
    }
    struct RocketContact
    {
        bool blocked=false,solid=false,world=false,sky=false;
        uintptr_t player=0;float fraction=1;Point point={},normal={};
    };
    struct BackedRocketEvidence
    {bool valid=false;float flight=0,firstFlight=0;Point nearPoint={},worldPoint={};};
    inline bool PreferStationaryEvidence(bool legacyAccepted,float response,float firstFlight,float observationAge)
    {return !legacyAccepted && !ImpactWindow(response,firstFlight,observationAge);}
    // Shared by the actual client collector and geometry-backed regression
    // scenes. Trace must report the FIRST obstacle, never a chosen endpoint.
    template<class Trace,class Stationary>
    BackedRocketEvidence TraceStationaryRocket(const Point& origin,const Point& velocity,
        float response,float observationAge,Trace&& trace,Stationary&& stationary)
    {
        BackedRocketEvidence out;
        if(!StationaryImpactWindow(response,0,observationAge) || !FinitePoint(origin) || !FinitePoint(velocity)) return out;
        const float speed=PointDistance(velocity,Point{});
        if(speed<100 || speed>4000) return out;
        const float duration=StationaryRocketWindow-response-observationAge;
        if(duration<=0) return out;
        Point end;for(int i=0;i<3;++i)end[i]=origin[i]+velocity[i]*duration;
        const auto acceptable=[](const RocketContact& hit) {return hit.blocked && !hit.solid && !hit.sky
            && std::isfinite(hit.fraction) && hit.fraction>=0 && hit.fraction<=1 && FinitePoint(hit.point) && FinitePoint(hit.normal);};
        const RocketContact first=trace(origin,end,uintptr_t(0));
        if(!acceptable(first)) return out;
        RocketContact world=first;
        if(!first.world)
        {
            if(!first.player || !stationary(first.player)) return out;
            world=trace(origin,end,first.player);
            if(!acceptable(world) || !world.world || world.fraction<first.fraction) return out;
        }
        out.flight=duration*world.fraction;
        out.firstFlight=duration*first.fraction;
        if(!StationaryImpactWindow(response,out.flight,observationAge)) return {};
        out.nearPoint=first.point;
        for(int i=0;i<3;++i)out.worldPoint[i]=world.point[i]+world.normal[i]*.5f;
        if(first.world) out.nearPoint=out.worldPoint;
        out.valid=true;return out;
    }
    inline float SplashDamage(float damage, float distance, float radius)
    {
        if (!std::isfinite(damage) || !std::isfinite(distance) || !std::isfinite(radius)
            || damage<=0 || distance<0 || radius<=0 || distance>=radius) return 0;
        return damage*(1.f-.5f*distance/radius);
    }
    inline bool BackedImpactWindow(float response,float flight,float observationAge)
    {
        // A low rocket contact can miss the eroded body but still hit the floor
        // shortly behind it. Only a separately traced fixed-world impact gets
        // this longer window; ordinary direct-contact evidence is unchanged.
        return std::isfinite(response) && std::isfinite(flight) && std::isfinite(observationAge)
            && response>0 && flight>=0 && observationAge>=0 && observationAge<=.1f
            && response+flight+observationAge<=.25f;
    }
    inline float BackedSplashDamage(float damage,float nearDistance,float worldDistance,float radius,float uncertainty)
    {
        if(!std::isfinite(uncertainty) || uncertainty<0 || !std::isfinite(nearDistance) || nearDistance<0
            || !std::isfinite(worldDistance) || worldDistance<0) return 0;
        // The near collision can shift with the player; the world endpoint
        // cannot. Bound both endpoints rather than crediting direct-hit damage.
        return SplashDamage(damage,std::max(nearDistance+2*uncertainty,worldDistance+uncertainty),radius);
    }
    inline float GroundedProbeHeight(float height,float feet)
    {
        // A stationary grounded target cannot occupy a point below its support
        // surface. Keep uncertainty probes inside that physically possible range.
        return std::max(height,feet+1.f);
    }
    inline float DistanceDamage(float damage,float distance,float range,bool spreadDisabled,bool rocket)
    {
        if (!std::isfinite(damage) || !std::isfinite(distance) || !std::isfinite(range)
            || damage<=0 || distance<0 || range<0 || range>1) return 0;
        // Valve's SimpleSpline damage-distance curve. Credit no close-range
        // bonus, crit, minicrit, or random upward spread.
        float x=std::clamp(1.f-distance/1024.f,0.f,1.f);
        if (!spreadDisabled) x=std::max(0.f,x-.1f);
        float amount=damage*range;
        if (rocket && x>.5f) amount*=.5f;
        const float spline=x*x*(3.f-2.f*x);
        return std::min(damage,damage+amount*(2.f*spline-1.f));
    }
    enum class Action { Shoot, Switch, Withhold };
    inline Action Decide(bool lethal,bool fresh,bool alternativeValidated)
    {
        if (!lethal || !fresh) return Action::Shoot;
        return alternativeValidated?Action::Switch:Action::Withhold;
    }
}
