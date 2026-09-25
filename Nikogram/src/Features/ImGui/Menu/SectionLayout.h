#pragma once
#include <ImGui/imgui.h>
#include <ImGui/imgui_internal.h>

namespace SectionLayout
{
    inline float ContentHeight(const ImGuiWindow* window)
    {
        // LastItemData may belong to a colour popup or a short same-line item.
        // CursorMaxPos records the full submitted content, even if clipped.
        return window->DC.CursorMaxPos.y-window->Pos.y+window->Scroll.y+ImGui::GetStyle().WindowPadding.y;
    }
}
