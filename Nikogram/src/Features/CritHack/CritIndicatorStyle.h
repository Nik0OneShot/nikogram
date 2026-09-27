#pragma once
#include <algorithm>
#include "../Ticks/TickIndicatorStyle.h"

namespace CritIndicatorStyle
{
    inline float Charge(int stored, int capacity, bool enabled)
    {
        return enabled && capacity > 0 ? std::clamp(float(stored) / capacity, 0.f, 1.f) : 0.f;
    }
    inline const char* ReserveStatus(int stored) { return stored > 0 ? "READY" : "EMPTY"; }
    inline float LabelBrightness(float charge) { return TickIndicatorStyle::LabelBrightness(charge); }
    inline bool Full(int stored, int capacity, bool enabled) { return TickIndicatorStyle::Full(stored, capacity, !enabled); }
    inline bool Streaming(bool enabled, bool boosted, float critEnd, float now)
    {
        return enabled && !boosted && critEnd > now;
    }
    inline float BarCharge(float reserve, bool streaming) { return streaming ? 1.f : std::clamp(reserve, 0.f, 1.f); }
}
