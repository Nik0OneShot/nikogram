#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace TriggerPolicy
{
    inline std::pair<double,double> Bounds(double lower,double upper)
    {
        lower=std::isfinite(lower)?std::clamp(lower,.05,.5):.05;
        upper=std::isfinite(upper)?std::clamp(upper,.05,.5):.15;
        if (lower>upper) std::swap(lower,upper);
        if (upper-lower<1e-6)
        { lower=std::min(lower,.45); upper=std::min(.5,lower+.05); }
        return {lower,upper};
    }
    inline std::optional<double> Delay(bool dynamic,double fixed,double lower,double upper,double randomUnit,bool disabled=false)
    {
        if (disabled) return 0.;
        if (!dynamic) return std::isfinite(fixed)?std::optional<double>(std::max(0.,fixed)):std::nullopt;
        if (!std::isfinite(randomUnit)) return std::nullopt;
        const auto bounds=Bounds(lower,upper);
        return bounds.first+(bounds.second-bounds.first)*std::clamp(randomUnit,0.,1.);
    }
    struct Timer
    {
        int target=-1,weapon=-1;
        double since=0,delay=0,last=0;
        bool active=false;
        void Reset() { *this={}; }
        bool Poll(int currentWeapon, double now, bool canFire)
        {
            if (!active) return false;
            if (currentWeapon!=weapon || !std::isfinite(now) || now<last || !canFire
                || now-last>.25 || now-since>delay+.25)
            { Reset(); return false; }
            last=now;
            return now-since+1e-9>=delay;
        }
        bool Update(int nextTarget,int nextWeapon,double now,double sampledDelay,bool canFire)
        {
            if (nextTarget<0 || nextWeapon<0 || !canFire || !std::isfinite(now) || !std::isfinite(sampledDelay) || sampledDelay<0)
            {Reset();return false;}
            if (!active || target!=nextTarget || weapon!=nextWeapon || now<last || now-last>.25)
            {target=nextTarget;weapon=nextWeapon;since=now;delay=sampledDelay;active=true;}
            last=now;
            return canFire && now-since+1e-9>=delay;
        }
    };
}
