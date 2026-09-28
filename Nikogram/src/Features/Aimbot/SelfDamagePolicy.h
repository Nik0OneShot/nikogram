#pragma once
#include <algorithm>
#include <cmath>
namespace SelfDamagePolicy
{
    inline bool Block(float damage, float maxHealth, float health, float protection)
    {
        if(std::isfinite(protection) && protection<=0) return false;
        if(!std::isfinite(damage)||!std::isfinite(maxHealth)||!std::isfinite(health)||!std::isfinite(protection)) return true;
        if(damage<=0) return false;
        const float allowance=std::max(0.f,maxHealth)*(1.f-std::clamp(protection,0.f,100.f)/100.f);
        return damage>allowance || damage>=health;
    }
}
