#pragma once
#include <algorithm>
#include <cmath>
namespace AngleDisplayPolicy
{
    // Display-only shortest-arc smoothing. Never changes command angles.
    inline float SmoothAngle(float current,float target,float dt)
    {
        const float delta=std::remainder(target-current,360.f);
        return current+delta*(1.f-std::exp(-14.f*std::clamp(dt,0.f,.1f)));
    }
}
