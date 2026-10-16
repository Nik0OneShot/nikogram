#pragma once
#include <algorithm>
#include <cmath>

namespace LedgePrediction
{
    // A drop deeper than this is a ledge worth stopping at (about a crouch-jump:
    // the player cannot simply step or hop back up). Fixed, not speed-scaled.
    inline constexpr float DropDepth=72.f;
    inline constexpr float EdgeMargin=2.f;
    // Which hypothesis a pass simulates: evidence picks the primary, the second
    // pass (alternate=true) simulates the other one.
    inline bool HoldHypothesis(bool evidence,bool alternate){return evidence!=alternate;}
    // Only supported grounded motion can choose the stay-on-support hypothesis.
    // Airborne movement is never held at ledge height or given ground friction.
    inline bool Hold(bool grounded,bool currentSupport,bool aheadSupport,bool impulse)
    {return grounded && currentSupport && !aheadSupport && !impulse;}
    inline bool Evidence(float speed, float previousSpeed, float directionDot, float age)
    {
        return std::isfinite(speed) && std::isfinite(previousSpeed) && std::isfinite(directionDot)
            && std::isfinite(age) && age >= .06f && age <= .25f && previousSpeed >= 40.f
            && (speed < previousSpeed*.8f || directionDot < .25f);
    }
    inline float LookAhead(float speed, float acceleration, float tick)
    {
        if (!std::isfinite(speed) || !std::isfinite(acceleration) || !std::isfinite(tick)
            || speed <= 0 || acceleration <= 0 || tick <= 0) return 0;
        return std::clamp(speed*speed/(2.f*acceleration)+speed*tick+2.f, 2.f, 96.f);
    }
    inline float Brake(float speed, float acceleration, float tick)
    { return std::max(0.f,speed-acceleration*tick); }
    inline bool Walkable(bool hit, bool solid, float normalZ)
    { return hit && !solid && std::isfinite(normalZ) && normalZ >= .707f; }
}
