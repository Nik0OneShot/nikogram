#pragma once
#include <cstdint>
#include <cmath>
namespace TargetSwitchPolicy
{
    // Handoff delay only. A changing candidate must not restart the deadline.
    struct State
    {
        std::uint32_t current = 0;
        double since = 0, delay = 0, last = 0;
        bool waiting = false;
        void Reset() { *this = {}; }
        bool Update(std::uint32_t candidate, double now, double sampledDelay)
        {
            if (!std::isfinite(now) || !std::isfinite(sampledDelay) || sampledDelay < 0)
            { Reset(); return false; }
            if (now < last) Reset();
            last = now;
            if (candidate && (!current || candidate == current))
            { current = candidate; waiting = false; return true; }
            if (!current) return false;
            if (!waiting) { waiting = true; since = now; delay = sampledDelay; }
            if (!candidate || now - since + 1e-9 < delay) return false;
            current = candidate; waiting = false; return true;
        }
    };
}
