#pragma once

// Engine-independent selection rules. Existing config bits retain their values.
namespace ProjectileAimPolicy
{
    constexpr int Auto=1, Head=2, Body=4, Feet=8, TrueFeet=64;
    constexpr int SplashOff=0, SplashInclude=1;
    inline int Hitboxes(int configured,bool bodyWeapon,bool player)
    {
        if(!player) return configured;
        if(configured&TrueFeet) return configured|Feet;
        if(bodyWeapon && (configured&(Head|Body|Feet)))
            return (configured|Body)&~Feet;
        return configured;
    }
    inline int Priority(int hitbox,int original,int configured,bool bodyWeapon,bool player)
    {
        if(!player) return original;
        if(configured&TrueFeet)
            return hitbox==2?0:hitbox==1?1:2;
        if(bodyWeapon)
            return hitbox==1?0:hitbox==0?1:-1;
        return original;
    }
    inline int Splash(int configured,bool directHit,bool allowDirectHitSplash)
    {return directHit?(allowDirectHitSplash?SplashInclude:SplashOff):configured;}
}
