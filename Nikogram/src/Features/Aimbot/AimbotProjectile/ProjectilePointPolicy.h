#pragma once
#include <algorithm>
#include <cmath>

namespace ProjectilePointPolicy
{
    // Detonator world impact is self-only; Scorch Shot has no remote air burst.
    inline bool Air(bool enabled,bool reflected,bool sticky,bool detonator)
    {return enabled&&!reflected&&(sticky||detonator);}
    inline bool Surface(bool detonator){return !detonator;}
    inline bool AirReady(float flight,float minimum)
    {return std::isfinite(flight)&&std::isfinite(minimum)&&flight>=0.f&&minimum>=0.f&&flight>=minimum;}
    // Hard guard equals the existing per-command sampling ceiling. Normal UI
    // counts (0..10) are unchanged; every emitted point also consumes its budget.
    constexpr int MaxAirSamples=512;
    template<class Step,class Emit,class Random>
    inline void AirSamples(int configured,bool& centerEmitted,Step&& step,Emit&& emit,Random&& random)
    {
        const int count=std::clamp(configured,0,MaxAirSamples);
        if(!count)
        {if(step())emit(std::clamp(random(),0.f,1.f));return;}
        for(int i=0;i<count;++i)
        {
            if(!i&&centerEmitted)continue;
            if(!step())return;
            emit(float(i)/count);
            if(!i)centerEmitted=true;
        }
    }
}
