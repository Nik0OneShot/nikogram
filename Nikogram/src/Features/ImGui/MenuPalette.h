#pragma once
#include <array>
namespace MenuPalette
{
    enum Role { Background, Panel, Accent, Heading, Text, Muted, Border, Inset, Hover, Pressed, Count };
    inline constexpr int Defaults[Count][3] = {
        {33,27,48},{43,36,60},{192,163,243},{242,204,127},{241,234,250},
        {187,175,202},{72,59,92},{25,21,33},{67,51,90},{110,86,145}
    };
    inline constexpr const char* Names[Count] = {
        "Background", "Panels", "Accent", "Headings", "Active text", "Inactive text",
        "Borders", "Navigation / input", "Hover / selection", "Pressed controls"
    };
    using Palette = std::array<std::array<float,3>,Count>;
    inline Palette Default()
    {
        Palette result{};
        for (int r=0;r<Count;++r) for (int c=0;c<3;++c) result[r][c]=Defaults[r][c]/255.f;
        return result;
    }
    inline Palette Colours = Default();
}
