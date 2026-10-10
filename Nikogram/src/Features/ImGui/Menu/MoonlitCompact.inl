// The eight-page Nikogram layout. This file does not replace Nullcore's pages.
namespace MoonlitUI
{
    enum class Editor { None, Aim, Visuals, Groups, Misc, Binds, Materials, Interface, Logs, Statistics, Players, Developer, AimDraw };
    inline Editor RequestedEditor=Editor::None, ActiveEditor=Editor::None;
    inline bool ResetCompactLayout=false;
    inline void More(const char* label,Editor editor,int tab=-1)
    {
        if(Button(label,{-1,0}))
        {
            RequestedEditor=editor;
            if(tab>=0){if(editor==Editor::Visuals)VisualPage=tab;else if(editor==Editor::Misc)MiscPage=tab;}
            CancelCapture();
        }
    }
    struct CompactPanel
    {
        bool visible;
        CompactPanel(const char* name,float height=0)
        {
            PushID(name);visible=BeginChild("panel",{0,height},ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding);
            if(visible){TextColored(Gold,"%s",name);Separator();}
        }
        ~CompactPanel(){EndChild();PopID();}
        explicit operator bool()const{return visible;}
    };
    struct CompactColumns
    {
        bool open;float height;
        CompactColumns(const char* id):height(std::max(1.f,GetContentRegionAvail().y-GetStyle().CellPadding.y*2))
        {
            open=BeginTable(id,2,ImGuiTableFlags_SizingStretchSame);
            if(open){TableNextColumn();BeginChild("left",{0,height});}
        }
        void Next(){EndChild();TableNextColumn();BeginChild("right",{0,height});}
        ~CompactColumns(){if(open){EndChild();EndTable();}}
        explicit operator bool()const{return open;}
    };
    inline void Heading(const char* text){Spacing();TextColored(Gold,"%s",text);Separator();}
    inline void ClassScope()
    {
        const char* classes[]={"Scout","Soldier","Pyro","Demoman","Heavy","Engineer","Medic","Sniper","Spy"};
        static int chosen=0;
        SetNextItemWidth(190*Vars::Menu::Scale.Value);
        const auto& binds=F::Binds.m_vBinds;
        const char* current=CurrentBind>=0&&CurrentBind<int(binds.size())?binds[CurrentBind].m_sName.c_str():"All classes (base)";
        if(BeginCombo("##class-scope",current))
        {
            if(Selectable("All classes (base)",CurrentBind==DEFAULT_BIND))CurrentBind=DEFAULT_BIND;
            for(int i=0;i<int(binds.size());++i)if(binds[i].m_iType==BindEnum::Class)
            {PushID(i);if(Selectable(binds[i].m_sName.c_str(),CurrentBind==i))CurrentBind=i;PopID();}
            EndCombo();
        }
        Tip("Edit this mode's class overrides. Unchanged values inherit this mode's base; selecting a class does not activate it.");
        SameLine();if(Button("+ Class"))OpenPopup("Add class override");
        if(BeginPopup("Add class override"))
        {
            Combo("Class",&chosen,classes,9);
            TextWrapped("Create a class condition. Change settings to add overrides; unchanged values inherit the base.");
            if(Button("Create"))
            {
                Bind_t b;b.m_sName=std::string(classes[chosen])+" settings";b.m_iType=BindEnum::Class;b.m_iInfo=chosen;
                b.m_iVisibility=BindVisibilityEnum::Hidden;
                CurrentBind=int(F::Binds.m_vBinds.size());F::Binds.m_vBinds.push_back(b);CloseCurrentPopup();
            }
            SameLine();if(Button("Cancel"))CloseCurrentPopup();
            EndPopup();
        }
        Separator();
    }
    inline int AimEditorMode=AimModes::Rage;
    inline void AimStyle(bool legit)
    {
        auto& var=AimModes::Get(Vars::Aimbot::General::AimType,legit?AimModes::Legit:AimModes::Rage);
        auto value=FGet(var,true);BeginDisabled(Disabled);
        TextUnformatted("Aim style");SetNextItemWidth(-1);
        const char* names[]={"Off","Plain","Smooth","Silent","Locking","Assistive"};
        const int displayed=AimModes::NormalizeStyle(legit?AimModes::Legit:AimModes::Rage,value);
        if(BeginCombo("##aim-style",names[displayed]))
        {
            for(int i=1;i<6;++i)
            {
                if(legit?(i!=2&&i!=5):(i!=1&&i!=3&&i!=4))continue;
                if(Selectable(names[i],displayed==i))value=i;
            }
            EndCombo();
        }
        EndDisabled();FSet(var,value);
    }
    inline void InfoBit(const char* name,int& mask,int bit,Color_t* colour=nullptr)
    {
        PushID(name);bool enabled=(mask&bit)!=0;
        if(Checkbox(name,&enabled)){if(enabled)mask|=bit;else mask&=~bit;}
        if(colour)
        {
            SameLine(std::max(GetCursorPosX(),GetWindowContentRegionMax().x-GetFrameHeight()));
            float c[]={colour->r/255.f,colour->g/255.f,colour->b/255.f,colour->a/255.f};
            if(ColorEdit4("##colour",c,ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoLabel|ImGuiColorEditFlags_AlphaBar))
                *colour=Color_t(int(c[0]*255+.5f),int(c[1]*255+.5f),int(c[2]*255+.5f),int(c[3]*255+.5f));
        }
        PopID();
    }
    inline bool ConfigNameOK(const std::string& name)
    {
        if(name.empty()||name.size()>100||name=="."||name==".."||name.back()=='.'||name.back()==' ')return false;
        for(unsigned char c:name)if(c<32||strchr("<>:\"/\\|?*",c))return false;
        auto upper=name.substr(0,name.find('.'));for(auto& c:upper)c=char(toupper(static_cast<unsigned char>(c)));
        if(upper=="CON"||upper=="PRN"||upper=="AUX"||upper=="NUL")return false;
        if(upper.size()==4&&(upper.starts_with("COM")||upper.starts_with("LPT"))&&upper[3]>='1'&&upper[3]<='9')return false;
        return true;
    }
}

void CMenu::MoonlitAim(bool legit)
{
    using namespace ImGui;using namespace MoonlitUI;namespace A=Vars::Aimbot;
    AimModes::EditScope editing(legit?AimModes::Legit:AimModes::Rage);
    AimEditorMode=legit?AimModes::Legit:AimModes::Rage;
    ClassScope();
    if(CompactColumns columns{legit?"Legit columns":"Rage columns"};columns)
    {
        {CompactPanel panel{"Aimbot"};if(panel)
        {
            ActivationRow(legit?Vars::AimModes::LegitEnabled:Vars::AimModes::RageEnabled,legit?Vars::AimModes::LegitActivation:Vars::AimModes::RageActivation,"Enabled");
            // Branching only reads inherited values. Styled reads are reserved
            // for editable controls, which pair them with FSet to pop the style.
            const bool combined=legit&&FGet(AimModes::Resolve(A::General::CombinedGuidance));
            if(!combined)AimStyle(legit);
            if(legit)
            {
                const int style=FGet(AimModes::Resolve(A::General::AimType));
                if(combined)
                {
                    SettingSlider(A::General::SmoothAmount,"Automatic gentle correction, 0 to 10. Zero removes Smooth's contribution.");
                    SettingSlider(A::General::AssistAmount,"Mouse-driven hitbox guidance. Yields to fast flicks and deliberate movement away; no Assistive pull while the mouse is idle.");
                }
                if(combined||style==A::General::AimTypeEnum::Smooth)
                {
                    if(!combined)SettingChoice(A::General::SmoothFormula,"Default preserves the old fraction-per-update method. Damped eases into movement with bounded turning speed and acceleration.");
                    if(combined||FGet(AimModes::Resolve(A::General::SmoothFormula))==A::General::SmoothFormulaEnum::Damped)
                    {
                        SettingSlider(A::General::SmoothTime,"Higher values give a slower, gentler response. This is a response scale, not a fixed delay before firing.","Smoothness (ms)");
                    }
                    else SettingSlider(A::General::AssistStrength,"Legacy movement fraction per update. 100% snaps directly.","Default strength");
                }
                else SettingSlider(A::General::AssistStrength,"Strength of the existing mouse-driven Assistive style.");
            }
            SettingSlider(A::General::TargetSwitchDelay,"Pause before moving or automatically firing at a different target. First acquisition is immediate; zero restores instant switching.");
            Setting(A::General::DynamicTargetSwitch,"Sample one delay per handoff using the same bounds as Triggerbot. Candidate changes do not restart the timer.");
            if(FGet(AimModes::Resolve(A::General::DynamicTargetSwitch)))
            {SettingSlider(A::General::TargetSwitchMin);SettingSlider(A::General::TargetSwitchMax);}
            SettingSlider(A::General::AimFOV,nullptr,"Aim field of view");SettingSlider(A::Hitscan::MultipointScale);
            SettingChoice(A::General::TargetSelection);
            SettingChoice(A::Hitscan::Hitboxes);SettingChoice(A::Hitscan::MultipointHitboxes);
            Heading("Automatic firing");Setting(A::General::AutoShoot,nullptr,true);
            Heading("Advanced targeting");SettingChoice(A::General::Target);SettingChoice(A::General::Ignore);
            SettingChoice(A::General::LeadAndRestrict);SettingSlider(A::General::MaxTargets);
            SettingSlider(Vars::Backtrack::Window,"Backtrack window for this aim mode. Legitbot defaults to zero.","Backtrack (ms)");
            More("More aim settings...",Editor::Aim);
        }}
        columns.Next();
        const float split=std::max(200.f*Vars::Menu::Scale.Value,columns.height*.57f);
        if(legit)
        {
            {CompactPanel p{"Triggerbot",split};if(p)
            {
                ActivationRow(Vars::Triggerbot::Enabled,Vars::Triggerbot::Activation,"Enabled");
                SettingChoice(Vars::Triggerbot::Hitboxes,"Off disables Triggerbot. It operates independently of aim assistance.");
                SettingSlider(Vars::Triggerbot::Delay);Setting(Vars::Triggerbot::DynamicDelay);
                if(FGet(Vars::Triggerbot::DynamicDelay)){SettingSlider(Vars::Triggerbot::DelayMin);SettingSlider(Vars::Triggerbot::DelayMax);}
                SettingChoice(Vars::Triggerbot::Backtrack);Setting(Vars::Triggerbot::BacktrackToCursor);
            }}
            {CompactPanel p{"Other"};if(p)
            {
                SettingActivation(Vars::AntiAim::LegitEnabled,MoonlitBinding::OnLethal,"On threat");
                TextDisabled("Automatic class / weapon rules");
                Tip("Independent of the aimbot key. Overrides regular AA while enabled. No Minwalk or pitch changes. Fake Forward, Real Left by default; Sniper rifles use Real Right. Off for Medic Mediguns, Sniper SMGs, Spy revolvers/sappers, and unlisted Sniper/Spy weapons. Right-click the bind for On threat: any enemy Sniper with clear line of sight, or other estimated lethal danger.");
                SettingChoice(A::Hitscan::Modifiers);Setting(A::General::FOVCircle);SettingColour(Vars::Colors::FOVCircle);SettingSlider(A::General::IgnoreInvisible);SettingSlider(A::General::TickTolerance);
            }}
        }
        else
        {
            {CompactPanel p{"Projectile & splash",split};if(p)
            {
                SettingSlider(A::Projectile::AimFOV,nullptr,"Projectile field of view");
                SettingChoice(A::Projectile::SplashPrediction);SettingChoice(A::Projectile::SplashMode);
                SettingChoice(A::Projectile::Hitboxes);SettingChoice(A::Projectile::Modifiers);SettingChoice(A::Projectile::StrafePrediction);
                SettingSlider(A::Projectile::SplashRadius);SettingSlider(A::Projectile::MaxSimulationTime);SettingSlider(A::Projectile::HitChance);
            }}
            {CompactPanel p{"Other"};if(p)
            {
                Setting(A::General::NoSpread);Setting(Vars::CritHack::ForceCrits,nullptr,true);Setting(Vars::CritHack::AvoidRandomCrits);
                Setting(Vars::Doubletap::Doubletap,nullptr,true);Setting(Vars::Doubletap::Warp,nullptr,true);Setting(Vars::Doubletap::RechargeTicks,nullptr,true);
                SettingChoice(A::Projectile::AutoDetonate);SettingChoice(A::Projectile::AutoAirblast);
                Setting(A::General::FOVCircle);SettingColour(Vars::Colors::FOVCircle);
            }}
        }
    }
}

void CMenu::MoonlitAntiAim()
{
    using namespace ImGui;using namespace MoonlitUI;namespace A=Vars::AntiAim;
    if(CompactColumns c{"Anti aim columns"};c)
    {
        {CompactPanel p{"Anti-aim angles"};if(p)
        {
            SettingActivation(A::Enabled,MoonlitBinding::OnLethal);SettingChoice(A::PitchReal);SettingChoice(A::PitchFake);
            SettingChoice(A::RealYawBase);SettingChoice(A::YawReal);SettingSlider(A::RealYawOffset);
            if(FGet(A::YawReal,true)==A::YawEnum::Jitter)SettingSlider(A::RealYawValue);
            SettingChoice(A::FakeYawBase);SettingChoice(A::YawFake);SettingSlider(A::FakeYawOffset);
            if(FGet(A::YawFake,true)==A::YawEnum::Jitter)SettingSlider(A::FakeYawValue);
            SettingSlider(A::SpinSpeed);Setting(A::MinWalk);Setting(A::HidePitchOnShot);
            Setting(A::FirstPersonRing);if(FGet(A::FirstPersonRing,true))SettingSlider(A::RingOffset);
        }}
        c.Next();
        {CompactPanel p{"Fake lag",c.height*.48f};if(p)
        {
            SettingChoice(Vars::Fakelag::Fakelag);SettingChoice(Vars::Fakelag::Options);SettingSlider(Vars::Fakelag::PlainTicks);
            auto ticks=FGet(Vars::Fakelag::RandomTicks,true);BeginDisabled(Disabled);
            TextUnformatted("Random minimum");if(Numeric("minimum",ticks.Min,1,22,"%i",SLIDER_CLAMP))ticks.Max=std::max(ticks.Min,ticks.Max);
            TextUnformatted("Random maximum");if(Numeric("maximum",ticks.Max,1,22,"%i",SLIDER_CLAMP))ticks.Min=std::min(ticks.Min,ticks.Max);
            EndDisabled();FSet(Vars::Fakelag::RandomTicks,ticks);
            Setting(Vars::Fakelag::UnchokeOnAttack);Setting(Vars::Fakelag::RetainBlastJump);
        }}
        {CompactPanel p{"Other"};if(p)
        {
            Setting(Vars::AutoPeek::Enabled,nullptr,true);Setting(Vars::Doubletap::AntiWarp);SettingSlider(Vars::Doubletap::TickLimit);
            Setting(Vars::Resolver::Enabled);Setting(Vars::Resolver::AutoResolve);Setting(Vars::Resolver::AutoResolveCheatersOnly);
            SettingSlider(Vars::Resolver::AutoResolveYawAmount);SettingSlider(Vars::Resolver::AutoResolvePitchAmount);
        }}
    }
}

void CMenu::MoonlitESP()
{
    using namespace ImGui;using namespace MoonlitUI;namespace V=Vars::Visuals;
    // Filtering selects an existing group only; no preset replaces the user's rules.
    SetNextItemWidth(145*Vars::Menu::Scale.Value);
    if(BeginCombo("##scope",ScopeNames[VisualScope])){for(int i=0;i<6;++i)if(Selectable(ScopeNames[i],VisualScope==i))VisualScope=i;EndCombo();}
    auto& groups=F::Groups.m_vGroups;
    if(VisualGroup<0||VisualGroup>=int(groups.size())||!InScope(groups[VisualGroup],VisualScope))
    {VisualGroup=-1;for(int i=int(groups.size())-1;i>=0;--i)if(InScope(groups[i],VisualScope)){VisualGroup=i;break;}}
    SameLine();SetNextItemWidth(195*Vars::Menu::Scale.Value);
    if(BeginCombo("##group",VisualGroup>=0?groups[VisualGroup].m_sName.c_str():"No matching group"))
    {for(int i=0;i<int(groups.size());++i)if(InScope(groups[i],VisualScope)){PushID(i);if(Selectable(groups[i].m_sName.c_str(),VisualGroup==i))VisualGroup=i;PopID();}EndCombo();}
    SameLine();if(Button("Custom groups..."))RequestedEditor=Editor::Groups;
    Tip("Create rules, choose targets and conditions, or change group priority.");Separator();
    if(CompactColumns c{"ESP columns"};c)
    {
        {CompactPanel p{"Player ESP",c.height*.60f};if(p)
        {
            ActivationRow(Vars::ESP::MoonlitMaster,Vars::ESP::MoonlitEnabled,"ESP enabled");
            if(VisualGroup>=0)
            {
                int mask=FGet(Vars::ESP::ActiveGroups,true);bool enabled=VisualGroup<32&&(uint32_t(mask)&(1u<<VisualGroup));
                BeginDisabled(Disabled||VisualGroup>=32);
                if(Checkbox("Group enabled",&enabled))mask=int(enabled?uint32_t(mask)|(1u<<VisualGroup):uint32_t(mask)&~(1u<<VisualGroup));
                EndDisabled();FSet(Vars::ESP::ActiveGroups,mask);
                auto& g=groups[VisualGroup];BeginDisabled(CurrentBind!=DEFAULT_BIND);
                Checkbox("Show information",&g.m_bMoonlitInformation);
                if(g.m_iTargets&TargetsEnum::Players)
                {
                    TextUnformatted("Dapper Mann ESP");SetNextItemWidth(-1);
                    Combo("##dapperesp",&g.m_iDapperESP,"Off\0Dapper Mann\0Dapper Mann - Money\0Giga Mann\0");
                    Tip("Stretch the selected photo across the player bounding box, behind other ESP information.");
                }
                InfoBit("Bounding box",g.m_iESP,ESPEnum::Box,&g.m_tColor);InfoBit("Name",g.m_iESP,ESPEnum::Name,g.m_bCustomNameColor?&g.m_tNameColor:nullptr);
                InfoBit("Health bar",g.m_iESP,ESPEnum::HealthBar);InfoBit("Health number",g.m_iESP,ESPEnum::HealthText);
                InfoBit("Weapon text",g.m_iESP,ESPEnum::WeaponText);InfoBit("Weapon icon",g.m_iESP,ESPEnum::WeaponIcon);
                InfoBit("Skeleton",g.m_iESP,ESPEnum::Bones);InfoBit("Distance",g.m_iESP,ESPEnum::Distance);InfoBit("Flags",g.m_iESP,ESPEnum::Flags);
                Checkbox("Offscreen arrows",&g.m_bOffscreenArrows);EndDisabled();
            }
            else TextWrapped("No matching group. Create one in Custom groups.");
        }}
        {CompactPanel p{"Model colours & materials"};if(p)
        {
            if(VisualGroup>=0)
            {
                auto& g=groups[VisualGroup];BeginDisabled(CurrentBind!=DEFAULT_BIND);
                MaterialLayers("Visible",g.m_tChams.Visible);
                if(!(g.m_iTargets&(TargetsEnum::ViewmodelWeapon|TargetsEnum::ViewmodelHands)))MaterialLayers("Behind walls",g.m_tChams.Occluded);
                RawSlider("Outline width",g.m_tGlow.Stencil,0,10);RawSlider("Soft glow",g.m_tGlow.Blur,0.f,10.f);EndDisabled();
            }
            More("More group settings...",Editor::Groups);
        }}
        c.Next();
        {CompactPanel p{"Other ESP",c.height*.37f};if(p)
        {
            SettingActivation(V::Radar::Enabled,MoonlitBinding::Dangersense);SettingChoice(V::Radar::Mode);SettingChoice(Vars::Menu::Indicators);
            Setting(Vars::Menu::BindWindow);Setting(Vars::Menu::SpectatorTargets);
            More("HUD & radar settings...",Editor::Visuals,3);
        }}
        {CompactPanel p{"World & effects"};if(p)
        {
            Setting(V::Removals::Scope,nullptr,false,"Remove scope overlay");Setting(V::Removals::PostProcessing,"Clean screenshots keeps native exposure/post processing active to prevent brightness changes.",false,"Remove post processing");
            Setting(V::Removals::ScreenEffects,nullptr,false,"Remove screen effects");Setting(V::Removals::ViewPunch,nullptr,false,"Remove view punch");
            Setting(V::Thirdperson::Enabled,nullptr,true);Setting(V::Thirdperson::Crosshair,nullptr,false,"Thirdperson Crosshair");
            SettingSlider(V::UI::FieldOfView);SettingSlider(V::UI::ZoomFieldOfView);
            More("World, removals & effects...",Editor::Visuals,2);More("Camera & viewmodel...",Editor::Visuals,4);
            More("Aimbot visualizers...",Editor::AimDraw);
        }}
    }
}

void CMenu::MoonlitMisc()
{
    using namespace ImGui;using namespace MoonlitUI;namespace M=Vars::Misc;
    if(CompactColumns c{"Misc columns"};c)
    {
        {CompactPanel p{"Miscellaneous"};if(p)
        {
            Setting(M::Automation::AntiAFK);Setting(M::Automation::AcceptItemDrops);SettingChoice(M::Automation::AntiBackstab);
            Setting(M::Automation::TauntControl,nullptr,true);Setting(M::Automation::KartControl,nullptr,true);
            Setting(M::Blockbot::Enabled,nullptr,true);Setting(M::Sound::HitsoundAlways);Setting(M::Sound::RemoveDSP);
            Heading("Freelook");
            PendingModes.try_emplace(&M::Freelook::Enabled,BindEnum::KeyEnum::Hold);
            Setting(M::Freelook::Enabled,"Hold to move the camera without changing your normal aim or movement direction.",true);
            More("Freelook settings...",Editor::Misc,1);
            Heading("OptiFine Zoom");
            PendingModes.try_emplace(&M::OptifineZoom::Enabled,BindEnum::KeyEnum::Hold);
            Setting(M::OptifineZoom::Enabled,"Assign a key; Hold is the default. Release restores the current normal or scoped view.",true);
            SettingSlider(M::OptifineZoom::Magnification);
            More("Zoom settings...",Editor::Misc,1);
            Heading("Utilities");
            if(Button("Console",{-1,0}))I::EngineClient->ClientCmd_Unrestricted("toggleconsole");
            if(Button("Full update",{-1,0}))I::EngineClient->ClientCmd_Unrestricted("cl_fullupdate");
            if(Button("Reconnect...",{-1,0}))OpenPopup("Reconnect?");
            if(BeginPopupModal("Reconnect?",nullptr,ImGuiWindowFlags_AlwaysAutoResize))
            {TextUnformatted("Disconnect and reconnect to this server?");if(Button("Reconnect")){I::EngineClient->ClientCmd_Unrestricted("retry");CloseCurrentPopup();}SameLine();if(Button("Cancel"))CloseCurrentPopup();EndPopup();}
            More("Automation & game utilities...",Editor::Misc,1);More("Players & blocking...",Editor::Misc,2);
            More("Queue & sound...",Editor::Misc,3);More("Logs...",Editor::Logs);More("Statistics...",Editor::Statistics);
        }}
        c.Next();
        {CompactPanel p{"Movement",c.height*.53f};if(p)
        {
            Setting(M::Movement::Bunnyhop,nullptr,true);SettingChoice(M::Movement::AutoStrafe);Setting(M::Movement::EdgeJump,nullptr,true);
            Setting(M::Movement::FastStop);Setting(M::Movement::DuckSpeed);Setting(M::Movement::NoPush);
            Setting(M::Movement::AutoRocketJump,nullptr,true);Setting(M::Movement::AutoCTap,nullptr,true);
            More("More movement settings...",Editor::Misc,0);
        }}
        {CompactPanel p{"Settings"};if(p)
        {
            DrawMenuStyleChoice();bool changed=false;
            changed|=Checkbox("Shimejis",&Workspace::PetEnabled);changed|=Checkbox("Subtle transitions",&MenuMode::Animations);
            changed|=Checkbox("Smooth sliders",&MenuMode::SmoothSliders);
            if(changed)m_sInterfaceStatus=Workspace::Save()?"Appearance saved.":"Could not save appearance.";
            Setting(Vars::Visuals::UI::CleanScreenshots,"Hides custom draws for screenshots. Keeps native post processing active on every frame to preserve exposure.");
            More("Interface settings...",Editor::Interface);More("Compatibility & utilities...",Editor::Misc,4);
            if(Button("Reset menu layout",{-1,0}))ResetCompactLayout=true;
            if(Button("Hide menu",{-1,0})){m_bIsOpen=false;I::MatSystemSurface->SetCursorAlwaysVisible(false);}
        }}
    }
}

void CMenu::MoonlitPlayers()
{
    using namespace ImGui;using namespace MoonlitUI;
    std::lock_guard lock(m_tMutex);static int selected=-1;
    const auto& players=F::PlayerUtils.m_vPlayerCache;
    if(CompactColumns c{"Player columns"};c)
    {
        {CompactPanel p{"Players"};if(p)
        {
            if(!I::EngineClient->IsInGame())TextDisabled("Join a game to see players.");
            else for(const auto& player:players){PushID(player.m_iUserID);if(Selectable(player.m_sName.c_str(),selected==player.m_iUserID))selected=player.m_iUserID;PopID();}
        }}
        c.Next();
        {CompactPanel p{"Adjustments"};if(p)
        {
            const auto found=std::find_if(players.begin(),players.end(),[&](const auto& x){return x.m_iUserID==selected;});
            if(!I::EngineClient->IsInGame()||found==players.end())TextDisabled("Select a player on the left.");
            else
            {
                const auto& player=*found;
                static std::string aliasDraft;
                BeginDisabled(player.m_bFake||!player.m_uAccountID);
                if(Button((player.m_sName+"###edit-alias").c_str(),{-1,0}))
                {
                    auto alias=F::PlayerUtils.m_mPlayerAliases.find(player.m_uAccountID);
                    aliasDraft=alias==F::PlayerUtils.m_mPlayerAliases.end()?"":alias->second;
                    OpenPopup("Edit alias");
                }
                Tip("Click the name to add or change an alias. Empty removes the alias.");
                if(BeginPopup("Edit alias"))
                {
                    SetNextItemWidth(240*Vars::Menu::Scale.Value);
                    const bool enter=InputTextWithHint("##alias","Alias...",&aliasDraft,ImGuiInputTextFlags_EnterReturnsTrue);
                    if(Button("Save")||enter)
                    {
                        if(aliasDraft.empty())F::PlayerUtils.m_mPlayerAliases.erase(player.m_uAccountID);
                        else F::PlayerUtils.m_mPlayerAliases[player.m_uAccountID]=aliasDraft;
                        F::PlayerUtils.m_bSave=true;CloseCurrentPopup();
                    }
                    SameLine();if(Button("Cancel"))CloseCurrentPopup();EndPopup();
                }
                EndDisabled();
                if(auto alias=F::PlayerUtils.m_mPlayerAliases.find(player.m_uAccountID);alias!=F::PlayerUtils.m_mPlayerAliases.end())TextWrapped("Alias: %s",alias->second.c_str());
                TextDisabled("%s | %d points | %d deaths",player.m_bFake?"Bot":"Player",player.m_iScore,player.m_iDeaths);Separator();
                if(Button(F::Spectate.GetTarget(true)==selected?"Stop spectating":"Spectate",{-1,0}))F::Spectate.SetTarget(selected);
                BeginDisabled(player.m_bLocal||I::EngineClient->IsPlayingDemo());
                if(Button("Set blockbot target",{-1,0}))
                {F::Blockbot.SetManual(selected,player.m_uAccountID,player.m_sName);Vars::Misc::Blockbot::Target.Map[DEFAULT_BIND]=Vars::Misc::Blockbot::Target.Value=Vars::Misc::Blockbot::TargetEnum::Manual;}
                EndDisabled();Heading("Tags");
                BeginDisabled(player.m_bFake);
                for(int i=0;i<int(F::PlayerUtils.m_vTags.size());++i)
                {
                    const auto& tag=F::PlayerUtils.m_vTags[i];if(!tag.m_bAssignable)continue;
                    bool on=F::PlayerUtils.HasTag(player.m_uAccountID,i);PushID(i);
                    if(Checkbox(tag.m_sName.c_str(),&on)){if(on)F::PlayerUtils.AddTag(player.m_uAccountID,i,true,player.m_sName.c_str());else F::PlayerUtils.RemoveTag(player.m_uAccountID,i,true,player.m_sName.c_str());}PopID();
                }
                EndDisabled();if(player.m_bFake)TextDisabled("Persistent tags require a Steam account.");
            }
            More("Full player tools...",Editor::Players);
        }}
    }
}

void CMenu::MoonlitConfigs()
{
    using namespace ImGui;using namespace MoonlitUI;
    static std::string selected,name,status,pending;static bool visual=false,pendingVisual=false;static int action=0;
    if(CompactColumns c{"Config columns"};c)
    {
        {CompactPanel p{"Presets"};if(p)
        {
            if(Checkbox("Visual-only configs",&visual)){selected.clear();name.clear();}
            const auto& path=visual?F::Configs.m_sVisualsPath:F::Configs.m_sConfigPath;
            const auto& current=visual?F::Configs.m_sCurrentVisuals:F::Configs.m_sCurrentConfig;
            TextDisabled("Current: %s",current.c_str());
            if(BeginListBox("##configs",{-1,std::max(110.f,c.height*.37f)}))
            {
                for(const auto& entry:GetDirectoryEntries(path))
                {
                    std::error_code error;if(!entry.is_regular_file(error)||entry.path().extension()!=F::Configs.m_sConfigExtension)continue;
                    std::string candidate;try{candidate=entry.path().stem().string();}catch(...){continue;}
                    if(Selectable(candidate.c_str(),selected==candidate)){selected=name=candidate;}
                }
                EndListBox();
            }
            SetNextItemWidth(-1);InputTextWithHint("##name","Config name...",&name);
            BeginDisabled(selected.empty());
            if(Button("Load",{-1,0}))
            {CancelCapture();const bool ok=visual?F::Configs.LoadVisual(selected):F::Configs.LoadConfig(selected);CurrentBind=DEFAULT_BIND;status=ok?"Loaded.":"Load failed.";}
            EndDisabled();BeginDisabled(!ConfigNameOK(name));
            if(Button("Save",{-1,0}))
            {
                pending=name;pendingVisual=visual;std::error_code error;
                if(std::filesystem::exists(path+name+F::Configs.m_sConfigExtension,error)){action=1;OpenPopup("Confirm config action");}
                else{const bool ok=visual?F::Configs.SaveVisual(name):F::Configs.SaveConfig(name);status=ok?"Saved.":"Save failed.";InvalidateDirectoryCache();}
            }
            EndDisabled();BeginDisabled(selected.empty()||(!visual&&selected=="default"));
            if(Button("Delete...",{-1,0})){pending=selected;pendingVisual=visual;action=2;OpenPopup("Confirm config action");}EndDisabled();
            BeginDisabled(selected.empty());
            if(Button("Reset...",{-1,0})){pending=selected;pendingVisual=visual;action=3;OpenPopup("Confirm config action");}EndDisabled();
            if(BeginPopupModal("Confirm config action",nullptr,ImGuiWindowFlags_AlwaysAutoResize))
            {
                TextWrapped("%s '%s'?",action==1?"Overwrite":action==2?"Delete":"Reset",pending.c_str());
                if(Button("Confirm"))
                {
                    if(action==1){const bool ok=pendingVisual?F::Configs.SaveVisual(pending):F::Configs.SaveConfig(pending);status=ok?"Saved.":"Save failed.";}
                    else if(action==2){if(pendingVisual)F::Configs.DeleteVisual(pending);else F::Configs.DeleteConfig(pending);selected.clear();}
                    else{if(pendingVisual)F::Configs.ResetVisual(pending);else F::Configs.ResetConfig(pending);}
                    InvalidateDirectoryCache();CloseCurrentPopup();
                }
                SameLine();if(Button("Cancel"))CloseCurrentPopup();EndPopup();
            }
            TextWrapped("%s",status.c_str());
        }}
        c.Next();
        {CompactPanel p{"Binds & resources"};if(p)
        {
            if(F::Binds.m_vBinds.empty())TextDisabled("No binds. Use a feature's key button to add one.");
            for(int i=0;i<int(F::Binds.m_vBinds.size());++i)
            {
                auto& bind=F::Binds.m_vBinds[i];PushID(i);Checkbox("##on",&bind.m_bEnabled);SameLine();
                if(Selectable(bind.m_sName.c_str(),CurrentBind==i)){CurrentBind=i;}
                Tip("Select to edit this bind's settings on another page. Use the bind editor to change conditions.");PopID();
            }
            Separator();More("Edit binds...",Editor::Binds);More("Material editor...",Editor::Materials);
            if(Vars::Debug::Options.Value)More("Developer tools...",Editor::Developer);
        }}
    }
}

void CMenu::MoonlitEditor()
{
    using namespace ImGui;using namespace MoonlitUI;
    if(RequestedEditor!=Editor::None){ActiveEditor=RequestedEditor;RequestedEditor=Editor::None;OpenPopup("More settings###MoonlitEditor");}
    bool open=true;const float k=Vars::Menu::Scale.Value;
    SetNextWindowSize({std::min(920*k,GetIO().DisplaySize.x-30),std::min(700*k,GetIO().DisplaySize.y-50)},ImGuiCond_Appearing);
    if(BeginPopupModal("More settings###MoonlitEditor",&open))
    {
        if(Button("Done"))CloseCurrentPopup();SameLine();TextDisabled("Changes use your existing config.");Separator();
        if(BeginChild("Editor body"))switch(ActiveEditor)
        {
        case Editor::Aim:
        {
            AimModes::EditScope editing(AimEditorMode);
            static std::string aimSearch;
            SetNextItemWidth(-1);InputTextWithHint("##AimSearch","Search settings (e.g. turning)...",&aimSearch);
            if(aimSearch.empty())MenuAimbot();else MenuSearch(aimSearch);
            break;
        }
        case Editor::AimDraw:
            Setting(Vars::AimModes::CircleEnabled);Setting(Vars::AimModes::CircleOverride);
            if(SettingColour(Vars::AimModes::CircleColour))FSet(Vars::AimModes::CircleOverride,true);
            MenuAimbot(1);break;
        case Editor::Groups:CustomGroups();break;
        case Editor::Visuals:Combo("Section",&VisualPage,VisualNames,5);MenuMoonlitVisuals();break;
        case Editor::Misc:Combo("Section",&MiscPage,MiscNames,5);MenuMoonlitMisc();break;
        case Editor::Binds:MenuSettings(1);break;
        case Editor::Materials:MenuSettings(2);break;
        case Editor::Interface:DrawInterfacePreferences();break;
        case Editor::Logs:MenuLogs(2);break;
        case Editor::Statistics:Statistics::Draw();break;
        case Editor::Players:MenuLogs(0);break;
        case Editor::Developer:MenuSettings(3);break;
        default:break;
        }
        EndChild();EndPopup();
    }
}
