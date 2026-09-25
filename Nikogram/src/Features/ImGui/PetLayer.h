#pragma once
#include <ImGui/imgui.h>
#include <ImGui/imgui_internal.h>

namespace NikoPet
{
    inline void DrawLayer(ImTextureID texture, const ImVec2& min, const ImVec2& max,
        const ImVec2& uv0, const ImVec2& uv1)
    {
        using namespace ImGui;
        SetNextWindowPos({0,0});
        SetNextWindowSize(GetIO().DisplaySize);
        Begin("##NikogramPetLayer", nullptr, ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoBringToFrontOnFocus);
        auto* layer=GetCurrentWindow();
        auto* draw=GetWindowDrawList();
        draw->PushClipRect({0,0},GetIO().DisplaySize,false);
        draw->AddImage(texture,min,max,uv0,uv1);
        draw->PopClipRect();
        End();
        // Back-to-front: taskbar, pet, then every regular window/popup.
        // Reassert each frame because clicking the taskbar can focus it.
        BringWindowToDisplayBack(layer);
        if(auto* bar=FindWindowByName("##NikogramTaskbar")) BringWindowToDisplayBack(bar);
    }
}
