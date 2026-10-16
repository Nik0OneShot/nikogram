#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace ServerEstimate
{
    struct CommandTime { int lerpTicks=0, targetTick=0; double seconds=0.; };
    // Mirror the server's requested rewind tick using the client's best-known
    // applied interpolation. This is not acknowledgement of server interpolation.
    inline std::optional<CommandTime> RequestedTime(int commandTick,float interpolation,float tick)
    {
        if(commandTick<0 || !std::isfinite(interpolation) || interpolation<0.f
            || !std::isfinite(tick) || tick<=0.f) return std::nullopt;
        const float rounded=0.5f+interpolation/tick;
        if(!std::isfinite(rounded) || double(rounded)>std::numeric_limits<int>::max()) return std::nullopt;
        const int lerpTicks=int(rounded);
        if(commandTick<lerpTicks) return std::nullopt;
        const int targetTick=commandTick-lerpTicks;
        return CommandTime{lerpTicks,targetTick,double(targetTick)*tick};
    }
    // Fill only a bounded gap between the newest sample and the command's
    // requested rewind time. Neither packet arrival nor stored warp ticks are
    // added to the pose. The client reference is used only for source freshness.
    inline float SampleScale(double reference,float latest,float previous,int maxMilliseconds,double requested)
    {
        if(!std::isfinite(reference)||!std::isfinite(latest)||!std::isfinite(previous)||!std::isfinite(requested))return 0.f;
        const double sampleTime=double(latest)-previous,age=reference-latest,horizon=requested-latest;
        if(sampleTime<.001 || sampleTime>.1 || age<=0. || age>.2 || maxMilliseconds<=0
            || horizon<=0. || horizon>.2 || horizon>maxMilliseconds/1000.+1e-6) return 0.f;
        // Reject an over-budget request instead of inventing a shorter pose for
        // an unchanged command tick. The tolerance covers floating-point time.
        return float(horizon/sampleTime);
    }
}
