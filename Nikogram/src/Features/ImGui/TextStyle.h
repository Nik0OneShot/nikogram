#pragma once
#include "MenuMode.h"
#include <algorithm>
#include <cmath>
namespace Workspace
{
    inline float Accent[3] = { .85f, .75f, .20f };
    inline bool BorderColourOverride = false;
    inline float BorderColour[3] = { .85f, .75f, .20f };
    inline bool InterfaceBorderSelected = false, InterfaceBorderCustom = false;
    inline float InterfaceBorderColour[3] = { .85f, .75f, .20f };
    inline float BorderChannel(int i)
    {
        if(MenuMode::DrawingMoonlit){constexpr float c[]={72/255.f,59/255.f,92/255.f};return c[i];}
        const float value = InterfaceBorderSelected
            ? (InterfaceBorderCustom ? InterfaceBorderColour[i] : Accent[i])
            : (BorderColourOverride ? BorderColour[i] : Accent[i]);
        return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f;
    }
    inline bool TextColourOverride = false;
    inline bool TitleTextOverride = false;
    inline float TitleTextColour[3] = { .88f, .80f, .36f };
    inline float TitleTextChannel(int i)
    {
        if(MenuMode::DrawingMoonlit){constexpr float c[]={242/255.f,204/255.f,127/255.f};return c[i];}
        // A restrained 20% blend toward white, following accent dynamically.
        const float value = TitleTextOverride ? TitleTextColour[i] : Accent[i] + (1.f - Accent[i]) * .20f;
        return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f;
    }
    inline float TextColour[3] = { 1.f, 1.f, 1.f };
    inline bool InactiveTextOverride = false;
    inline float InactiveTextColour[3] = { .55f, .48f, .13f };
    inline float InactiveTextChannel(int i)
    {
        if(MenuMode::DrawingMoonlit){constexpr float c[]={187/255.f,175/255.f,202/255.f};return c[i];}
        const float value = InactiveTextOverride ? InactiveTextColour[i] : Accent[i] * .65f;
        return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f;
    }
    inline const void* TextIconFont = nullptr;
    // Scoped exception for explicit semantic colours (active binds, ESP names).
    inline thread_local bool PreserveTextColour = false;
    struct ScopedTextColour
    {
        bool previous = PreserveTextColour;
        explicit ScopedTextColour(bool preserve = true) { PreserveTextColour = previous || preserve; }
        ~ScopedTextColour() { PreserveTextColour = previous; }
        ScopedTextColour(const ScopedTextColour&) = delete;
        ScopedTextColour& operator=(const ScopedTextColour&) = delete;
    };
    inline float TextChannel(int i)
    {
        if(MenuMode::DrawingMoonlit){constexpr float c[]={241/255.f,234/255.f,250/255.f};return c[i];}
        const float value = TextColourOverride ? TextColour[i] : Accent[i];
        return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f;
    }
}
