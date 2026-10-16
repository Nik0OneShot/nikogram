#pragma once
#include <cmath>
#include <cstdint>
#include <cstddef>
namespace IndicatorOverlayPolicy
{
    inline constexpr size_t MaxCommands=16384,MaxPoints=65536,MaxText=2048;
    inline bool Fresh(double now,double captured,int width,int height,int currentWidth,int currentHeight,
        uint64_t generation,uint64_t currentGeneration,uint64_t visibility,uint64_t currentVisibility)
    {
        return width>0&&height>0&&width==currentWidth&&height==currentHeight
            &&generation==currentGeneration&&visibility==currentVisibility
            &&std::isfinite(now)&&std::isfinite(captured)&&now>=captured&&now-captured<=.1;
    }
}
