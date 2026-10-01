#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace CombatPriorityPolicy
{
    // Optional categories break ties within explicit player-list priority.
    // Uber displacement outranks medics, which outrank burning players.
    inline int Priority(int base, bool enabled, bool medic, bool burning, bool uber)
    {
        if (!enabled) return base;
        const std::int64_t score=std::int64_t(base)*8+(uber?4:0)+(medic?2:0)+(burning?1:0);
        return int(std::clamp(score,std::int64_t(std::numeric_limits<int>::min()),std::int64_t(std::numeric_limits<int>::max())));
    }
    inline float FOV(bool projectile, float general, float specific)
    {
        const float value=projectile?specific:general;
        return std::isfinite(value)?std::clamp(value,0.f,180.f):0.f;
    }
}
