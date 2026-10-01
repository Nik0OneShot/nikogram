#pragma once
#include <cmath>
#include <cstdint>
#include <array>
#include <algorithm>
namespace AmmoConservationPolicy
{
    struct ChargeOwnership
    {
        int owner=0,weapon=0;
        bool automatedHold=false,manualCharge=false;
        void Observe(int who,int held,float begin,bool manual)
        {
            if (owner!=who || weapon!=held) {*this={};owner=who;weapon=held;}
            if (!std::isfinite(begin) || begin<0) {automatedHold=false;manualCharge=true;return;}
            if (begin==0 && !manual) manualCharge=false;
            if (manual) {manualCharge=true;automatedHold=false;}
        }
        bool Owned(float begin) const
        {return std::isfinite(begin) && begin>=0 && !manualCharge && (begin==0 || automatedHold);}
        void Record(bool outgoingHold,bool automatic)
        {automatedHold=automatic && outgoingHold && !manualCharge;}
    };
    inline ChargeOwnership chargeOwnership;
    inline bool StickyHoldSafe(float begin,float now,float maximum,float delay)
    {
        if (!std::isfinite(begin) || !std::isfinite(now) || !std::isfinite(maximum) || !std::isfinite(delay)
            || begin<0 || delay<=0 || delay>.15f || maximum<.5f || maximum>10.f) return false;
        if (begin==0) return true;
        const float elapsed=now-begin;
        // Includes the whole 100-ms action window plus a scheduling margin.
        return elapsed>=0 && elapsed+delay+.25f<maximum;
    }
    // A projectile center must enter the target's interior, not merely graze
    // an expanded collision hull. All inputs are command-local predictions.
    inline float CoreFraction(const std::array<float,3>& from, const std::array<float,3>& to,
        const std::array<float,3>& mins, const std::array<float,3>& maxs, float inset)
    {
        if (!std::isfinite(inset) || inset<0) return -1.f;
        float enter=0,exit=1;
        for (int axis=0;axis<3;++axis)
        {
            if (!std::isfinite(from[axis]) || !std::isfinite(to[axis]) || !std::isfinite(mins[axis]) || !std::isfinite(maxs[axis])) return -1.f;
            const float low=mins[axis]+inset,high=maxs[axis]-inset,d=to[axis]-from[axis];
            if (!std::isfinite(d) || low>=high) return -1.f;
            if (std::abs(d)<.00001f) {if (from[axis]<low || from[axis]>high) return -1.f;continue;}
            const float a=(low-from[axis])/d,b=(high-from[axis])/d;
            enter=std::max(enter,std::min(a,b));exit=std::min(exit,std::max(a,b));
            if (enter>exit) return -1.f;
        }
        return enter;
    }
    inline bool FreshPill(float birthAge, float fuse, float creation, float lastFire)
    {
        // Client birth is NOT a fuse clock. This only restricts direct-contact
        // evidence to recently observed shots correlated with our firing time.
        return std::isfinite(birthAge) && std::isfinite(fuse) && std::isfinite(creation) && std::isfinite(lastFire)
            && birthAge>=0 && birthAge<=.9f && fuse>=1.2f && fuse<=3.f
            && creation>0 && lastFire>0 && std::abs(creation-lastFire)<=.25f;
    }
    inline bool ImpactWindow(float delay, float flight, float observationAge)
    {
        return std::isfinite(delay) && std::isfinite(flight) && std::isfinite(observationAge)
            && delay > 0 && flight >= 0 && flight <= .1f && observationAge >= 0 && observationAge <= .1f
            && delay + flight + observationAge <= .15f;
    }
    inline bool Covered(float estimate, int health, float margin = 10.f)
    {
        // The margin is additional to the discounted damage estimate.
        return health > 0 && std::isfinite(estimate) && std::isfinite(margin) && margin >= 2.f
            && estimate >= float(health) * 1.25f + margin;
    }
    struct Window
    {
        bool started = false;
        std::uint64_t start = 0, last = 0;
        bool Allow(std::uint64_t now)
        {
            if (started && now < last) { started=false; return false; }
            last=now;
            if (!started || now-start>=1100) { started=true; start=now; }
            return now-start<100;
        }
    };
}
