#pragma once
#include <cmath>
#include <algorithm>
#include <array>

namespace GrenadeCollisionPolicy
{
    // Keep grenade foot candidates inside the lower body, above floor contact.
    inline std::array<float,2> FeetHeights(float bottom,float top,float hull,float shift)
    {
        const float height=std::max(0.f,top-bottom);
        const float primary=std::clamp(std::max({shift,hull+4.f,height*.20f}),0.f,height*.35f);
        const float fallback=std::clamp(std::max(primary+4.f,height*.32f),primary,height*.45f);
        return {bottom+primary,bottom+fallback};
    }
    // Equal-distance blockers win. Never replace a trace that begins in solid world geometry.
    inline bool PreferTarget(bool allowed, bool targetHit, float targetFraction,
        bool worldHit, float worldFraction, bool worldStartSolid, bool worldAllSolid)
    {
        if (!allowed || !targetHit || !std::isfinite(targetFraction) || targetFraction < 0.f || targetFraction > 1.f
            || !std::isfinite(worldFraction) || worldFraction < 0.f || worldFraction > 1.f
            || worldStartSolid || worldAllSolid)
            return false;
        return !worldHit || targetFraction < worldFraction;
    }
}
