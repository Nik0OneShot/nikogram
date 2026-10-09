#pragma once
#include <cmath>

// A command-number/sequence lease, not a global "AA is enabled" test. Shooting,
// compatibility corrections and inactive class presets must never be rewritten
// just because an older command used anti-aim.
namespace AntiAimPacketPolicy
{
    struct Ticket
    {
        int sequence = -1, command = -1;
        bool controlled = false, selectedSend = false;
        float fakePitch = 0.f, fakeYaw = 0.f;

        bool Matches(int slot, int number) const
        {
            return sequence >= 0 && slot == sequence && number == command;
        }
        bool NeedsRepair(int slot, int number, float pitch, float yaw) const
        {
            if (!controlled || !Matches(slot, number)
                || !std::isfinite(fakePitch) || !std::isfinite(fakeYaw)) return false;
            return !selectedSend || !std::isfinite(pitch) || !std::isfinite(yaw)
                || std::abs(pitch - fakePitch) > .001f
                || std::abs(std::remainder(yaw - fakeYaw, 360.f)) > .001f;
        }
    };
}
