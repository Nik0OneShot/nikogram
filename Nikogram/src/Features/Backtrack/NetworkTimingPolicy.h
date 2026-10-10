#pragma once
#include <algorithm>
#include <cmath>

namespace NetworkTimingPolicy
{
    inline double Nonnegative(double value, double maximum=1.)
    { return std::isfinite(value)?std::clamp(value,0.,maximum):0.; }

    // INetChannelInfo describes GetLatency as RTT. Preserve the established
    // engine-channel estimate; never halve it based on scoreboard ping.
    inline double RewindCorrection(double channelLatency, double interpolation, double maxUnlag)
    { return std::clamp(Nonnegative(channelLatency)+Nonnegative(interpolation),0.,Nonnegative(maxUnlag)); }

    // Simulation may already have consumed missing remote ticks. Align with
    // the same receipt-clock estimate used by record validation, but cap it at
    // the established total-channel budget minus those consumed ticks. Channel
    // flow measurements are engine estimates, not assumed one-way delays.
    inline double ProjectileLead(double clock, double simulated, double original,
        double channelLatency, double choke, double fallbackRTT, double tick)
    {
        const double advanced=std::isfinite(simulated)&&std::isfinite(original)
            ?std::max(0.,simulated-original):0.;
        const double extra=Nonnegative(choke,.35);
        const double remaining=std::clamp(Nonnegative(fallbackRTT)+extra-advanced,0.,1.);
        const double age=clock-original;
        if(std::isfinite(clock)&&std::isfinite(original)&&original>0
            &&std::isfinite(simulated)&&std::isfinite(tick)&&tick>0&&age>=-2*tick&&age<=1.)
            return std::min(remaining,std::clamp(clock+Nonnegative(channelLatency)+extra-simulated,0.,1.));
        return remaining;
    }
}
