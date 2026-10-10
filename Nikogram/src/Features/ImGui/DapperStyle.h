#pragma once
#include "MenuMode.h"
#include "MenuPalette.h"
#include <algorithm>
#include <cmath>

// Presentation only. Stable IDs append to the existing menu choices; no aim,
// rendering-feature or condition values are copied when a style is selected.
namespace DapperStyle
{
    inline constexpr const char* Names[]{"Dapper Desktop", "Bank of Dapper", "Dapper Scrapbook"};
    inline constexpr int Defaults[3][MenuPalette::Count][3] = {
        {{35,44,40},{46,57,51},{53,114,88},{235,242,235},{237,243,237},{184,198,185},{87,108,93},{29,37,32},{62,83,67},{69,127,93}},
        {{23,41,32},{31,55,41},{66,103,69},{235,232,207},{230,235,215},{177,195,158},{80,109,73},{20,37,27},{51,78,50},{72,114,68}},
        {{53,42,31},{69,54,40},{155,96,74},{245,230,200},{242,226,197},{207,185,150},{124,101,70},{44,34,25},{92,68,47},{166,105,78}}
    };
    inline MenuPalette::Palette Default(int index)
    {
        MenuPalette::Palette result{};
        index=std::clamp(index,0,2);
        for(int r=0;r<MenuPalette::Count;++r) for(int c=0;c<3;++c) result[r][c]=Defaults[index][r][c]/255.f;
        return result;
    }
    struct Preferences
    {
        MenuPalette::Palette colours{};
        int photo=1, opacity=100;
        bool reactive=false;
    };
    inline Preferences Themes[3]{{Default(0),1,100,false},{Default(1),2,100,false},{Default(2),3,100,false}};
    inline int Index(int style) { return std::clamp(style-MenuMode::DapperDesktop,0,2); }
    inline Preferences& Get(int style) { return Themes[Index(style)]; }
    inline const char* Name(int style) { return Names[Index(style)]; }
    inline int Photo(int style, bool ready=true, bool watched=false)
    {
        const auto& p=Get(style);
        return p.reactive ? (watched?2:ready?3:1) : std::clamp(p.photo,1,3);
    }
    inline float Opacity(int style) { return std::clamp(Get(style).opacity,20,100)/100.f; }
    inline void Normalize(Preferences& p)
    {
        p.photo=std::clamp(p.photo,1,3);p.opacity=std::clamp(p.opacity,20,100);
        for(auto& row:p.colours)for(auto& c:row)c=std::isfinite(c)?std::clamp(c,0.f,1.f):0.f;
    }
}
