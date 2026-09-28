#pragma once
#include <cmath>

// Shadow-only screening estimate, NOT a server damage calculation or hit guarantee.
namespace AmmoEvidencePolicy
{
    inline bool TimingCandidate(bool sticky, bool armed, bool request, bool settled, bool manual, bool uncertain)
    {
        return sticky && armed && request && settled && !manual && !uncertain;
    }
    inline float SplashScreen(float damage, float distance, float radius)
    {
        if (!std::isfinite(damage) || !std::isfinite(distance) || !std::isfinite(radius)
            || damage <= 0 || distance < 0 || radius <= 0 || distance >= radius) return 0;
        // Deliberately discount nominal damage; defenses/timing are separate gates.
        return damage * .5f * (1.f - .5f * distance / radius);
    }
    inline bool ScreenCovered(float damage, int health, bool complete, bool uncertain)
    {
        return complete && !uncertain && health > 0 && std::isfinite(damage)
            && std::floor(damage) >= health;
    }
}
