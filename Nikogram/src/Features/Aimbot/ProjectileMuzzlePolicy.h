#pragma once
#include <optional>
#include <cmath>

namespace ProjectileMuzzlePolicy
{
    // Trial simulations change only this scoped override, never a real convar.
    inline thread_local std::optional<bool> flipOverride;
    struct Trial
    {
        std::optional<bool> previous=flipOverride;
        explicit Trial(bool flip) {flipOverride=flip;}
        ~Trial() {flipOverride=previous;}
        Trial(const Trial&)=delete;
        Trial& operator=(const Trial&)=delete;
    };
    constexpr float NearWallDistance=48.f;
    constexpr float SwitchProbeDistance=128.f;
    struct Probe
    {
        bool valid=false,blocked=false;
        float distance=0.f;
        int steps=0;
        const char* reason="invalid_setup";
    };
    inline bool SwitchImpact(bool hit,bool solid,bool actor,float distance)
    {return !actor && (solid || (hit && std::isfinite(distance) && distance>=0 && distance<SwitchProbeDistance));}
    inline bool CanSwitch(const Probe& current,const Probe& alternate)
    {return current.valid && current.blocked && alternate.valid && !alternate.blocked;}
    inline bool ReturnRight(bool owned,bool flipped,const Probe& right)
    {return owned && flipped && right.valid && !right.blocked;}
    inline bool NearWall(bool hit,bool solid,bool targetFirst,float distance)
    {return !targetFirst && (solid || (hit && std::isfinite(distance) && distance>=0 && distance<NearWallDistance));}
    inline bool RejectImpact(bool delayedBomb,bool hit,bool solid,bool targetFirst,float distance)
    {return !targetFirst && (solid || (!delayedBomb && NearWall(hit,false,false,distance)));}
    inline bool ChargedSwitchSafe(bool charged,float chargeBegin)
    {return !charged || (std::isfinite(chargeBegin) && chargeBegin==0.f);}
    inline bool StickySwitchSafe(bool sticky,float chargeBegin)
    {return ChargedSwitchSafe(sticky,chargeBegin);}
    inline bool Retry(bool enabled,bool autoShoot,bool manual,bool secondary,bool choked,bool blocked,int result)
    {return enabled && autoShoot && !manual && !secondary && !choked && blocked && result==0;}
    inline bool Pending(int acknowledgement,int requiredSequence,float now,float earliest)
    {return requiredSequence>0 && (acknowledgement<requiredSequence || !std::isfinite(now) || !std::isfinite(earliest) || now<earliest);}
}
