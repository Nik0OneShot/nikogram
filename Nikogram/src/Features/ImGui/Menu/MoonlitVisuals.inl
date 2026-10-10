// Nikogram-only presentation over the existing visual groups and config variables.
#include "MoonlitGroups.h"
namespace MoonlitUI
{
    inline int VisualPage=0, VisualGroup=0, VisualScope=0;
    inline const char* VisualNames[]={"Players & buildings","Outlines & materials","World & effects","HUD & indicators","Camera & viewmodel","Custom groups"};
    inline const char* ScopeNames[]={"All groups","Enemies","Teammates","Local player","Buildings","Viewmodels"};
    template<class T> inline void RawSlider(const char* label,T& value,T low,T high)
    {
        PushID(label);TextWrapped("%s",label);SetNextItemWidth(-1);
        if constexpr(std::is_same_v<T,int>)Numeric("raw",value,low,high,"%i");
        else Numeric("raw",value,low,high,"%.1f");
        PopID();
    }
    inline bool MasterOn()
    {
        return Vars::ESP::MoonlitMaster.Value&&Vars::ESP::MoonlitEnabled.Value;
    }

    inline bool ColourEdit(const char* label,Color_t& colour)
    {
        float c[]={colour.r/255.f,colour.g/255.f,colour.b/255.f,colour.a/255.f};
        TextWrapped("%s",label);SetNextItemWidth(-1);PushID(label);
        const bool changed=ColorEdit4("##colour",c,ImGuiColorEditFlags_AlphaBar|ImGuiColorEditFlags_NoInputs);
        if(changed)colour=Color_t(int(c[0]*255+.5f),int(c[1]*255+.5f),int(c[2]*255+.5f),int(c[3]*255+.5f));
        PopID();return changed;
    }
    inline bool SettingColour(ConfigVar<Color_t>& source)
    {
        auto& var=AimModes::Resolve(source);
        PushID(var.Name());auto c=FGet(var,true);BeginDisabled(Disabled);
        bool changed=ColourEdit(StripDoubleHash(var.m_vNames.front()).c_str(),c);
        EndDisabled();FSet(var,c);PopID();return changed;
    }
    inline void SettingString(ConfigVar<std::string>& var)
    {
        PushID(var.Name());auto value=FGet(var,true);BeginDisabled(Disabled);
        TextWrapped("%s",StripDoubleHash(var.m_vNames.front()).c_str());SetNextItemWidth(-1);
        if(BeginCombo("##choice",value.c_str(),ImGuiComboFlags_HeightLarge))
        {
            for(const char* entry:var.m_vValues)if(Selectable(entry,value==entry))value=entry;
            EndCombo();
        }
        if(var.m_iFlags&DROPDOWN_CUSTOM){SetNextItemWidth(-1);InputTextWithHint("##custom","Custom name...",&value);}
        EndDisabled();FSet(var,value);Spacing();PopID();
    }
    inline void Bits(const char* label,int& bits,std::initializer_list<std::pair<const char*,int>> entries)
    {
        PushID(label);
        for(auto [name,bit]:entries){bool on=(bits&bit)!=0;if(Checkbox(name,&on)){if(on)bits|=bit;else bits&=~bit;}}
        PopID();
    }
    inline void MasterESP()
    {
        ActivationRow(Vars::ESP::MoonlitMaster,Vars::ESP::MoonlitEnabled,"ESP enabled");Separator();
    }
    inline bool InScope(const Group_t& g,int scope)
    {
        switch(scope)
        {
        case 1:return (g.m_iTargets&TargetsEnum::Players)&&(g.m_iConditions&ConditionsEnum::Enemy);
        case 2:return (g.m_iTargets&TargetsEnum::Players)&&(g.m_iConditions&ConditionsEnum::Team);
        case 3:return (g.m_iTargets&TargetsEnum::Players)&&(g.m_iConditions&ConditionsEnum::Local);
        case 4:return (g.m_iTargets&TargetsEnum::Buildings)!=0;
        case 5:return (g.m_iTargets&(TargetsEnum::ViewmodelWeapon|TargetsEnum::ViewmodelHands))!=0;
        default:return true;
        }
    }
    inline bool GroupSelector()
    {
        auto& groups=F::Groups.m_vGroups;
        SetNextItemWidth(-1);
        if(BeginCombo("##scope",ScopeNames[VisualScope]))
        {
            for(int i=0;i<6;++i)if(Selectable(ScopeNames[i],VisualScope==i)){VisualScope=i;CancelCapture();}
            EndCombo();
        }
        if(VisualGroup<0||VisualGroup>=int(groups.size())||!InScope(groups[VisualGroup],VisualScope))
        {
            VisualGroup=-1;
            for(int i=int(groups.size())-1;i>=0;--i)if(InScope(groups[i],VisualScope)){VisualGroup=i;break;}
        }
        TextUnformatted("Editing group");SetNextItemWidth(-1);
        if(BeginCombo("##group",VisualGroup>=0?groups[VisualGroup].m_sName.c_str():"No matching group"))
        {
            for(int i=0;i<int(groups.size());++i)if(InScope(groups[i],VisualScope))
            {PushID(i);if(Selectable(groups[i].m_sName.c_str(),VisualGroup==i))VisualGroup=i;PopID();}
            EndCombo();
        }
        if(VisualGroup<0)
        {
            Help("No settings were changed. Create a starter group below, or use Custom groups to define your own targets.");
            BeginDisabled(groups.size()>=32||CurrentBind!=DEFAULT_BIND);
            if(Button("Create starter group"))
            {
                Group_t g;g.m_sName=std::string("Nikogram ")+ScopeNames[VisualScope];
                g.m_tColor=Color_t(192,163,243,255);g.m_bTagsOverrideColor=false;
                g.m_iTargets=VisualScope==4?TargetsEnum::Buildings:VisualScope==5?TargetsEnum::ViewmodelWeapon:TargetsEnum::Players;
                g.m_iConditions=VisualScope==3?ConditionsEnum::Local:(ConditionsEnum::RED|ConditionsEnum::BLU|(VisualScope==2?ConditionsEnum::Team:ConditionsEnum::Enemy));
                g.m_iESP=VisualScope==5?0:ESPEnum::Name|ESPEnum::Box|ESPEnum::HealthBar;
                VisualGroup=int(groups.size());groups.push_back(g);
                RemapActiveGroups([&](uint32_t mask){return mask|(1u<<VisualGroup);});
            }
            EndDisabled();return false;
        }
        if(Button("Custom groups...")){VisualPage=5;return false;}
        Help("Editing this exact group, not a separate preset. Use Custom groups to change who it matches or its priority.");
        int active=FGet(Vars::ESP::ActiveGroups,true);BeginDisabled(Disabled||VisualGroup>=32);
        bool enabled=VisualGroup<32&&(uint32_t(active)&(1u<<VisualGroup));
        if(Checkbox("Enable this group",&enabled)&&VisualGroup<32)
            active=int(enabled?uint32_t(active)|(1u<<VisualGroup):uint32_t(active)&~(1u<<VisualGroup));
        EndDisabled();FSet(Vars::ESP::ActiveGroups,active);
        if(CurrentBind!=DEFAULT_BIND)Help("Group appearance itself is shared by all binds. Only the active-group mask supports overrides.");
        return true;
    }
    inline void MaterialLayers(const char* label,std::vector<std::pair<std::string,Color_t>>& layers)
    {
        PushID(label);TextUnformatted(label);SetNextItemWidth(-1);
        if(BeginCombo("##materials",MoonlitGroups::LayerSummary(layers).c_str(),ImGuiComboFlags_HeightLarge))
        {
            Help("Tick multiple materials. Click a colour to edit tint and opacity.");
            Help("Top draws first; lower layers draw over it. Original + Fresnel keeps the original texture underneath.");
            std::vector<std::string> names={"Original"};
            for(auto& [id,m]:F::Materials.m_mMaterials)if(m.m_sName!="Original"&&m.m_sName!="None")names.push_back(m.m_sName);
            std::sort(names.begin()+1,names.end());names.erase(std::unique(names.begin(),names.end()),names.end());
            int remove=-1,move=-1,direction=0;
            if(BeginTable("layers",4,ImGuiTableFlags_SizingStretchProp))
            {
                TableSetupColumn("Material",ImGuiTableColumnFlags_WidthStretch);
                TableSetupColumn("Colour",ImGuiTableColumnFlags_WidthFixed,GetFrameHeight());
                TableSetupColumn("Up",ImGuiTableColumnFlags_WidthFixed,GetFrameHeight());
                TableSetupColumn("Down",ImGuiTableColumnFlags_WidthFixed,GetFrameHeight());
                for(int i=0;i<int(layers.size());++i)
                {
                    PushID(i);TableNextColumn();bool enabled=true;
                    if(Checkbox(layers[i].first.c_str(),&enabled))remove=i;
                    TableNextColumn();auto& colour=layers[i].second;
                    float c[]={colour.r/255.f,colour.g/255.f,colour.b/255.f,colour.a/255.f};
                    if(ColorEdit4("##tint",c,ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar))
                        colour=Color_t(int(c[0]*255+.5f),int(c[1]*255+.5f),int(c[2]*255+.5f),int(c[3]*255+.5f));
                    TableNextColumn();BeginDisabled(i==0);if(ArrowButton("up",ImGuiDir_Up)){move=i;direction=-1;}EndDisabled();
                    TableNextColumn();BeginDisabled(i==int(layers.size())-1);if(ArrowButton("down",ImGuiDir_Down)){move=i;direction=1;}EndDisabled();
                    PopID();
                }
                EndTable();
            }
            if(remove>=0)layers.erase(layers.begin()+remove);
            else if(move>=0)std::swap(layers[move],layers[move+direction]);
            Separator();
            for(const auto& name:names)
            {
                if(std::any_of(layers.begin(),layers.end(),[&](const auto& p){return p.first==name;}))continue;
                PushID(name.c_str());bool enabled=false;
                if(Checkbox(name.c_str(),&enabled))MoonlitGroups::AddLayer(layers,name);
                PopID();
            }
            if(layers.empty())Help("None: this model pass is not drawn.");
            EndCombo();
        }
        PopID();
    }

}

#include "MoonlitGroups.inl"

void CMenu::MenuMoonlitVisuals()
{
    using namespace ImGui;using namespace MoonlitUI;
    namespace V=Vars::Visuals;
    BeginDisabled(BlockCaptureFrame||CaptureFrame==GetFrameCount());
    MasterESP();
    if(VisualPage==5)
    {
        CustomGroups();
    }
    else if(VisualPage<=1)
    {
        if(GroupSelector())
        {
            auto& g=F::Groups.m_vGroups[VisualGroup];
            const bool wide=GetContentRegionAvail().x>=620*Vars::Menu::Scale.Value;
            if(BeginTable("VisualColumns",wide?2:1,ImGuiTableFlags_SizingStretchProp))
            {
                if(wide){TableSetupColumn("Settings",ImGuiTableColumnFlags_WidthStretch,1.35f);TableSetupColumn("Example",ImGuiTableColumnFlags_WidthStretch,1.f);}
                TableNextColumn();
                // Group data has no bind map. Do not silently edit global appearance while editing a bind.
                BeginDisabled(CurrentBind!=DEFAULT_BIND);
                if(VisualPage==0)
                {
                    if(Card c{"At a glance"};c)
                    {
                        Checkbox("Show information",&g.m_bMoonlitInformation);
                        Help("Nikogram only. Keep your selected details while hiding labels and boxes; materials stay separate.");
                        BeginDisabled(!g.m_bMoonlitInformation);
                        Bits("main",g.m_iESP,{{"Name",ESPEnum::Name},{"Box",ESPEnum::Box},{"Distance",ESPEnum::Distance}});
                        if(g.m_iTargets&(TargetsEnum::Players|TargetsEnum::Buildings))Bits("health",g.m_iESP,{{"Health bar",ESPEnum::HealthBar},{"Health number",ESPEnum::HealthText}});
                        EndDisabled();
                    }
                    if(Card c{"Extra information",true};c)
                    {
                        if(g.m_iTargets&TargetsEnum::Players)Bits("players",g.m_iESP,{{"Skeleton",ESPEnum::Bones},{"Uber bar",ESPEnum::UberBar},{"Uber number",ESPEnum::UberText},{"Class icon",ESPEnum::ClassIcon},{"Class name",ESPEnum::ClassText},{"Weapon icon",ESPEnum::WeaponIcon},{"Weapon name",ESPEnum::WeaponText},{"Priority",ESPEnum::Priority},{"Labels",ESPEnum::Labels},{"Buffs",ESPEnum::Buffs},{"Debuffs",ESPEnum::Debuffs},{"Lag compensation",ESPEnum::LagCompensation},{"Ping",ESPEnum::Ping},{"KDR",ESPEnum::KDR}});
                        if(g.m_iTargets&(TargetsEnum::Buildings|TargetsEnum::Projectiles))Bits("owner",g.m_iESP,{{"Owner name",ESPEnum::Owner}});
                        if(g.m_iTargets&TargetsEnum::Buildings)Bits("buildings",g.m_iESP,{{"Upgrade level",ESPEnum::Level},{"Ammo bars",ESPEnum::AmmoBars},{"Ammo numbers",ESPEnum::AmmoText}});
                        if(g.m_iTargets&TargetsEnum::Objective)Bits("objective",g.m_iESP,{{"Intel return timer",ESPEnum::IntelReturnTime}});
                        Bits("flags",g.m_iESP,{{"Status flags",ESPEnum::Flags}});
                    }
                    if(Card c{"Colours & readability"};c)
                    {
                        ColourEdit("Group colour / opacity",g.m_tColor);Checkbox("Player tags override colour",&g.m_bTagsOverrideColor);
                        Checkbox("Custom name colour",&g.m_bCustomNameColor);
                        if(g.m_bCustomNameColor)ColourEdit("Name colour",g.m_tNameColor);
                        Help("These settings are shared with this group's outlines. Material passes have their own tints.");
                    }
                    if(Card c{"Quick starting looks",true};c)
                    {
                        Help("Replace information flags for this group only. Colours, targets, materials and other groups are preserved.");
                        if(Button("Clean")){g.m_bMoonlitInformation=true;g.m_iESP=ESPEnum::Name|ESPEnum::Box|ESPEnum::HealthBar;}
                        if(Button("Detailed")){g.m_bMoonlitInformation=true;g.m_iESP=ESPEnum::Name|ESPEnum::Box|ESPEnum::Distance|ESPEnum::HealthBar|ESPEnum::HealthText|((g.m_iTargets&TargetsEnum::Players)?ESPEnum::WeaponText|ESPEnum::Buffs|ESPEnum::Debuffs:0);}
                    }
                    if(Card c{"Offscreen & pickup information",true};c)
                    {
                        Checkbox("Offscreen arrows",&g.m_bOffscreenArrows);RawSlider("Arrow offset",g.m_iOffscreenArrowsOffset,0,1000);
                        RawSlider("Arrow range (HU)",g.m_flOffscreenArrowsMaxDistance,0.f,5000.f);Checkbox("Pickup timers",&g.m_bPickupTimer);
                    }
                }
                else
                {
                    if(Card c{"Model outlines"};c)
                    {
                        RawSlider("Outline width",g.m_tGlow.Stencil,0,10);
                        RawSlider("Soft glow",g.m_tGlow.Blur,0.f,10.f);
                        Help("Set both to zero to turn off this group's outline.");ColourEdit("Outline / group colour",g.m_tColor);
                    }
                    if(Card c{"Visible material"};c)MaterialLayers("In sight",g.m_tChams.Visible);
                    if(!(g.m_iTargets&(TargetsEnum::ViewmodelWeapon|TargetsEnum::ViewmodelHands)))
                        if(Card c{"Behind-cover material"};c)MaterialLayers("Through walls",g.m_tChams.Occluded);
                    Help("Viewmodel hands and weapons can be selected in the group filter above. Backtrack materials remain in Custom groups.");
                }
                EndDisabled();TableNextColumn();
                const bool active=MasterOn()&&VisualGroup<32&&(uint32_t(FGet(Vars::ESP::ActiveGroups))&(1u<<VisualGroup));
                (void)active;EndTable();
            }
        }
    }
    else if(BeginTable("VisualSettings",MoonlitBinding::Columns(GetContentRegionAvail().x,Vars::Menu::Scale.Value),ImGuiTableFlags_SizingStretchSame))
    {
        TableNextColumn();
        if(VisualPage==2)
        {
            if(Card c{"Removals"};c){Help("Enabled means that the named effect is removed.");Setting(V::Removals::Scope);Setting(V::Removals::PostProcessing);Setting(V::Removals::ScreenOverlays);Setting(V::Removals::ScreenEffects);Setting(V::Removals::ViewPunch);Setting(V::Removals::AngleForcing);}
            if(Card c{"Players & debris",true};c){Setting(V::Removals::Disguises);Setting(V::Removals::Taunts);Setting(V::Removals::Ragdolls);Setting(V::Removals::Gibs);Setting(V::Removals::MOTD);}
            if(Card c{"Interpolation removals",true};c){Help("Advanced: these also affect entity interpolation, not only appearance.");Setting(V::Removals::Interpolation);Setting(V::Removals::Lerp);Setting(V::Animations::Interpolation,"Checked means remove animation interpolation.");}
            TableNextColumn();
            if(Card c{"World atmosphere"};c){SettingChoice(V::World::Modulations,"Enable the surfaces you want to tint. Darker colours also reduce brightness.");SettingColour(Vars::Colors::WorldModulation);SettingColour(Vars::Colors::SkyModulation);SettingColour(Vars::Colors::PropModulation);SettingString(V::World::SkyboxChanger);SettingString(V::World::WorldTexture);
                SettingChoice(V::World::DapperSky,"Full-bright repeating photos on the six sky faces. Independent of world photos; takes priority over Skybox changer. 3D sky scenery stays native. Clean screenshots use the normal sky.");
                if(FGet(V::World::DapperSky,true))SettingSlider(V::World::DapperSkyRepeat);
                SettingChoice(V::World::DapperPhoto,"Full-bright repeating photos on opaque world surfaces, including ordinary proxy-backed walls. Water, sky and transparent/cutout surfaces are preserved.");
                if(FGet(V::World::DapperPhoto,true)){Setting(V::World::DapperProps);SettingSlider(V::World::DapperRepeat);}}
            if(Card c{"Particles, fog & props",true};c){SettingColour(Vars::Colors::ParticleModulation);SettingColour(Vars::Colors::FogModulation);Setting(V::World::NearPropFade);Setting(V::World::NoPropFade);}
            if(Card c{"Weapon & player effects",true};c){SettingString(V::Effects::BulletTracer);SettingString(V::Effects::CritTracer);SettingString(V::Effects::MedigunBeam);SettingString(V::Effects::MedigunCharge);SettingString(V::Effects::ProjectileTrail);SettingChoice(V::Effects::SpellFootsteps);SettingColour(Vars::Colors::SpellFootstep);SettingChoice(V::Effects::RagdollEffects);Setting(V::Effects::DrawIconsThroughWalls);Setting(V::Effects::DrawDamageNumbersThroughWalls);}
        }
        else if(VisualPage==3)
        {
            if(Card c{"HUD panels"};c){SettingChoice(Vars::Menu::Indicators);Setting(Vars::Menu::BindWindow);Setting(Vars::Menu::BindWindowTitle);Setting(Vars::Menu::BindWindowHorizontal);Help("Move the panels with the menu open. Radar can hide the corresponding standalone indicators.");}
            if(Card c{"Spectator panel",true};c){SettingChoice(Vars::Menu::SpectatorScope);SettingChoice(Vars::Menu::SpectatorLayout);Setting(Vars::Menu::SpectatorGroup);SettingChoice(Vars::Menu::SpectatorStates);SettingChoice(Vars::Menu::SpectatorViews);Setting(Vars::Menu::SpectatorTargets);Setting(Vars::Menu::SpectatorLabels);Setting(Vars::Menu::SpectatorRespawn);SettingSlider(Vars::Menu::SpectatorPage);if(Button("Reset spectator size")){FSet(Vars::Menu::SpectatorWidth,0);FSet(Vars::Menu::SpectatorHeight,0);}Help("Drag a spectator-panel corner to resize it.");}
            if(Card c{"Privacy & scoreboard",true};c){SettingChoice(V::UI::StreamerMode);SettingChoice(V::UI::ChatTags);SettingColour(Vars::Colors::Local);Setting(V::UI::RevealScoreboard);Setting(V::UI::ScoreboardUtility);Setting(V::UI::ScoreboardColors);Setting(V::UI::CleanScreenshots);}
            if(Card c{"Notifications",true};c){SettingChoice(Vars::Logging::Logs);SettingChoice(Vars::Logging::NotificationPosition);SettingSlider(Vars::Logging::NotificationTime);SettingSlider(Vars::Logging::MaxNotifications);}
            if(Card c{"Notification destinations",true};c){SettingChoice(Vars::Logging::VoteStart::LogTo);SettingChoice(Vars::Logging::VoteCast::LogTo);SettingChoice(Vars::Logging::ClassChange::LogTo);SettingChoice(Vars::Logging::Damage::LogTo);SettingChoice(Vars::Logging::CheatDetection::LogTo);SettingChoice(Vars::Logging::Tags::LogTo);SettingChoice(Vars::Logging::Aliases::LogTo);SettingChoice(Vars::Logging::Resolver::LogTo);}
            TableNextColumn();
            if(Card c{"Radar"};c){SettingActivation(V::Radar::Enabled,MoonlitBinding::Dangersense);SettingChoice(V::Radar::Mode);SettingChoice(V::Radar::Shape);SettingChoice(V::Radar::Position);SettingChoice(V::Radar::Orientation);SettingSlider(V::Radar::Size);SettingSlider(V::Radar::Range);SettingChoice(V::Radar::Players);}
            if(Card c{"Radar details",true};c){Setting(V::Radar::Markers);Setting(V::Radar::ClassIcons);Setting(V::Radar::Names);Setting(V::Radar::Health);Setting(V::Radar::Height);Setting(V::Radar::Distance);Setting(V::Radar::Local);Setting(V::Radar::Rings);Setting(V::Radar::Outline);Setting(V::Radar::Crit);Setting(V::Radar::Ticks);Setting(V::Radar::Binds);Setting(V::Radar::BindBackground);Setting(V::Radar::HideStandalone);Setting(V::Radar::ShowBindKey);SettingChoice(V::Radar::BarStyle);}
            if(Card c{"Radar sizing & position",true};c){SettingSlider(V::Radar::OffsetX);SettingSlider(V::Radar::OffsetY);SettingSlider(V::Radar::IconScale);SettingSlider(V::Radar::NameScale);SettingSlider(V::Radar::DistanceScale);SettingSlider(V::Radar::HeightScale);SettingSlider(V::Radar::HealthWidthScale);SettingSlider(V::Radar::HealthHeightScale);SettingSlider(V::Radar::HeightTolerance);SettingSlider(V::Radar::Opacity);}
            if(Card c{"Radar colours",true};c){Setting(V::Radar::InterfaceColors);Setting(V::Radar::GroupColors);const bool previous=FGet(V::Radar::InterfaceBorder);Setting(V::Radar::InterfaceBorder);if(!previous&&FGet(V::Radar::InterfaceBorder))FSet(V::Radar::Border,V::Radar::Border.Default);SettingColour(V::Radar::Background);if(SettingColour(V::Radar::Border))FSet(V::Radar::InterfaceBorder,false);SettingColour(V::Radar::Text);SettingColour(V::Radar::LocalColor);SettingColour(V::Radar::Enemy);SettingColour(V::Radar::Team);SettingColour(V::Radar::HealthColor);SettingColour(V::Radar::CritColor);SettingColour(V::Radar::TickColor);SettingColour(V::Radar::BindActive);SettingColour(V::Radar::BindInactive);}
        }
        else if(VisualPage==4)
        {
            if(Card c{"Camera"};c){SettingSlider(V::UI::FieldOfView,"Zero keeps the game's field of view.");SettingSlider(V::UI::ZoomFieldOfView);Setting(V::Thirdperson::Enabled,nullptr,true);Setting(V::Thirdperson::Crosshair,nullptr,false,"Thirdperson Crosshair");SettingSlider(V::Thirdperson::Distance);SettingSlider(V::Thirdperson::Right);SettingSlider(V::Thirdperson::Up);}
            if(Vars::Debug::Options.Value)if(Card c{"Camera debug",true};c){Setting(V::Thirdperson::Scale);Setting(V::Thirdperson::Collide);}
            TableNextColumn();
            if(Card c{"Weapon position"};c){SettingSlider(V::Viewmodel::OffsetX);SettingSlider(V::Viewmodel::OffsetY);SettingSlider(V::Viewmodel::OffsetZ);}
            if(Card c{"Weapon rotation & sway",true};c){SettingSlider(V::Viewmodel::Pitch);SettingSlider(V::Viewmodel::Yaw);SettingSlider(V::Viewmodel::Roll);SettingSlider(V::Viewmodel::SwayScale);SettingSlider(V::Viewmodel::SwayInterp);}
            if(Card c{"Aim presentation",true};c){Setting(V::Viewmodel::CrosshairAim);Setting(V::Viewmodel::ViewmodelAim);Setting(V::Viewmodel::CrosshairCooldown);Setting(V::Viewmodel::ViewmodelCooldown);}
            Help("Weapon and hand materials are in Outlines & materials > Viewmodels. These controls use existing features, not mockup-only settings.");
            if(Card c{"Aimbot visualizers"};c)
            {
                Setting(Vars::AimModes::CircleEnabled);Setting(Vars::AimModes::CircleOverride);
                if(SettingColour(Vars::AimModes::CircleColour))FSet(Vars::AimModes::CircleOverride,true);
                Help("With colour override off, the active aim mode supplies its own circle colour.");
            }
        }
        EndTable();
    }
    if(VisualPage==4&&CollapsingHeader("Aimbot visualizers - draw settings"))MenuAimbot(1);
    if(VisualPage==3 && CollapsingHeader("Advanced interface & log controls"))
    {
        Help("Complete existing interface editor, including scale, fonts, menu keys, colours and logging. These are shared preferences.");
        MenuVisuals(2);
    }
    EndDisabled();
    if(Capturing)TextColored(Gold,"Press a key... Escape cancels.");
    if(!BindStatus.empty())Help(BindStatus.c_str());
    Help("Save your changes in Configs & binds. Nullcore keeps its original layout.");
}
