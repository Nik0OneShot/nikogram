#pragma once
#include <cmath>
#include <cstdint>
namespace AmmoConservationPolicy
{
    inline bool Covered(float estimate, int health)
    {
        // The margin is additional to the discounted damage estimate.
        return health > 0 && std::isfinite(estimate) && estimate >= float(health) * 1.25f + 10.f;
    }
    struct Window
    {
        bool started = false;
        std::uint64_t start = 0, last = 0;
        bool Allow(std::uint64_t now)
        {
            if (started && now < last) { started=false; return false; }
            last=now;
            if (!started || now-start>=1100) { started=true; start=now; }
            return now-start<100;
        }
    };
}
