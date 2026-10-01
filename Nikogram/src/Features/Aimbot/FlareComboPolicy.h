#pragma once
#include <cmath>

namespace FlareComboPolicy
{
    // Weapon/deploy cooldown is not a user disabling the combo.
    inline bool Enabled(bool option,bool aim,bool autoShoot,bool pyro)
    { return option && aim && autoShoot && pyro; }
    inline bool ShotMatches(int validatedCommand,int command,int validatedTarget,int target)
    { return command>0 && validatedCommand==command && target>=0 && validatedTarget==target; }
    struct State
    {
        int target=-1,owner=-1;
        double started=0;
        bool active=false,switched=false,returning=false,waiting=false;
        void Reset() {*this={};}
        bool Arm(int enemy,int local,double now)
        {
            if (active || enemy<0 || local<0 || !std::isfinite(now)) return false;
            target=enemy;owner=local;started=now;active=true;switched=false;return true;
        }
        bool IsValid(int enemy,int local,double now,bool alive,bool enabled) const
        {
            return active && enabled && alive && enemy==target && local==owner && std::isfinite(now)
                && now>=started && now-started<=((waiting || returning)?10.:1.5);
        }
        bool Valid(int enemy,int local,double now,bool alive,bool enabled)
        {
            if (!IsValid(enemy,local,now,alive,enabled)) {Reset();return false;}
            return true;
        }
        bool Shot(double now)
        {
            if (!active || !switched || returning || waiting || !std::isfinite(now) || now<started) return false;
            switched=false;returning=true;started=now;return true;
        }
        bool AbortToReturn(double now)
        {
            if (!active || !switched || !std::isfinite(now) || now<started) return false;
            switched=false;returning=true;waiting=false;started=now;return true;
        }
        bool Returned(double now)
        {
            if (!active || !returning || !std::isfinite(now) || now<started) return false;
            returning=false;waiting=true;started=now;return true;
        }
        bool Retry(double now)
        {
            if (!active || !waiting || !std::isfinite(now) || now<started) return false;
            waiting=false;switched=false;started=now;return true;
        }
    };
    inline bool Ready(double now,double nextPrimary,double nextOwnerAttack,double earliest)
    {
        return std::isfinite(now) && std::isfinite(nextPrimary) && std::isfinite(nextOwnerAttack)
            && std::isfinite(earliest) && now>=nextPrimary && now>=nextOwnerAttack && now>=earliest;
    }
}
