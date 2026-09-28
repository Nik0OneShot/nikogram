#pragma once
#include <cmath>
namespace MeleeTracePolicy
{
    // A direct clip is only a recovery for missed broadphase discovery, not
    // permission to ignore an earlier world/entity collision or initial overlap.
    inline bool Recover(bool allowed, bool targetHit, float targetFraction,
        bool targetStartSolid, bool targetAllSolid, float obstacleFraction,
        bool obstacleStartSolid, bool obstacleAllSolid)
    {
        return allowed && targetHit && !targetStartSolid && !targetAllSolid
            && !obstacleStartSolid && !obstacleAllSolid
            && std::isfinite(targetFraction) && std::isfinite(obstacleFraction)
            && targetFraction > 0.f && targetFraction < 1.f
            && obstacleFraction >= 0.f && obstacleFraction <= 1.f
            && targetFraction + 0.0001f < obstacleFraction;
    }
}
