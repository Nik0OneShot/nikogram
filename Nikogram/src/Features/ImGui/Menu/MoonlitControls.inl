// Included after MoonlitUI. Reusable controls, exclusive to the Nikogram menu.
#include "MoonlitBinding.h"
#include "../../Aimbot/AimModes.h"
namespace MoonlitUI
{
    inline int MiscPage = 0;
    inline const char* MiscNames[] = {"Movement", "Automation", "Players & blocking", "Queue & sound", "Game utilities", "Skin changer"};
    inline ConfigVar<bool>* Capturing = nullptr;
    inline ConfigVar<int>* CapturingValue = nullptr;
    inline int CaptureOnValue=1;
    inline MoonlitBinding::CaptureGate CaptureKeys;
    inline int CaptureMode = BindEnum::KeyEnum::Hold;
    inline bool CaptureDefaultOn = false;
    inline bool CaptureNotActive=false;
    inline int CaptureSpecialMode=-1;
    inline int CaptureFrame = -1;
    inline bool BlockCaptureFrame = false;
    inline std::string BindStatus;
    inline std::unordered_map<const BaseVar*,int> PendingModes;

    inline void CancelCapture() { Capturing = nullptr; CapturingValue=nullptr; CaptureFrame = -1; CaptureSpecialMode=-1; }
    inline void PollCapture()
    {
        BlockCaptureFrame = Capturing != nullptr || CapturingValue != nullptr;
        if (!BlockCaptureFrame) return;
        F::Menu.m_bInKeybind = true;
        if (!F::Menu.m_bIsOpen || !IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || CurrentBind != DEFAULT_BIND)
        { CancelCapture(); return; }
        if (GetFrameCount() <= CaptureFrame) return;
        const int key = CaptureKeys.Poll([](int i){return U::KeyHandler.Down(byte(i));},
            [](int i){return U::KeyHandler.Pressed(byte(i));}, Vars::Menu::PrimaryKey.Value, Vars::Menu::SecondaryKey.Value);
        if (!key) return;
        if (key == VK_ESCAPE) BindStatus = "Key capture cancelled.";
        else if(CapturingValue)
        {
            const bool assigned=MoonlitBinding::AssignValue(*CapturingValue,F::Binds.m_vBinds,key,CaptureMode,CapturingValue->m_vNames.front(),CaptureOnValue,0);
            BindStatus=assigned?"Key assigned. Save your config to keep it.":"Advanced binding was left unchanged.";
        }
        else
        {
            auto* var = Capturing;
            const bool assigned = CaptureSpecialMode>=0
                ? MoonlitBinding::AssignActivation(*var,F::Binds.m_vBinds,static_cast<MoonlitBinding::Activation>(CaptureSpecialMode),CaptureMode,key,var->m_vNames.front())
                : MoonlitBinding::Assign(*var,F::Binds.m_vBinds,key,CaptureMode,var->m_vNames.front(),CaptureDefaultOn,CaptureNotActive);
            BindStatus = assigned ? "Key assigned. Save your config to keep it." : "This setting now has an advanced bind; it was left unchanged.";
        }
        CancelCapture();
        // Suppress the assignment click through release, including other controls.
        CaptureFrame = GetFrameCount();
    }
    inline void Help(const char* text)
    {
        if (!text || !*text) return;
        PushStyleColor(ImGuiCol_Text,GetStyleColorVec4(ImGuiCol_TextDisabled));
        PushTextWrapPos(0); TextUnformatted(text); PopTextWrapPos(); PopStyleColor();
    }
    inline void Tip(const char* text)
    {
        if(text && *text && IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))SetTooltip("%s",text);
    }
    // Formatting is presentation only. Never round the saved float on inspection.
    inline std::string NumberFormat(const char* source,float step)
    {
        std::string result=source?source:"%g";
        const auto at=result.find("%g");
        if(at!=std::string::npos)result.replace(at,2,step>=1.f?"%.0f":step>=.1f?"%.1f":"%.2f");
        return result;
    }
    template<class T> inline bool Numeric(const char* id,T& value,T low,T high,const char* format,int limits=0)
    {
        PushID(id);SetNextItemWidth(-1);
        // NoRoundToFormat keeps compact display precision independent of the value.
        PushStyleColor(ImGuiCol_FrameBg,ImVec4());PushStyleColor(ImGuiCol_FrameBgHovered,ImVec4());PushStyleColor(ImGuiCol_FrameBgActive,ImVec4());
        PushStyleColor(ImGuiCol_SliderGrab,ImVec4());PushStyleColor(ImGuiCol_SliderGrabActive,ImVec4());
        bool changed;
        if constexpr(std::is_same_v<T,int>)changed=SliderInt("##value",&value,low,high,format,ImGuiSliderFlags_NoRoundToFormat);
        else changed=SliderFloat("##value",&value,low,high,format,ImGuiSliderFlags_NoRoundToFormat);
        PopStyleColor(5);
        if(!TempInputIsActive(GetID("##value")))
        {
            const auto lo=GetItemRectMin(),hi=GetItemRectMax();const float k=Vars::Menu::Scale.Value;
            const float target=high>low?std::clamp(float((double(value)-double(low))/(double(high)-double(low))),0.f,1.f):0.f;
            const float t=Ease(GetID("thumb"),target,true),x=lo.x+4*k+(hi.x-lo.x-8*k)*t;
            auto* draw=GetWindowDrawList();
            draw->AddRectFilled({lo.x,hi.y-3*k},hi,GetColorU32(ImGuiCol_FrameBg),k);
            draw->AddRectFilled({lo.x,hi.y-3*k},{x,hi.y},GetColorU32(Purple),k);
            draw->AddRectFilled({x-2*k,hi.y-4*k},{x+2*k,hi.y+1*k},GetColorU32(Purple),k);
        }
        Tip("Right-click to enter an exact value. Ctrl-click also works.");
        static T draft{};
        if(IsItemHovered()&&IsMouseReleased(ImGuiMouseButton_Right))draft=value;
        if(BeginPopupContextItem("Exact value"))
        {
            SetNextItemWidth(170*Vars::Menu::Scale.Value);if(IsWindowAppearing())SetKeyboardFocusHere();
            if constexpr(std::is_same_v<T,int>)InputInt("##exact",&draft,0,0);
            else InputFloat("##exact",&draft,0,0,"%.9g");
            const bool enter=IsKeyPressed(ImGuiKey_Enter);
            if((Button("Apply")||enter)&&std::isfinite(double(draft))){value=draft;changed=true;CloseCurrentPopup();}
            SameLine();if(Button("Cancel"))CloseCurrentPopup();EndPopup();
        }
        if(changed)
        {
            if(!std::isfinite(double(value)))value=low;
            if(limits&(SLIDER_CLAMP|SLIDER_MIN))value=std::max(low,value);
            if(limits&(SLIDER_CLAMP|SLIDER_MAX))value=std::min(high,value);
        }
        PopID();return changed;
    }
    struct Card
    {
        bool open = false;
        Card(const char* title, bool collapsed = false)
        {
            PushID(title);
            BeginChild("panel", {0,0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            PushStyleColor(ImGuiCol_Text, Gold);
            if (collapsed) open = CollapsingHeader(title);
            else { TextUnformatted(title); Separator(); open = true; }
            PopStyleColor();
        }
        ~Card() { EndChild(); PopID(); }
        explicit operator bool() const { return open; }
    };
    inline void QuickBind(ConfigVar<bool>& source, bool defaultOn = false, bool notActive=false)
    {
        auto& var=AimModes::Resolve(source);
        const auto match = MoonlitBinding::Inspect(var,F::Binds.m_vBinds,defaultOn,notActive);
        const auto* bind = match.index >= 0 ? &F::Binds.m_vBinds[match.index] : nullptr;
        std::string caption = Capturing == &var ? "..." : match.advanced ? "Advanced" : bind ? U::KeyHandler.String(byte(bind->m_iKey)) : "Bind";
        if (caption.empty() || caption == "none") caption = "Bind";
        BeginDisabled(CurrentBind != DEFAULT_BIND || match.advanced || (var.m_iFlags & (NOSAVE | NOBIND)));
        if (Button((caption+"###key").c_str(),{std::max(62.f*Vars::Menu::Scale.Value,CalcTextSize(caption.c_str()).x+GetStyle().FramePadding.x*2),0}))
        {
            CancelCapture();Capturing = &var; CaptureFrame = GetFrameCount(); F::Menu.m_bInKeybind = true;
            CaptureMode = bind ? bind->m_iInfo : PendingModes[&var];
            CaptureDefaultOn = defaultOn;
            CaptureNotActive=notActive;
            CaptureKeys.Begin([](int i){return U::KeyHandler.Down(byte(i));});
            BindStatus = notActive?"Press a key; Escape cancels. Hold enables; release disables.":"Press a key; Escape cancels.";
        }
        if (IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            SetTooltip("%s",match.advanced ? "Shared or advanced binding: edit it in Configs & binds. It will not be overwritten here."
                : CurrentBind != DEFAULT_BIND ? "Stop editing the current bind before assigning a key."
                : "Click, then press a key. Right-click for Hold / Toggle. A bound switch enables or disables that binding.");
        if (BeginPopupContextItem("activation"))
        {
            TextUnformatted("Activation");
            int mode = bind ? bind->m_iInfo : PendingModes[&var];
            for (int i=0;i<2;++i) if (Selectable(i==0?"Hold":"Toggle",mode==i))
            {
                CaptureMode=i;PendingModes[&var]=i;
                if (match.index >= 0) { auto& b=F::Binds.m_vBinds[match.index];b.m_iInfo=i;b.m_bActive=notActive;b.m_tKeyStorage={}; }
            }
            if (match.index >= 0 && Selectable(defaultOn?"Clear key (leave on)":"Clear key (leave off)"))
            {
                const int id = match.index;
                RemoveBindAndRemap(id);
                var.Map[DEFAULT_BIND]=defaultOn;
                BindStatus="Binding removed. Save your config to keep it.";
            }
            EndPopup();
        }
        EndDisabled();
    }
    inline void QuickActivationBind(ConfigVar<bool>& var,MoonlitBinding::Activation special,const char* automaticLabel=nullptr)
    {
        const char* automatic=automaticLabel?automaticLabel:special==MoonlitBinding::Dangersense?"Dangersense":"On lethal";
        const auto match=MoonlitBinding::InspectActivation(var,F::Binds.m_vBinds,special);
        const auto* bind=match.index>=0?&F::Binds.m_vBinds[match.index]:nullptr;
        PendingModes.try_emplace(&var,BindEnum::KeyEnum::Toggle);
        const int mode=bind?MoonlitBinding::ActivationMode(*bind,special):PendingModes[&var];
        std::string caption=Capturing==&var?"...":match.advanced?"Custom":mode==special?automatic
            :bind&&bind->m_iType==BindEnum::Key?U::KeyHandler.String(byte(bind->m_iKey)):"Bind";
        if(caption.empty()||caption=="none")caption="Bind";
        BeginDisabled(CurrentBind!=DEFAULT_BIND||match.advanced||(var.m_iFlags&(NOSAVE|NOBIND)));
        if(Button((caption+"###key").c_str(),{std::max(62.f*Vars::Menu::Scale.Value,CalcTextSize(caption.c_str()).x+GetStyle().FramePadding.x*2),0}))
        {
            CancelCapture();Capturing=&var;CaptureSpecialMode=special;CaptureFrame=GetFrameCount();
            CaptureMode=mode==MoonlitBinding::Hold||mode==MoonlitBinding::Toggle?mode:PendingModes[&var];
            F::Menu.m_bInKeybind=true;CaptureKeys.Begin([](int i){return U::KeyHandler.Down(byte(i));});
            BindStatus="Press a key; Escape cancels. Assigning a key replaces automatic activation.";
        }
        Tip(match.advanced?"Shared or multiple overrides are preserved. Use Configs > Edit binds to simplify them."
            :"Click, then press a key. Right-click for Hold, Toggle, or automatic activation. The checkbox enables this binding.");
        if(BeginPopupContextItem("activation"))
        {
            bool changed=false;
            for(int choice:{int(MoonlitBinding::Hold),int(MoonlitBinding::Toggle),int(special)})
            {
                const char* label=choice==MoonlitBinding::Hold?"Hold":choice==MoonlitBinding::Toggle?"Toggle":automatic;
                if(Selectable(label,mode==choice))
                {
                    CancelCapture();
                    if(choice!=special)PendingModes[&var]=choice;
                    if(match.index>=0||choice==special)
                        changed=MoonlitBinding::AssignActivation(var,F::Binds.m_vBinds,special,choice,0,var.m_vNames.front());
                    BindStatus="Activation mode selected. Save your config to keep it.";
                    break;
                }
                if(choice==special)Tip(special==MoonlitBinding::Dangersense
                    ?"Activate radar only when an enemy Spy is behind you, including through walls. No key required."
                    :"Activate anti-aim for any enemy Sniper with clear line of sight, even unscoped, looking away or holding another weapon. Includes Snipers outside your camera view. Other threats still require estimated lethal damage. No key required.");
            }
            if(!changed&&match.index>=0&&Selectable("Clear binding (leave off)"))
            {
                CancelCapture();RemoveBindAndRemap(match.index);var.Map[DEFAULT_BIND]=false;
                BindStatus="Binding removed. Save your config to keep it.";
            }
            EndPopup();
        }
        EndDisabled();
    }
    inline void SettingActivation(ConfigVar<bool>& var,MoonlitBinding::Activation special,const char* automaticLabel=nullptr)
    {
        PushID(var.Name());
        const auto match=MoonlitBinding::InspectActivation(var,F::Binds.m_vBinds,special);
        const bool bound=match.index>=0;
        bool enabled=bound?F::Binds.m_vBinds[match.index].m_bEnabled:FGet(var,true);
        bool committed=false;
        BeginDisabled(Disabled);
        if(BeginTable("row",2,ImGuiTableFlags_SizingStretchProp))
        {
            TableSetupColumn("label",ImGuiTableColumnFlags_WidthStretch);
            const char* automatic=automaticLabel?automaticLabel:special==MoonlitBinding::Dangersense?"Dangersense":"On lethal";
            TableSetupColumn("key",ImGuiTableColumnFlags_WidthFixed,std::max(62.f*Vars::Menu::Scale.Value,CalcTextSize(automatic).x+GetStyle().FramePadding.x*2));
            TableNextColumn();
            if(Checkbox(StripDoubleHash(var.m_vNames.front()).c_str(),&enabled)&&bound)
            {
                auto& b=F::Binds.m_vBinds[match.index];b.m_bEnabled=enabled;b.m_bActive=false;b.m_tKeyStorage={};
            }
            Tip(bound?"Enable or disable this activation binding. Its live state is shown in the binds indicator.":"Enable this feature, or assign a key/automatic activation on the right.");
            // Commit an unbound master before drawing the button: selecting an
            // automatic mode then establishes OFF base + ON condition override.
            if(!bound){FSet(var,enabled);committed=true;}
            TableNextColumn();QuickActivationBind(var,special,automaticLabel);EndTable();
        }
        EndDisabled();
        // BeginTable may return false when this row is clipped. The styled
        // getter still ran, so complete its pair even without a visible table.
        if(!bound&&!committed)FSet(var,enabled);
        PopID();
    }
    inline void ValueBind(ConfigVar<int>& var,int on)
    {
        PushID(var.Name());
        const auto match=MoonlitBinding::InspectValue(var,F::Binds.m_vBinds,on,0);
        const auto* bind=match.index>=0?&F::Binds.m_vBinds[match.index]:nullptr;
        std::string caption=CapturingValue==&var?"...":match.advanced?"Advanced":bind?U::KeyHandler.String(byte(bind->m_iKey)):"Bind";
        BeginDisabled(CurrentBind!=DEFAULT_BIND||match.advanced||on==0);
        if(Button((caption+"###key").c_str(),{62*Vars::Menu::Scale.Value,0}))
        {
            CancelCapture();CapturingValue=&var;CaptureOnValue=on;CaptureFrame=GetFrameCount();CaptureMode=bind?bind->m_iInfo:PendingModes[&var];
            F::Menu.m_bInKeybind=true;CaptureKeys.Begin([](int i){return U::KeyHandler.Down(byte(i));});
        }
        Tip(match.advanced?"This setting has advanced overrides. Use Configs > Edit binds.":"Click, then press a key. Right-click for Hold / Toggle.");
        if(BeginPopupContextItem("activation"))
        {
            const int mode=bind?bind->m_iInfo:PendingModes[&var];
            for(int i=0;i<2;++i)if(Selectable(i==0?"Hold":"Toggle",mode==i))
            {PendingModes[&var]=i;if(match.index>=0){auto& b=F::Binds.m_vBinds[match.index];b.m_iInfo=i;b.m_bActive=false;b.m_tKeyStorage={};}}
            if(match.index>=0&&Selectable("Clear key (leave off)")){RemoveBindAndRemap(match.index);var.Map[DEFAULT_BIND]=0;}
            EndPopup();
        }
        EndDisabled();PopID();
    }
    inline void Setting(ConfigVar<bool>& source, const char* detail = nullptr, bool binding = false, const char* label = nullptr)
    {
        auto& var=AimModes::Resolve(source);
        PushID(var.Name());
        const auto match = binding && CurrentBind==DEFAULT_BIND ? MoonlitBinding::Inspect(var,F::Binds.m_vBinds) : MoonlitBinding::Match{};
        const bool simple=match.index>=0;
        bool value=simple?F::Binds.m_vBinds[match.index].m_bEnabled:FGet(var,true);
        const bool inverted=(var.m_iFlags&TOGGLE_INVERT)!=0;
        bool display=inverted?!value:value;
        BeginDisabled(Disabled);
        if(BeginTable("row",binding?2:1,ImGuiTableFlags_SizingStretchProp))
        {
            TableSetupColumn("label",ImGuiTableColumnFlags_WidthStretch);
            if(binding)TableSetupColumn("key",ImGuiTableColumnFlags_WidthFixed,62*Vars::Menu::Scale.Value);
            TableNextColumn();
            if(Checkbox(label?label:StripDoubleHash(var.m_vNames.front()).c_str(),&display))
            {
                value=inverted?!display:display;
                if(simple){auto& b=F::Binds.m_vBinds[match.index];b.m_bEnabled=value;b.m_bActive=false;b.m_tKeyStorage={};}
            }
            Tip(detail);
            if(binding){TableNextColumn();QuickBind(var);}
            EndTable();
        }
        EndDisabled();
        if(!simple) FSet(var,value);
        PopID();
    }
    template<class T> inline void SettingSlider(ConfigVar<T>& source, const char* detail=nullptr, const char* label=nullptr)
    {
        auto& var=AimModes::Resolve(source);
        PushID(var.Name());auto value=FGet(var,true);TextWrapped("%s",label?label:StripDoubleHash(var.m_vNames.front()).c_str());Tip(detail);
        BeginDisabled(Disabled);SetNextItemWidth(-1);
        if constexpr(std::is_same_v<T,int>)Numeric("slider",value,var.m_unMin.i,var.m_unMax.i,var.m_sExtra,var.m_iFlags);
        else Numeric("slider",value,var.m_unMin.f,var.m_unMax.f,NumberFormat(var.m_sExtra,var.m_unStep.f).c_str(),var.m_iFlags);
        EndDisabled();FSet(var,value);PopID();
    }
    inline void SettingChoice(ConfigVar<int>& source,const char* detail=nullptr)
    {
        auto& var=AimModes::Resolve(source);
        PushID(var.Name());auto value=FGet(var,true);
        const bool multi=(var.m_iFlags&DROPDOWN_MULTI)!=0;
        std::string preview;int index=0;
        for(const char* label:var.m_vValues)
        {
            if(!strcmp(label,"##Divider"))continue;
            const int id=multi?int(1u<<index):index;
            if(multi?(value&id)!=0:value==id){if(!preview.empty())preview+=", ";preview+=StripDoubleHash(label);}
            ++index;
        }
        if(preview.empty())preview=var.m_sExtra?var.m_sExtra:"None";
        TextWrapped("%s",StripDoubleHash(var.m_vNames.front()).c_str());BeginDisabled(Disabled);SetNextItemWidth(-1);
        if(BeginCombo("##choice",preview.c_str(),ImGuiComboFlags_HeightLarge))
        {
            index=0;
            for(const char* label:var.m_vValues)
            {
                if(!strcmp(label,"##Divider")){Separator();continue;}
                const int id=multi?int(1u<<index):index;
                if(Selectable(label,multi?(value&id)!=0:value==id,multi?ImGuiSelectableFlags_DontClosePopups:0))
                {if(multi)value^=id;else value=id;}
                ++index;
            }
            EndCombo();
        }
        if(IsItemHovered())SetTooltip("%s",preview.c_str());
        Tip(detail);EndDisabled();FSet(var,value);PopID();
    }
    inline void ActivationRow(ConfigVar<bool>& master,ConfigVar<bool>& activation,const char* label)
    {
        PushID(master.Name());
        auto value=FGet(master,true);BeginDisabled(Disabled);
        if(Checkbox(label,&value)){}
        EndDisabled();FSet(master,value);SameLine();
        PendingModes.try_emplace(&activation,BindEnum::KeyEnum::Hold);
        // ESP is normally visible and its key hides it; aim/trigger keys enable.
        QuickBind(activation,true,&activation!=&Vars::ESP::MoonlitEnabled);
        PopID();
    }
}
