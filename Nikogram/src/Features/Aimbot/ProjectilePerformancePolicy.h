#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <chrono>

namespace ProjectilePerformancePolicy
{
    enum class SearchWork : std::size_t { Prediction, Sampling, Selection, Validation, Geometry, Count };

    // Cooperative limits, not a frame-time guarantee. An engine operation or
    // full candidate validation already in progress must finish safely.
    struct SearchBudget
    {
        using Clock = std::chrono::steady_clock;
        static constexpr std::int64_t TotalUs = 4000;
        static constexpr std::array<std::int64_t, 5> DeadlineUs{2000, 3000, 3500, TotalUs, 3000};
        static constexpr std::array<unsigned, 5> Limits{512, 512, 4096, 64, 2048};
        std::array<unsigned, 5> used{};
        Clock::time_point start{};
        int command = 0;
        bool initialized = false, limited = false;

        void Begin(int value, Clock::time_point now)
        {
            if (initialized && command == value) return;
            initialized = true; command = value; start = now; used = {}; limited = false;
        }
        bool Check(SearchWork work, std::int64_t elapsed, bool consume)
        {
            const auto index = static_cast<std::size_t>(work);
            if (elapsed >= DeadlineUs[index] || used[index] >= Limits[index])
            { limited = true; return false; }
            if (consume) ++used[index];
            return true;
        }
        unsigned Remaining(SearchWork work) const
        {
            const auto index = static_cast<std::size_t>(work);
            return used[index] < Limits[index] ? Limits[index] - used[index] : 0;
        }
        bool Check(SearchWork work, bool consume = false)
        {
            return Check(work, std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count(), consume);
        }
    };

    inline thread_local SearchBudget* currentSearchBudget = nullptr;
    struct SearchScope
    {
        SearchBudget* previous = currentSearchBudget;
        SearchScope(SearchBudget& budget, int command, bool enabled)
        {
            if (enabled) budget.Begin(command, SearchBudget::Clock::now());
            currentSearchBudget = enabled ? &budget : nullptr;
        }
        ~SearchScope() { currentSearchBudget = previous; }
        SearchScope(const SearchScope&) = delete;
        SearchScope& operator=(const SearchScope&) = delete;
    };
    inline bool SearchAllowed(SearchWork work = SearchWork::Validation)
    { return !currentSearchBudget || currentSearchBudget->Check(work); }
    inline bool SearchStep(SearchWork work)
    { return !currentSearchBudget || currentSearchBudget->Check(work, true); }
    inline bool SearchGeometryStep() { return SearchStep(SearchWork::Geometry); }

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
