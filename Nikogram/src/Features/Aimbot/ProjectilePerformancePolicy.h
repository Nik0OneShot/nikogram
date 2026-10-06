#pragma once
#include <cstdint>
#include <cstddef>

namespace ProjectilePerformancePolicy
{
    struct SideAttempts
    {
        bool tried[2] = {};
        bool Mark(bool flipped)
        {
            const bool fresh = !tried[flipped ? 1 : 0];
            tried[flipped ? 1 : 0] = true;
            return fresh;
        }
    };

    struct FlushWindow
    {
        std::uint64_t last = 0;
        std::size_t pending = 0;
        bool Due(std::uint64_t now, std::size_t bytes)
        {
            pending += bytes;
            if (pending < 65536 && last && now >= last && now - last < 250)
                return false;
            last = now;
            pending = 0;
            return true;
        }
        void Reset() { last = 0; pending = 0; }
    };

    inline bool NeedsPreview(bool aimEnabled, bool secondary, bool attacking,
        bool requested, bool searched, bool canFire, bool equivalentCooldown=false)
    {
        // A failed live search must not immediately repeat as a cosmetic preview.
        // Cooldown searches may also be reused, but only when the caller has
        // established that preview and normal aiming use equivalent inputs.
        return aimEnabled && !secondary && !attacking && requested && !(searched && (canFire || equivalentCooldown));
    }
}
