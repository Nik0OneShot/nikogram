#pragma once
#include <ImGui/imgui.h>
#include <ImGui/imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <charconv>
#include <cstring>
#include <cstdint>
#include <unordered_map>

namespace StableControls {
// Keep uncommitted text in ImGui and a per-widget draft. Range enforcement must
// not rewrite an active editor when a user deletes through an incomplete value.
inline bool BoundedInt(const char* label,int& value,int minimum,int maximum){
 struct Draft{char text[64]{};int frame=-2;bool editing=false;};
 static std::unordered_map<ImGuiID,Draft> drafts;
 const auto id=ImGui::GetID(label);auto& draft=drafts[id];const int frame=ImGui::GetFrameCount();
 auto format=[&]{auto result=std::to_chars(draft.text,draft.text+sizeof(draft.text)-1,value);*result.ptr=0;};
 if(!draft.editing||draft.frame!=frame-1)format();
 ImGui::BeginGroup();ImGui::PushID(label);
 const float button=ImGui::GetFrameHeight();
 ImGui::SetNextItemWidth(std::max(1.f,ImGui::CalcItemWidth()-(button+ImGui::GetStyle().ItemInnerSpacing.x)*2));
 const bool enter=ImGui::InputText("##value",draft.text,sizeof(draft.text),ImGuiInputTextFlags_CharsDecimal|ImGuiInputTextFlags_AutoSelectAll|ImGuiInputTextFlags_EnterReturnsTrue);
 const bool active=ImGui::IsItemActive(),cancel=ImGui::IsKeyPressed(ImGuiKey_Escape);
 const bool commit=enter||ImGui::IsItemDeactivatedAfterEdit();
 int delta=0;ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat,true);
 ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);if(ImGui::Button("-",{button,button}))delta=ImGui::GetIO().KeyCtrl?-100:-1;
 ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);if(ImGui::Button("+",{button,button}))delta=ImGui::GetIO().KeyCtrl?100:1;
 ImGui::PopItemFlag();ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);ImGui::TextUnformatted(label);
 ImGui::PopID();ImGui::EndGroup();draft.editing=active;draft.frame=frame;
 if((!commit&&!delta)||cancel)return false;
 const char*begin=draft.text;const char*end=begin+std::strlen(begin);if(begin!=end&&*begin=='+')++begin;
 int parsed=0;auto result=std::from_chars(begin,end,parsed);
 if(begin==end||(*draft.text=='+'&&*begin=='-')||result.ec!=std::errc{}||result.ptr!=end){if(!active)format();return false;}
 const int next=int(std::clamp(int64_t(parsed)+delta,int64_t(minimum),int64_t(maximum)));
 const bool changed=next!=value;value=next;if(!active||delta)format();return changed;
}

inline float PopupWidth(float em){
 return std::max(120.f,std::min(ImGui::GetFontSize()*em,ImGui::GetIO().DisplaySize.x-32.f));
}
inline bool BeginMenu(const char* label,bool enabled=true,float em=26.f){
 const float width=PopupWidth(em);
 ImGui::SetNextWindowSizeConstraints({width,0.f},{width,FLT_MAX});
 ImGui::SetNextWindowSize({width,0.f},ImGuiCond_Appearing);
 if(!ImGui::BeginMenu(label,enabled))return false;
 // ImGui recycles menu windows by depth. Switching siblings may submit both
 // into one window, contaminating the next auto-size measurement. Keep both
 // measurement frames active but unpainted; do not skip content/navigation.
 struct Measurements{ImGuiContext* context=nullptr;int frame=-1;std::unordered_map<ImGuiID,int> duplicates;};
 static Measurements measured;
 auto* context=ImGui::GetCurrentContext();const int frame=ImGui::GetFrameCount();
 if(measured.context!=context||frame<measured.frame){measured.duplicates.clear();measured.context=context;}
 if(measured.frame!=frame){for(auto it=measured.duplicates.begin();it!=measured.duplicates.end();){if(it->second<frame-1)it=measured.duplicates.erase(it);else ++it;}measured.frame=frame;}
 auto* window=ImGui::GetCurrentWindow();
 if(window->BeginCount>1)measured.duplicates[window->ID]=frame;
 const auto duplicate=measured.duplicates.find(window->ID);
 if(duplicate!=measured.duplicates.end()&&duplicate->second>=frame-1)window->Hidden=true;
 return true;
}
// Capture the owner before BeginPopupModal changes CurrentWindow. Explicit
// width removes the wrapped-text/auto-size feedback on the very first frame.
inline bool BeginConfirmation(const char* title){
 const auto* owner=ImGui::GetCurrentWindow()->RootWindow;
 const auto pos=owner->Pos,size=owner->Size;
 const float width=PopupWidth(28.f);
 ImGui::SetNextWindowPos({pos.x+size.x*.5f,pos.y+size.y*.5f},ImGuiCond_Appearing,{.5f,.5f});
 ImGui::SetNextWindowSize({width,0.f},ImGuiCond_Appearing);
 ImGui::SetNextWindowSizeConstraints({width,0.f},{width,FLT_MAX});
 ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{16.f,14.f});
 ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{8.f,10.f});
 ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,6.f);
 const bool open=ImGui::BeginPopupModal(title,nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove);
 ImGui::PopStyleVar(3);return open;
}
}
