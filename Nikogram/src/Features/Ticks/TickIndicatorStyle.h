#pragma once
#include <algorithm>
namespace TickIndicatorStyle
{
    inline float Charge(int ticks, int maximum, bool speedhack)
    {
        return !speedhack && maximum > 0 ? std::clamp(float(ticks) / maximum, 0.f, 1.f) : 0.f;
    }
    inline float LabelBrightness(float charge) { return .6f + .4f * std::clamp(charge, 0.f, 1.f); }
    inline bool Full(int ticks, int maximum, bool speedhack) { return !speedhack && maximum > 0 && ticks >= maximum; }
    // This HUD reports the reserve, not whether a doubletap bind is currently enabled.
    inline const char* Status(int ticks, int maximum, bool speedhack, bool warp)
    {
        if (speedhack) return "SPEEDHACK";
        if (warp) return "WARPING";
        if (Full(ticks, maximum, false)) return "FULL";
        return maximum > 0 && ticks > 0 ? "CHARGING" : "EMPTY";
    }
}
