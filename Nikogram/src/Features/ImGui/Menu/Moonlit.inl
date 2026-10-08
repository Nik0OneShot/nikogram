// Included by Menu.cpp: shared settings pages remain the single source of truth.
namespace MoonlitUI
{
    using namespace ImGui;
    inline ImVec4 Colour(int r, int g, int b) { return {r/255.f,g/255.f,b/255.f,1.f}; }
    inline const ImVec4 Purple = Colour(192,163,243), Gold = Colour(242,204,127);
    struct StyleScope
    {
        ImGuiStyle style = GetStyle();
        ImColor colours[9] = {F::Render.Accent,F::Render.Background0,F::Render.Background0p5,F::Render.Background1,
            F::Render.Background1p5,F::Render.Background1p5L,F::Render.Background2,F::Render.Inactive,F::Render.Active};
        ImFont* savedSmallFont=F::Render.FontSmall, *savedRegularFont=F::Render.FontRegular, *savedLargeFont=F::Render.FontLarge;
        StyleScope()
        {
            MenuMode::DrawingMoonlit = true;
            F::Render.Accent=Purple; F::Render.Background0=Colour(33,27,48);
            F::Render.Background0p5=F::Render.Background1=F::Render.Background1p5=Colour(43,36,60);
            F::Render.Background1p5L=Colour(67,51,90);F::Render.Background2=Colour(110,86,145);
            F::Render.Active=Colour(241,234,250);F::Render.Inactive=Colour(187,175,202);
            F::Render.FontSmall=F::Render.FontRegular=F::Render.FontMoonlit;
            F::Render.FontLarge=F::Render.FontMoonlitHeading;
            auto& s=GetStyle();const float k=Vars::Menu::Scale.Value;
            s.WindowPadding={12*k,12*k};s.FramePadding={6*k,2*k};s.ItemSpacing={8*k,4*k};s.ItemInnerSpacing={6*k,3*k};
            s.CellPadding={4*k,2*k}; // Space between settings columns, without changing Nullcore.
            s.WindowRounding=7*k;s.ChildRounding=2*k;s.FrameRounding=2*k;s.PopupRounding=4*k;s.GrabRounding=2*k;
            s.ScrollbarSize=10*k;s.ScrollbarRounding=8*k;s.FrameBorderSize=0;s.ChildBorderSize=1;s.WindowTitleAlign={.5f,.5f};
            auto* c=s.Colors;
            c[ImGuiCol_Text]=F::Render.Active;c[ImGuiCol_TextDisabled]=F::Render.Inactive;
            c[ImGuiCol_WindowBg]=F::Render.Background0;c[ImGuiCol_ChildBg]=F::Render.Background1;
            c[ImGuiCol_PopupBg]=Colour(39,31,55);c[ImGuiCol_Border]=c[ImGuiCol_Separator]=Colour(72,59,92);
            c[ImGuiCol_TableBorderLight]=c[ImGuiCol_TableBorderStrong]=c[ImGuiCol_Border];
            c[ImGuiCol_TitleBg]=c[ImGuiCol_TitleBgActive]=Colour(25,21,33);
            c[ImGuiCol_FrameBg]=Colour(25,21,33);c[ImGuiCol_FrameBgHovered]=F::Render.Background1p5L;c[ImGuiCol_FrameBgActive]=F::Render.Background1p5L;
            c[ImGuiCol_Button]=F::Render.Background1;c[ImGuiCol_ButtonHovered]=F::Render.Background1p5L;c[ImGuiCol_ButtonActive]=F::Render.Background2;
            c[ImGuiCol_Header]=F::Render.Background1p5L;c[ImGuiCol_HeaderHovered]=F::Render.Background1p5L;c[ImGuiCol_HeaderActive]=F::Render.Background2;
            c[ImGuiCol_CheckMark]=c[ImGuiCol_SliderGrab]=Purple;c[ImGuiCol_SliderGrabActive]=Gold;
            c[ImGuiCol_ScrollbarGrab]=F::Render.Background2;c[ImGuiCol_ScrollbarGrabHovered]=Purple;c[ImGuiCol_ScrollbarGrabActive]=Purple;
            c[ImGuiCol_ResizeGrip]=Colour(72,59,92);c[ImGuiCol_ResizeGripHovered]=Purple;c[ImGuiCol_ResizeGripActive]=Purple;
        }
        ~StyleScope()
        {
            GetStyle()=style;F::Render.Accent=colours[0];F::Render.Background0=colours[1];F::Render.Background0p5=colours[2];
            F::Render.Background1=colours[3];F::Render.Background1p5=colours[4];F::Render.Background1p5L=colours[5];
            F::Render.Background2=colours[6];F::Render.Inactive=colours[7];F::Render.Active=colours[8];
            F::Render.FontSmall=savedSmallFont;F::Render.FontRegular=savedRegularFont;F::Render.FontLarge=savedLargeFont;MenuMode::DrawingMoonlit=false;
        }
    };
    inline float Ease(ImGuiID id,float target,bool slider=false)
    {
        float* value=GetStateStorage()->GetFloatRef(id,target);
        if(!MenuMode::Animations || (slider && !MenuMode::SmoothSliders)) *value=target;
        else *value+= (target-*value)*(1.f-std::exp(-18.f*MenuMode::AnimationSpeed*std::min(GetIO().DeltaTime,.1f)));
        return *value;
    }
    inline bool Toggle(const char* label,const char* detail,bool& value)
    {
        PushID(label);const auto pos=GetCursorScreenPos();const float w=GetContentRegionAvail().x,k=Vars::Menu::Scale.Value;
        const float height=std::max(62*k,GetTextLineHeight()*2+16*k);
        const bool clicked=InvisibleButton("switch",{w,height});if(clicked)value=!value;
        if(IsItemHovered())SetMouseCursor(ImGuiMouseCursor_Hand);
        const float t=Ease(GetID("animation"),value?1.f:0.f);
        const auto lo=pos+ImVec2(w-38*k,(height-22*k)*.5f);
        auto* draw=GetWindowDrawList();draw->AddRectFilled(lo,lo+ImVec2(38*k,22*k),GetColorU32(ImLerp(Colour(92,76,111),Purple,t)),11*k);
        draw->AddCircleFilled(lo+ImVec2((11+16*t)*k,11*k),8*k,GetColorU32(value?Colour(36,22,59):Colour(241,234,250)));
        draw->PushClipRect(pos,pos+ImVec2(std::max(1.f,w-50*k),height),true);
        draw->AddText(pos+ImVec2(0,9*k),GetColorU32(ImGuiCol_Text),label);
        draw->AddText(pos+ImVec2(0,9*k+GetTextLineHeight()+4*k),GetColorU32(ImGuiCol_TextDisabled),detail);
        draw->PopClipRect();PopID();return clicked;
    }
    inline bool Slider(const char* label,float& value,float low,float high,const char* format)
    {
        PushID(label);TextUnformatted(label);SetNextItemWidth(-1);
        PushStyleColor(ImGuiCol_SliderGrab,ImVec4());PushStyleColor(ImGuiCol_SliderGrabActive,ImVec4());
        const bool changed=SliderFloat("##value",&value,low,high,format,ImGuiSliderFlags_AlwaysClamp);
        PopStyleColor(2);
        if(!TempInputIsActive(GetID("##value")))
        {
            const auto lo=GetItemRectMin(),hi=GetItemRectMax();const float k=Vars::Menu::Scale.Value;
            const float t=Ease(GetID("thumb"),std::clamp((value-low)/(high-low),0.f,1.f),true);
            const float x=lo.x+9*k+(hi.x-lo.x-18*k)*t;
            GetWindowDrawList()->AddRectFilled({lo.x+5*k,hi.y-4*k},{x,hi.y-2*k},GetColorU32(Purple),2*k);
            GetWindowDrawList()->AddCircleFilled({x,hi.y-3*k},4*k,GetColorU32(Purple));
        }
        PopID();return changed;
    }
}

#include "MoonlitMisc.inl"
#include "MoonlitVisuals.inl"

#include "MoonlitCompact.inl"

void CMenu::DrawMoonlit()
{
    using namespace ImGui;using namespace MoonlitUI;
    StyleScope theme;PushFont(F::Render.FontMoonlit);
    static int page=0;
    const char* names[]={"Ragebot","Anti aim","Legitbot","ESP","Misc","Skin changer","Playerlist","Configs"};
    const char* icons[]={ICON_MD_GPS_FIXED,ICON_MD_SWAP_HORIZ,ICON_MD_MOUSE,ICON_MD_VISIBILITY,ICON_MD_SETTINGS,ICON_MD_BRUSH,ICON_MD_PERSON,ICON_MD_SAVE};
    const auto screen=GetIO().DisplaySize;const float k=Vars::Menu::Scale.Value;
    const float bar=H::Draw.Scale(26),top=Workspace::TopTaskbar?bar:0.f,bottom=screen.y-(Workspace::TopTaskbar?0.f:bar);
    NikoPet::World.windows.clear();NikoPet::World.width=screen.x;NikoPet::World.height=screen.y;
    NikoPet::World.barWidth=screen.x;NikoPet::World.barHeight=bar;NikoPet::World.top=Workspace::TopTaskbar;
    const ImVec2 size(std::min(1000*k,screen.x-16),std::min(800*k,bottom-top-16));
    SetNextWindowSize(size,ResetCompactLayout?ImGuiCond_Always:ImGuiCond_FirstUseEver);
    SetNextWindowPos({(screen.x-size.x)*.5f,top+(bottom-top-size.y)*.5f},ResetCompactLayout?ImGuiCond_Always:ImGuiCond_FirstUseEver);
    if(!ResetCompactLayout)if(auto* existing=FindWindowByName("###MoonlitCompactMain"))
        SetNextWindowPos({std::clamp(existing->Pos.x,0.f,std::max(0.f,screen.x-existing->SizeFull.x)),
            std::clamp(existing->Pos.y,top,std::max(top,bottom-existing->SizeFull.y))},ImGuiCond_Always);
    SetNextWindowSizeConstraints({std::min(820*k,screen.x-16),std::min(580*k,bottom-top-16)},{screen.x-8,bottom-top-8});
    ResetCompactLayout=false;bool windowOpen=true;
    if(Begin("nikogram###MoonlitCompactMain",&windowOpen,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoScrollbar))
    {
        PollCapture();BeginDisabled(BlockCaptureFrame);
        const auto pos=GetWindowPos(),extent=GetWindowSize();
        NikoPet::World.windows.push_back({100,pos.x,pos.y,pos.x+extent.x,pos.y+extent.y});
        const float side=std::min(150*k,GetContentRegionAvail().x*.19f);
        PushStyleColor(ImGuiCol_ChildBg,Colour(25,21,33));
        if(BeginChild("Navigation",{side,0},ImGuiChildFlags_AlwaysUseWindowPadding))
        {
            const float width=GetContentRegionAvail().x;
            if(auto logo=F::Render.NikogramLogo())Image(logo,{width,width/3.f});else TextUnformatted("nikogram");
            Spacing();Separator();Spacing();
            for(int i=0;i<8;++i)
            {
                PushID(i);
                PushStyleColor(ImGuiCol_Header,Colour(65,48,87));
                if(Selectable("##page",page==i,0,{0,49*k}))
                {page=i;CancelCapture();ClearActiveID();ActiveMap.clear();}
                const auto lo=GetItemRectMin();auto* draw=GetWindowDrawList();
                const ImU32 color=GetColorU32(page==i?Purple:F::Render.Inactive.Value);
                draw->AddText(F::Render.IconFont,18*k,{lo.x+7*k,lo.y+15*k},color,icons[i]);
                draw->AddText({lo.x+33*k,lo.y+16*k},color,names[i]);
                PopStyleColor();PopID();
            }
            Separator();
            const bool privacy=Vars::Visuals::UI::StreamerMode.Value>=Vars::Visuals::UI::StreamerModeEnum::Local;
            TextDisabled("%s",privacy?"Local config":F::Configs.m_sCurrentConfig.c_str());
        }
        EndChild();PopStyleColor();SameLine();
        if(BeginChild("Content",{},ImGuiChildFlags_AlwaysUseWindowPadding))
        {
            TextColored(Gold,"%s",names[page]);Separator();
            Bind_t bind;if(!F::Binds.GetBind(CurrentBind,&bind))CurrentBind=DEFAULT_BIND;
            if(CurrentBind!=DEFAULT_BIND)
            {
                Text("Editing: %s",bind.m_sName.c_str());SameLine();
                if(SmallButton("Back to base"))CurrentBind=DEFAULT_BIND;Separator();
            }
            switch(page)
            {
            case 0:MoonlitAim(false);break;case 1:MoonlitAntiAim();break;case 2:MoonlitAim(true);break;
            case 3:MoonlitESP();break;case 4:MoonlitMisc();break;case 5:SkinChanger::Menu();break;
            case 6:MoonlitPlayers();break;case 7:MoonlitConfigs();break;
            }
        }
        EndChild();EndDisabled();
        MoonlitEditor();
    }
    End();
    if(!windowOpen){CancelCapture();m_bIsOpen=false;I::MatSystemSurface->SetCursorAlwaysVisible(false);}
    PopFont();
}
