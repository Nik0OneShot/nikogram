#pragma once
#include <array>
#include <algorithm>
#include <cmath>

// Engine-independent safety rules shared by the runtime and regression tests.
namespace AimbotAuditPolicy
{
    inline bool Bounds(const std::array<float,3>& low,const std::array<float,3>& high)
    {
        for(int axis=0;axis<3;++axis)
            if(!std::isfinite(low[axis]) || !std::isfinite(high[axis]) || low[axis]>=high[axis]) return false;
        return true;
    }
    inline bool ClearAdvance(float fraction,bool startSolid,bool allSolid)
    { return std::isfinite(fraction) && fraction==1.f && !startSolid && !allSolid; }
    inline float RedirectLifetime(bool timed,float verifiedRemaining,float latency,float maximum)
    {
        if(!std::isfinite(latency) || latency<0 || !std::isfinite(maximum) || maximum<=0) return 0;
        if(!timed) return maximum;
        // Negative means unknown. Client creation time must not become a fuse clock.
        if(!std::isfinite(verifiedRemaining) || verifiedRemaining<=latency) return 0;
        return std::min(maximum,verifiedRemaining-latency);
    }
    struct DamageHistory
    {
        int handle=0,type=-1;
        float dps=0,expires=0;
        void Record(int victim,int resistance,float damageRate,float now)
        {
            if(!victim || resistance<0 || resistance>=3 || !std::isfinite(damageRate) || damageRate<=0 || !std::isfinite(now)) return;
            *this={victim,resistance,damageRate,now+1.f};
        }
        bool Applies(int victim,float now) const
        {return handle==victim && victim!=0 && std::isfinite(now) && now>=expires-1.f && now<expires && dps>0;}
    };
}
