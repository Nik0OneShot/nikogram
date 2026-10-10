// Three native shells sharing the same real settings, class scopes and editors.
// There is no preview-only settings copy and no gameplay state migration.
namespace DapperUI
{
    using namespace ImGui;
    struct State { int page=2,searchMode=AimModes::Legit;std::string search; };
    inline State Pages[3];
    inline const char* PagesNames[]{"Ragebot","Anti aim","Legitbot","ESP","Misc","Skins","Players","Configs","Interface"};
    inline const char* BankNames[]{"Aim controls","Angle controls","Assist controls","Visual assets","Utilities","Inventory","Members","Accounts","Preferences"};
    inline void Photo(int style,ImVec2 size)
    {
        auto pos=GetCursorScreenPos();auto* draw=GetWindowDrawList();
        draw->AddRectFilled(pos-ImVec2(4,4),pos+size+ImVec2(4,4),GetColorU32(ImGuiCol_Border));
        if(auto texture=F::Render.DapperPhoto(DapperStyle::Get(style).photo))
            ImageWithBg(texture,size,{0,0},{1,1},{0,0,0,0},{1,1,1,DapperStyle::Opacity(style)});
        else {Dummy(size);draw->AddText(pos,GetColorU32(ImGuiCol_Text),"Dapper Mann");}
    }
    inline void Navigation(State& state,bool vertical,bool bank,float k)
    {
        const float right=GetCursorScreenPos().x+GetContentRegionAvail().x;
        for(int i=0;i<9;++i)
        {
            const char* label=bank?BankNames[i]:PagesNames[i];
            const float width=vertical?GetContentRegionAvail().x:CalcTextSize(label).x+18*k;
            if(!vertical&&i&&GetItemRectMax().x+GetStyle().ItemSpacing.x+width<=right)SameLine();
            PushID(i);PushStyleColor(ImGuiCol_Button,state.page==i?F::Render.Accent.Value:F::Render.Background0p5.Value);
            if(Button(label,{width,vertical?37*k:31*k}))
            {state.page=i;MoonlitUI::CancelCapture();ClearActiveID();ActiveMap.clear();}
            PopStyleColor();PopID();
        }
    }
}

void CMenu::DrawDapper()
{
    using namespace ImGui;using namespace MoonlitUI;
    const int appearance=MenuMode::Active,index=DapperStyle::Index(appearance);
    const bool bank=appearance==MenuMode::BankOfDapper,scrapbook=appearance==MenuMode::DapperScrapbook;
    auto& state=DapperUI::Pages[index];StyleScope theme(appearance);PushFont(F::Render.FontRegular);
    const float k=Vars::Menu::Scale.Value;const auto screen=GetIO().DisplaySize;
    const float bar=H::Draw.Scale(26),top=Workspace::TopTaskbar?bar:0.f,bottom=screen.y-(Workspace::TopTaskbar?0.f:bar);
    const char* window=bank?"Bank of Dapper###DapperBankMain":scrapbook?"Dapper Scrapbook###DapperScrapbookMain":"Dapper Desktop###DapperDesktopMain";
    const ImVec2 size(std::max(1.f,std::min(1180*k,screen.x-16)),std::max(1.f,std::min(830*k,bottom-top-16)));
    SetNextWindowSize(size,ResetCompactLayout?ImGuiCond_Always:ImGuiCond_FirstUseEver);
    SetNextWindowPos({(screen.x-size.x)*.5f,top+(bottom-top-size.y)*.5f},ResetCompactLayout?ImGuiCond_Always:ImGuiCond_FirstUseEver);
    if(!ResetCompactLayout)if(auto* existing=FindWindowByName(window))
        SetNextWindowPos({std::clamp(existing->Pos.x,0.f,std::max(0.f,screen.x-existing->SizeFull.x)),
            std::clamp(existing->Pos.y,top,std::max(top,bottom-existing->SizeFull.y))},ImGuiCond_Always);
    SetNextWindowSizeConstraints({std::min(900*k,screen.x-16),std::min(600*k,bottom-top-16)},{std::max(1.f,screen.x-8),std::max(1.f,bottom-top-8)});
    ResetCompactLayout=false;bool open=true;
    NikoPet::World.windows.clear();NikoPet::World.width=screen.x;NikoPet::World.height=screen.y;
    NikoPet::World.barWidth=screen.x;NikoPet::World.barHeight=bar;NikoPet::World.top=Workspace::TopTaskbar;
    if(Begin(window,&open,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoScrollbar))
    {
        PollCapture();BeginDisabled(BlockCaptureFrame);
        const auto pos=GetWindowPos(),extent=GetWindowSize();NikoPet::World.windows.push_back({100,pos.x,pos.y,pos.x+extent.x,pos.y+extent.y});
        if(scrapbook)
        {
            auto* draw=GetWindowDrawList();
            for(float y=pos.y+84*k;y<pos.y+extent.y-24*k;y+=56*k)
                draw->AddRectFilled({pos.x+5*k,y},{pos.x+18*k,y+7*k},GetColorU32(ImGuiCol_Border),3*k);
            Indent(20*k);
        }
        DapperUI::Photo(appearance,{31*k,40*k});SameLine();BeginGroup();
        PushFont(F::Render.FontLarge);
        TextUnformatted(bank?"Bank of Dapper":scrapbook?"Dapper Scrapbook":"DAPPER MANN");PopFont();
        TextDisabled("%s",bank?"PRIVATE CLIENT SOFTWARE":scrapbook?"Settings, keepsakes & questionable decisions":"Personal control centre");EndGroup();
        Separator();
        // Sidebar for Bank, tab strips for Desktop and Scrapbook.
        if(bank)
        {
            if(BeginChild("Bank navigation",{155*k,std::max(1.f,GetContentRegionAvail().y-26*k)},ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding))
            {
                DapperUI::Navigation(state,true,true,k);Spacing();Separator();TextDisabled("Member 0001");TextUnformatted("Dapper Mann");
                TextDisabled("Personal account");
            }
            EndChild();SameLine();
        }
        else {DapperUI::Navigation(state,false,false,k);Separator();}
        if(BeginChild("Dapper workspace",{0,std::max(1.f,GetContentRegionAvail().y-26*k)},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar))
        {
            if(state.page==0)state.searchMode=AimModes::Rage;else if(state.page==2)state.searchMode=AimModes::Legit;
            const float searchWidth=std::max(60.f,GetContentRegionAvail().x-65*k);SetNextItemWidth(searchWidth);
            InputTextWithHint("##DapperSearch",bank?"Find a control...":scrapbook?"Find a note or setting...":"Search all settings...",&state.search);
            SameLine();if(Button("Clear"))state.search.clear();
            Bind_t bind;if(!F::Binds.GetBind(CurrentBind,&bind))CurrentBind=DEFAULT_BIND;
            if(CurrentBind!=DEFAULT_BIND){Text("Editing: %s",bind.m_sName.c_str());SameLine();if(SmallButton("Back to base"))CurrentBind=DEFAULT_BIND;}
            Separator();
            const float contentWidth=GetContentRegionAvail().x;
            const bool photoColumn=!bank&&contentWidth>760*k;
            const float photoWidth=scrapbook?137*k:108*k;
            if(photoColumn&&!scrapbook)
            {
                if(BeginChild("Membership photo",{photoWidth,0},ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding))
                {DapperUI::Photo(appearance,{std::max(1.f,GetContentRegionAvail().x),115*k});Spacing();TextDisabled("LICENSED TO");TextUnformatted("Dapper Mann");TextDisabled("Personal edition");}
                EndChild();SameLine();
            }
            const float settingsWidth=photoColumn&&scrapbook?std::max(1.f,contentWidth-photoWidth-GetStyle().ItemSpacing.x):0;
            if(BeginChild("Dapper settings",{settingsWidth,0},ImGuiChildFlags_AlwaysUseWindowPadding))
            {
                TextColored(Gold,"%s",DapperUI::PagesNames[state.page]);Separator();
                if(!state.search.empty())
                {
                    int mode=state.searchMode==AimModes::Rage?0:1;SetNextItemWidth(180*k);
                    if(Combo("Aim settings for",&mode,"Ragebot\0Legitbot\0"))state.searchMode=mode?AimModes::Legit:AimModes::Rage;
                    AimModes::EditScope editing(state.searchMode);MenuSearch(state.search);
                }
                else switch(state.page)
                {
                    case 0:MoonlitAim(false);break;case 1:MoonlitAntiAim();break;case 2:MoonlitAim(true);break;
                    case 3:MoonlitESP();break;case 4:MoonlitMisc();break;case 5:SkinChanger::Menu();break;
                    case 6:MoonlitPlayers();break;case 7:MoonlitConfigs();break;
                    case 8:if(DrawInterfacePreferences())RequestedEditor=Editor::Visuals;break;
                }
            }
            EndChild();
            if(photoColumn&&scrapbook)
            {
                SameLine();if(BeginChild("Pinned portrait",{photoWidth,0},ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding))
                {
                    DapperUI::Photo(appearance,{std::max(1.f,GetContentRegionAvail().x),148*k});
                    Spacing();TextWrapped("A steady hand.");Spacing();Separator();TextWrapped("DAPPER APPROVED");
                }
                EndChild();
            }
        }
        EndChild();
        if(scrapbook)Unindent(20*k);
        Separator();TextDisabled("%s  |  %s",DapperStyle::Name(appearance),MenuMode::Indicator<0?"Indicators follow menu":"Independent indicator style");
        EndDisabled();MoonlitEditor();
    }
    End();
    if(!open){CancelCapture();m_bIsOpen=false;I::MatSystemSurface->SetCursorAlwaysVisible(false);}
    PopFont();
}
