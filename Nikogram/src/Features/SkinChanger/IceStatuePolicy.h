#pragma once
#include <array>
#include <utility>

namespace SkinIce
{
    // Cosmetic-only copy of TF2's authored ice_player shader inputs. Do not
    // copy its live-player invis/BurnLevel/YellowLevel proxies onto corpses.
    constexpr std::array<std::pair<const char*,const char*>,6> Textures={{{"$basetexture","models/player/shared/ice_player"},
        {"$bumpmap","models/weapons/c_items/c_xms_cold_shoulder_normal"},
        {"$phongwarptexture","models/player/shared/ice_player_warp"},
        {"$lightwarptexture","models/player/shared/ice_player_lightwarp"},
        {"$envmap","Effects/cubemapper"},{"$detail","effects/tiledfire/fireLayeredSlowTiled512"}}};
    constexpr std::array<std::pair<const char*,const char*>,13> Parameters={{{"$model","1"},
        {"$phong","1"},{"$phongexponent","200"},{"$phongboost","100"},
        {"$phongfresnelranges","[.25 1 4]"},{"$basemapalphaphongmask","1"},
        {"$envmaptint","[.2 .2 .2]"},{"$rimlight","1"},{"$rimlightexponent","50"},{"$rimlightboost","15"},
        {"$detailscale","5"},{"$detailblendfactor",".01"},{"$detailblendmode","6"}}};
}
