#pragma once
#include <algorithm>
#include <cmath>

namespace MovementControlPolicy
{
    inline bool Finite(float a, float b) { return std::isfinite(a) && std::isfinite(b); }

    // Solve the ordinary TF WalkMove friction/acceleration step. Return a wish
    // speed, not a velocity mutation. A zero result leaves friction to stop us.
    // TF boosts acceleration below its low-wishspeed threshold; omitting that
    // branch makes a seemingly small counter-input reverse a slow player.
    inline float StopWishSpeed(float speed, float maximumWish, float accelerate,
        float friction, float stopSpeed, float surface, float dt, bool lostFooting)
    {
        if (!Finite(speed, maximumWish) || !Finite(accelerate, friction)
            || !Finite(stopSpeed, surface) || !std::isfinite(dt)
            || speed <= 0.f || maximumWish <= 0.f || accelerate <= 0.f
            || friction < 0.f || stopSpeed < 0.f || surface <= 0.f || dt <= 0.f)
            return 0.f;
        const float remaining = speed < 0.1f ? speed
            : std::max(0.f, speed - std::max(speed, stopSpeed) * friction * surface * dt);
        if (remaining <= 0.f) return 0.f;
        const float coefficient = accelerate * dt * surface;
        const float threshold = 100.f * friction * surface / accelerate;
        const float boostedBase = std::max(remaining, stopSpeed) * friction * surface;
        const float candidates[] = { maximumWish, remaining / coefficient,
            remaining / (dt * surface) - boostedBase };
        float bestWish = 0.f, bestBraking = 0.f;
        for (float wish : candidates)
        {
            wish = std::clamp(wish, 0.f, maximumWish);
            if (!std::isfinite(wish) || wish <= 0.f) continue;
            const float acceleration = !lostFooting && wish < threshold
                ? (boostedBase + wish) * dt * surface : coefficient * wish;
            // Never deliberately command a reversal. Floating error at an exact
            // stop is tolerated below the engine's own 0.1-unit speed threshold.
            if (std::isfinite(acceleration) && acceleration <= remaining + 0.00001f
                && acceleration > bestBraking)
                bestWish = wish, bestBraking = acceleration;
        }
        return bestWish;
    }

    inline bool HasDirection(float x, float y)
    {
        return Finite(x, y) && std::isfinite(std::hypot(x, y)) && std::hypot(x, y) >= 0.1f;
    }
}
