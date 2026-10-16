#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>

namespace TickbaseCorrectionPolicy
{
    // Commands live in a reusable input ring; an address alone is not identity.
    constexpr int Capacity = 90;
    struct Context
    {
        std::uintptr_t player = 0, channel = 0, input = 0;
        int handle = -1, spawn = -1, server = -1;
        bool operator==(const Context&) const = default;
    };
    struct Entry
    {
        std::uintptr_t slot = 0;
        int sequence = 0, command = 0, shift = 0;
    };
    class Queue
    {
        Context context{};
        bool active = false;
        int lastAck = 0, lastOutgoing = 0, latest = 0;
        std::array<Entry, Capacity> entries{};
        std::size_t count = 0;
    public:
        void Reset() { *this = Queue{}; }
        std::size_t Size() const { return count; }
        bool Observe(const Context& current, int ack, int outgoing, int newest, bool valid)
        {
            if (!valid || ack < 0 || outgoing < 0 || newest < outgoing || ack > newest)
            { Reset(); return false; }
            if (!active || !(current == context) || ack < lastAck || outgoing < lastOutgoing || newest < latest)
                Reset();
            active = true; context = current; lastAck = ack; lastOutgoing = outgoing; latest = newest;
            std::size_t kept = 0;
            for (std::size_t i = 0; i < count; ++i)
                if (entries[i].sequence > ack && entries[i].sequence <= newest
                    && std::int64_t(newest) - entries[i].sequence < Capacity)
                    entries[kept++] = entries[i];
            count = kept;
            return true;
        }
        bool Register(std::uintptr_t slot, int sequence, int command, int shift, int maximumShift)
        {
            if (!active || !slot || command <= 0 || sequence <= lastAck || sequence != latest
                || shift <= 0 || shift > std::min(maximumShift, Capacity - 1)) return false;
            for (std::size_t i = 0; i < count; ++i)
                if (entries[i].sequence == sequence)
                { entries[i] = {slot, sequence, command, shift}; return true; }
            if (count == entries.size()) return false;
            entries[count++] = {slot, sequence, command, shift};
            return true;
        }
        int Correction(std::uintptr_t slot, int sequence, int command) const
        {
            if (!active || sequence <= lastAck || sequence > latest) return 0;
            for (std::size_t i = 0; i < count; ++i)
                if (entries[i].slot == slot && entries[i].sequence == sequence && entries[i].command == command)
                    return entries[i].shift;
            return 0;
        }
    };
    inline Queue queue;
}
