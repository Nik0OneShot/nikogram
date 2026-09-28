#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
namespace TargetPolicy
{
    inline std::size_t CandidateCount(std::size_t count, int limit, bool hasPriority)
    {
        // Priority must not remove the candidates needed when it fails CanHit.
        const auto budget = std::size_t(std::max(0, limit));
        return std::min(count, budget + (hasPriority ? std::min(budget, std::size_t(2)) : 0));
    }
    inline std::vector<std::size_t> Candidates(std::size_t count, int limit, bool priority, unsigned tick)
    {
        const auto primary = std::min(count, std::size_t(std::max(0, limit)));
        std::vector<std::size_t> result;
        for (std::size_t i=0;i<primary;++i) result.push_back(i);
        const auto total = CandidateCount(count, limit, priority);
        if (total > primary)
        {
            const auto remaining=count-primary, extra=total-primary;
            // Keep the best fallback when there is room; rotate the last slot fairly.
            if (extra > 1) result.push_back(primary);
            const auto fixed = extra > 1 ? 1u : 0u;
            result.push_back(primary + fixed + tick % (remaining-fixed));
        }
        return result;
    }
    inline bool HasFlightTick(int tick) { return tick >= 1; }
    inline bool SplashRisk(bool enabled, bool invulnerable, float radius, float distanceSquared, bool visible)
    {
        return enabled && !invulnerable && std::isfinite(radius) && radius > 0.f
            && std::isfinite(distanceSquared) && distanceSquared <= radius * radius && visible;
    }
}
