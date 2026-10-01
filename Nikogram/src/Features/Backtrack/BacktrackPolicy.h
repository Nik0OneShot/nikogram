#pragma once
#include <algorithm>
#include <cmath>

namespace BacktrackPolicy
{
    constexpr double ServerTolerance=.2;
    inline double SafeWindow(double tick)
    {return std::isfinite(tick) && tick>0?std::max(0.,ServerTolerance-tick):0.;}
    inline double Window(double requested,double tick)
    {return std::isfinite(requested)?std::clamp(requested,0.,SafeWindow(tick)):0.;}
    inline bool Usable(double simulation,bool invalid,double serverTime,double maxUnlag,double tick,double timeMod)
    {
        if(invalid || !std::isfinite(simulation) || !std::isfinite(serverTime) || !std::isfinite(maxUnlag)
            || !std::isfinite(tick) || !std::isfinite(timeMod) || simulation<0 || maxUnlag<0 || tick<=0) return false;
        const double age=serverTime-simulation;
        // Melee's deliberately predicted records may be ahead by its swing delay.
        return age>=-std::max(tick,-timeMod)-1e-6 && age<=maxUnlag+tick;
    }
    inline bool Within(double delta,double window)
    {return std::isfinite(delta) && std::isfinite(window) && window>0 && std::abs(delta)<=window;}
    inline bool NewSample(double current,double previous)
    {return std::isfinite(current) && current>=0 && (!std::isfinite(previous) || current>previous);}
}
