#pragma once
#include <algorithm>
#include <cmath>

namespace TickBudgetPolicy
{
    // Extra commands after the current command cannot exceed the stored reserve.
    inline int Available(int requested, int maximum, int stored)
    { return requested > 1 ? std::max(0, std::min({ requested - 1, maximum, stored })) : 0; }

    inline int Interval(float fireRate, float tick)
    {
        if (!std::isfinite(fireRate) || !std::isfinite(tick) || fireRate <= 0.f || tick <= 0.f)
            return 0;
        const double interval = std::ceil(fireRate / tick);
        return interval <= 2147483647. ? std::max(1, int(interval)) : 0;
    }

    inline int Shots(int commands, int delay, int interval)
    { return interval > 0 && commands >= delay ? 1 + (commands - delay) / interval : 0; }
}
