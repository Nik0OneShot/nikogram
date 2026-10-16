#pragma once
#include "TextStyle.h"
#include "../../SDK/Definitions/Types.h"

namespace Workspace
{
    // Surface HUD colours must not depend on Present having run since the last
    // bind update, or on an ImGui window's temporary DrawingMoonlit scope.
    inline Color_t HudTextColour(bool active = true)
    {
        const auto channel = [active](int i)
        {
            const float value = active
                ? (TextColourOverride ? TextColour[i] : Accent[i])
                : (InactiveTextOverride ? InactiveTextColour[i] : Accent[i] * .65f);
            return static_cast<unsigned char>((std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f) * 255.f);
        };
        return { channel(0), channel(1), channel(2), 255 };
    }
}
