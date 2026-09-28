#pragma once
#include <algorithm>
#include <cmath>

namespace BowChargePolicy
{
    inline float Fraction(float now, float begin, float maxTime=1.f)
    {
        if (!std::isfinite(now) || !std::isfinite(begin) || begin <= 0.f)
            return 0.f;
        if (!std::isfinite(maxTime) || maxTime<=0.f) maxTime=1.f;
        // Valve caps GetCurrentCharge at one second before normalizing by max time.
        return std::clamp(std::clamp(now - begin, 0.f, 1.f)/maxTime, 0.f, 1.f);
    }
    inline float BodyDamage(float fraction,float base,float charged,float boost,float resistance)
    {
        if(!std::isfinite(fraction)||!std::isfinite(base)||!std::isfinite(charged)
            ||!std::isfinite(boost)||!std::isfinite(resistance)||base<0||charged<0||boost<0||resistance<0) return 0;
        return (base+charged*std::clamp(fraction,0.f,1.f))*boost*std::min(resistance,1.f);
    }
    inline bool Lethal(float damage,int health,bool uncertain)
    { return !uncertain && health>0 && std::isfinite(damage) && std::floor(damage)>=health; }
    inline float Center(float low,float high) { return (low+high)*.5f; }
}
