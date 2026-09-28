#pragma once
#include <cmath>

namespace AmmoLifetimePolicy
{
    inline constexpr float ClockTolerance = .05f;
    inline const char* ResetReason(int local, int previousLocal, int /*command*/, int /*previousCommand*/, float now, float previousTime)
    {
        if (local != previousLocal) return "local_identity_changed";
        // Final commands are rewritten by CritHack: neither repetition nor a
        // decrease establishes a world reset. Identity and time are authoritative.
        if (!std::isfinite(now)) return "invalid_time";
        if (previousTime - now > ClockTolerance) return "time_rewind";
        return nullptr;
    }
    inline bool ResetNeeded(int local, int previousLocal, int command, int previousCommand, float now, float previousTime)
    {
        return ResetReason(local, previousLocal, command, previousCommand, now, previousTime) != nullptr;
    }
    inline bool Missing(bool seen, bool complete, int& misses)
    {
        if (seen) misses = 0;
        else if (complete) ++misses;
        return misses >= 2;
    }
    struct RequestObservation
    {
        bool requested = false;
        int originalCommand = -1, requestCommand = -1, sources = 0;
    };
    struct CommandRequests
    {
        RequestObservation current{};
        void Begin(int command) { current = {}; current.originalCommand = command; }
        void Record(int command, int sources)
        {
            current.requested = true;
            current.requestCommand = command;
            current.sources |= sources;
        }
        RequestObservation Take()
        {
            const auto result = current;
            current = {};
            return result;
        }
    };
    struct RequestGate
    {
        bool held = false;
        unsigned long long last = 0;
        int previousContext = 0;
        bool Due(bool request, unsigned long long now, int context = 0)
        {
            const bool due = request && (!held || context != previousContext || now < last || now - last >= 250);
            held = request;
            previousContext = context;
            if (due) last = now;
            return due;
        }
    };
}
