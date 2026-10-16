#pragma once

// User-supplied class/held-weapon eligibility. All enabled presets use the
// existing real Edge/fake Forward solver, with no fixed class-specific yaw.
namespace LegitAAPolicy
{
    // Native TF2 class order, not the menu's presentation order.
    enum Class { UnknownClass=0, Scout=1, Sniper=2, Soldier=3, Demoman=4,
        Medic=5, Heavy=6, Pyro=7, Spy=8, Engineer=9 };
    enum class Weapon { Unavailable, Other, Medigun, SniperRifle, SMG, Melee,
        Revolver, Sapper, Knife, DisguiseKit };
    struct Preset { bool enabled=false; };
    inline Preset Select(int playerClass,Weapon held)
    {
        if(held==Weapon::Unavailable)return {};
        switch(playerClass)
        {
        case Scout: case Soldier: case Pyro: case Demoman: case Heavy: case Engineer:
            return {true};
        case Medic:
            return held==Weapon::Medigun?Preset{}:Preset{true};
        case Sniper:
            if(held==Weapon::SniperRifle)return {true}; // Scoped or unscoped.
            if(held==Weapon::Melee)return {true};
            return {}; // SMG and unlisted weapons have no approved preset.
        case Spy:
            if(held==Weapon::Knife||held==Weapon::DisguiseKit)return {true};
            return {}; // Revolver, sapper, and unlisted weapons stay off.
        default: return {};
        }
    }
}
