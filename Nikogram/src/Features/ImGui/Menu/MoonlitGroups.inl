// Included after the Moonlit visual control helpers. No runtime matching changes.
namespace MoonlitUI
{
    using GroupChoices=std::initializer_list<std::pair<const char*,int>>;
    inline const GroupChoices GroupTargets={{"Players",TargetsEnum::Players},{"Buildings",TargetsEnum::Buildings},{"Projectiles",TargetsEnum::Projectiles},{"Ragdolls",TargetsEnum::Ragdolls},{"Objective",TargetsEnum::Objective},{"NPCs",TargetsEnum::NPCs},{"Health",TargetsEnum::Health},{"Ammo",TargetsEnum::Ammo},{"Money",TargetsEnum::Money},{"Powerups",TargetsEnum::Powerups},{"Spellbook",TargetsEnum::Spellbook},{"Bombs",TargetsEnum::Bombs},{"Gargoyle",TargetsEnum::Gargoyle},{"Fake angle",TargetsEnum::FakeAngle},{"Viewmodel weapon",TargetsEnum::ViewmodelWeapon},{"Viewmodel hands",TargetsEnum::ViewmodelHands}};
    inline const GroupChoices GroupClasses={{"Scout",PlayerEnum::Scout},{"Soldier",PlayerEnum::Soldier},{"Pyro",PlayerEnum::Pyro},{"Demoman",PlayerEnum::Demoman},{"Heavy",PlayerEnum::Heavy},{"Engineer",PlayerEnum::Engineer},{"Medic",PlayerEnum::Medic},{"Sniper",PlayerEnum::Sniper},{"Spy",PlayerEnum::Spy}};
    inline const GroupChoices GroupRelations={{"Enemy",ConditionsEnum::Enemy},{"Team",ConditionsEnum::Team}};
    inline const GroupChoices GroupColours={{"RED",ConditionsEnum::RED},{"BLU",ConditionsEnum::BLU}};
    inline const GroupChoices GroupSpecial={{"Local",ConditionsEnum::Local},{"Friends",ConditionsEnum::Friends},{"Party",ConditionsEnum::Party},{"Priority",ConditionsEnum::Priority},{"Aim target",ConditionsEnum::Target}};
    inline std::string GroupNames(int bits,GroupChoices entries,const char* empty="None")
    {
        std::string text;for(auto [label,bit]:entries)if(bits&bit){if(!text.empty())text+=", ";text+=label;}return text.empty()?empty:text;
    }
    inline void GroupMulti(const char* label,int& bits,GroupChoices entries,const char* empty="None")
    {
        PushID(label);TextUnformatted(label);SetNextItemWidth(-1);
        if(BeginCombo("##choices",GroupNames(bits,entries,empty).c_str(),ImGuiComboFlags_HeightLarge))
        {Bits("flags",bits,entries);EndCombo();}PopID();
    }
    inline Group_t NewVisualGroup()
    {
        Group_t g;g.m_sName="New group";g.m_iTargets=TargetsEnum::Players;
        g.m_iConditions=ConditionsEnum::Enemy|ConditionsEnum::RED|ConditionsEnum::BLU;
        g.m_iESP=ESPEnum::Name|ESPEnum::HealthBar;g.m_tColor={192,163,243,255};g.m_bTagsOverrideColor=false;return g;
    }
    inline void GroupSummary(const Group_t& g)
    {
        TextWrapped("Applies to: %s",GroupNames(g.m_iTargets,GroupTargets).c_str());
        if(g.m_iTargets&TargetsEnum::Players)TextWrapped("Classes: %s",GroupNames(g.m_iPlayers,GroupClasses,"All classes").c_str());
        if(g.m_iTargets&(TargetsEnum::Players|TargetsEnum::Buildings|TargetsEnum::Projectiles|TargetsEnum::Ragdolls|TargetsEnum::Objective|TargetsEnum::NPCs))
        {
            TextWrapped("Team match: %s; colours: %s",GroupNames(g.m_iConditions,GroupRelations).c_str(),GroupNames(g.m_iConditions,GroupColours).c_str());
            if(g.m_iConditions&(ConditionsEnum::Local|ConditionsEnum::Friends|ConditionsEnum::Party|ConditionsEnum::Priority|ConditionsEnum::Target))
                TextWrapped("Also accepts: %s (where supported)",GroupNames(g.m_iConditions,GroupSpecial).c_str());
        }
        if((g.m_iTargets&TargetsEnum::Players)&&(g.m_iPlayers&PlayerEnum::Conds))
            TextWrapped("Required states: %s",GroupNames(g.m_iPlayers,{{"invulnerable",PlayerEnum::Invulnerable},{"crit boosted",PlayerEnum::Crits},{"invisible",PlayerEnum::Invisible},{"disguised",PlayerEnum::Disguise},{"hurt",PlayerEnum::Hurt}}).c_str());
        if(g.m_iTargets&TargetsEnum::Buildings)
        {
            TextWrapped("Buildings: %s",GroupNames(g.m_iBuildings,{{"sentry",BuildingEnum::Sentry},{"dispenser",BuildingEnum::Dispenser},{"teleporter",BuildingEnum::Teleporter}},"all types").c_str());
            if(g.m_iBuildings&BuildingEnum::Hurt)TextUnformatted("Hurt buildings only");
        }
        if(g.m_iConditions&ConditionsEnum::Dormant)TextUnformatted("Dormant entities only (where supported)");
        const auto info=GroupNames(g.m_iESP,{{"name",ESPEnum::Name},{"box",ESPEnum::Box},{"distance",ESPEnum::Distance},{"skeleton",ESPEnum::Bones},{"health bar",ESPEnum::HealthBar},{"health number",ESPEnum::HealthText},{"Uber bar",ESPEnum::UberBar},{"Uber number",ESPEnum::UberText},{"class icon",ESPEnum::ClassIcon},{"class name",ESPEnum::ClassText},{"weapon icon",ESPEnum::WeaponIcon},{"weapon name",ESPEnum::WeaponText},{"priority",ESPEnum::Priority},{"labels",ESPEnum::Labels},{"buffs",ESPEnum::Buffs},{"debuffs",ESPEnum::Debuffs},{"flags",ESPEnum::Flags},{"lag compensation",ESPEnum::LagCompensation},{"ping",ESPEnum::Ping},{"KDR",ESPEnum::KDR},{"owner",ESPEnum::Owner},{"level",ESPEnum::Level},{"ammo bars",ESPEnum::AmmoBars},{"ammo text",ESPEnum::AmmoText},{"intel timer",ESPEnum::IntelReturnTime}});
        TextWrapped("Information: %s%s",info.c_str(),g.m_bMoonlitInformation?"":" (hidden in Nikogram)");
        TextWrapped("Visible: %s",MoonlitGroups::LayerSummary(g.m_tChams.Visible).c_str());
        TextWrapped("Behind walls: %s",MoonlitGroups::LayerSummary(g.m_tChams.Occluded).c_str());
        if(!g.m_iTargets)TextColored(Gold,"Choose at least one target type.");
        if(g.m_iTargets&(TargetsEnum::Players|TargetsEnum::Buildings|TargetsEnum::Projectiles|TargetsEnum::Ragdolls|TargetsEnum::Objective))
        {
            const bool team=(g.m_iConditions&(ConditionsEnum::Enemy|ConditionsEnum::Team))&&(g.m_iConditions&(ConditionsEnum::RED|ConditionsEnum::BLU));
            const bool special=g.m_iConditions&(ConditionsEnum::Local|ConditionsEnum::Friends|ConditionsEnum::Party|ConditionsEnum::Priority|ConditionsEnum::Target);
            if(!team&&!special)Help("This group cannot match team-based entities: select a relationship and RED/BLU, or an appropriate special match.");
        }
    }
    inline void CustomGroups()
    {
        auto& groups=F::Groups.m_vGroups;
        if(VisualGroup<0||VisualGroup>=int(groups.size()))VisualGroup=groups.empty()?-1:int(groups.size())-1;
        Help("Higher groups take priority. The first matching enabled group supplies the visual setup; groups do not merge.");
        if(CurrentBind!=DEFAULT_BIND)Help("Finish editing the current bind to change group structure or appearance. Group activation can still be bound.");
        const int columns=MoonlitBinding::Columns(GetContentRegionAvail().x,Vars::Menu::Scale.Value);
        if(!BeginTable("CustomGroupsColumns",columns,ImGuiTableFlags_SizingStretchProp))return;
        if(columns==2){TableSetupColumn("Groups",ImGuiTableColumnFlags_WidthStretch,.8f);TableSetupColumn("Rule",ImGuiTableColumnFlags_WidthStretch,1.5f);}
        TableNextColumn();
        {
            Card c{"Custom groups"};
            BeginDisabled(CurrentBind!=DEFAULT_BIND||groups.size()>=32);
            if(Button("New group")){MoonlitGroups::Add(groups,VisualGroup,NewVisualGroup(),RemapActiveGroups);VisualScope=0;}
            BeginDisabled(VisualGroup<0);
            if(Button("Duplicate selected")&&VisualGroup>=0)
            {
                const int source=VisualGroup;auto copy=groups[source];copy.m_sName+=" copy";
                MoonlitGroups::Add(groups,VisualGroup,std::move(copy),RemapActiveGroups,source);VisualScope=0;
            }
            EndDisabled();EndDisabled();
            if(groups.size()>=32)Help("Maximum 32 groups. Existing groups can still be edited.");
            Separator();
            const uint32_t mask=uint32_t(FGet(Vars::ESP::ActiveGroups));
            for(int i=int(groups.size())-1;i>=0;--i)
            {
                PushID(i);
                std::string label=std::to_string(int(groups.size())-i)+". "+groups[i].m_sName+((i<32&&(mask&(1u<<i)))?"":" (off)")+"###group";
                if(Selectable(label.c_str(),VisualGroup==i))VisualGroup=i;
                if(IsItemHovered())SetTooltip("%s",groups[i].m_sName.c_str());
                PopID();
            }
            Separator();
            BeginDisabled(CurrentBind!=DEFAULT_BIND||VisualGroup<0);
            BeginDisabled(VisualGroup==int(groups.size())-1);
            if(Button("Move up"))MoonlitGroups::Move(groups,VisualGroup,1,RemapActiveGroups);
            EndDisabled();BeginDisabled(VisualGroup<=0);
            if(Button("Move down"))MoonlitGroups::Move(groups,VisualGroup,-1,RemapActiveGroups);
            EndDisabled();
            if(Button("Delete selected..."))OpenPopup("Delete group");
            if(BeginPopupModal("Delete group",nullptr,ImGuiWindowFlags_AlwaysAutoResize))
            {
                TextWrapped("Delete '%s'?",VisualGroup>=0?groups[VisualGroup].m_sName.c_str():"");
                Help("The other groups and their bind activation settings are preserved.");
                if(Button("Delete")){MoonlitGroups::Remove(groups,VisualGroup,RemapActiveGroups);CloseCurrentPopup();}
                SameLine();if(Button("Cancel"))CloseCurrentPopup();EndPopup();
            }
            EndDisabled();
            Help("Changes apply immediately. Save your config in Configs & binds to keep them.");
        }
        TableNextColumn();
        if(VisualGroup>=0)
        {
            auto& g=groups[VisualGroup];PushID(VisualGroup);
            if(Card c{"Selected group"};c)
            {
                BeginDisabled(CurrentBind!=DEFAULT_BIND);TextUnformatted("Name");SetNextItemWidth(-1);InputText("##name",&g.m_sName);EndDisabled();
                int active=FGet(Vars::ESP::ActiveGroups,true);BeginDisabled(Disabled||VisualGroup>=32);
                bool on=VisualGroup<32&&(uint32_t(active)&(1u<<VisualGroup))!=0;
                if(Checkbox("Group enabled",&on)&&VisualGroup<32)active=int(on?uint32_t(active)|(1u<<VisualGroup):uint32_t(active)&~(1u<<VisualGroup));
                EndDisabled();FSet(Vars::ESP::ActiveGroups,active);
                GroupSummary(g);
                if(!MasterOn())Help("The Nikogram ESP master is off. This group's saved setup is unchanged.");
            }
            BeginDisabled(CurrentBind!=DEFAULT_BIND);
            if(Card c{"Applies to"};c)
            {
                GroupMulti("Target types",g.m_iTargets,GroupTargets);
                GroupMulti("Relationship",g.m_iConditions,GroupRelations);
                GroupMulti("Team colours",g.m_iConditions,GroupColours);
                if(g.m_iTargets&TargetsEnum::Players)GroupMulti("Player classes",g.m_iPlayers,GroupClasses,"All classes");
                if(g.m_iTargets&TargetsEnum::Buildings)GroupMulti("Building types",g.m_iBuildings,{{"Sentry",BuildingEnum::Sentry},{"Dispenser",BuildingEnum::Dispenser},{"Teleporter",BuildingEnum::Teleporter}},"All buildings");
                if(g.m_iTargets&TargetsEnum::Projectiles)GroupMulti("Projectile types",g.m_iProjectiles,{{"Rocket",ProjectileEnum::Rocket},{"Sticky",ProjectileEnum::Sticky},{"Pipe",ProjectileEnum::Pipe},{"Arrow",ProjectileEnum::Arrow},{"Heal",ProjectileEnum::Heal},{"Flare",ProjectileEnum::Flare},{"Fire",ProjectileEnum::Fire},{"Repair",ProjectileEnum::Repair},{"Cleaver",ProjectileEnum::Cleaver},{"Milk",ProjectileEnum::Milk},{"Jarate",ProjectileEnum::Jarate},{"Gas",ProjectileEnum::Gas},{"Bauble",ProjectileEnum::Bauble},{"Baseball",ProjectileEnum::Baseball},{"Energy",ProjectileEnum::Energy},{"Short circuit",ProjectileEnum::ShortCircuit},{"Meteor shower",ProjectileEnum::MeteorShower},{"Lightning",ProjectileEnum::Lightning},{"Fireball",ProjectileEnum::Fireball},{"Bomb",ProjectileEnum::Bomb},{"Bats",ProjectileEnum::Bats},{"Pumpkin",ProjectileEnum::Pumpkin},{"Monoculus",ProjectileEnum::Monoculus},{"Skeleton",ProjectileEnum::Skeleton},{"Misc",ProjectileEnum::Misc}},"All projectiles");
            }
            if(Card c{"Advanced matching",true};c)
            {
                GroupMulti("Additional matches (OR)",g.m_iConditions,GroupSpecial);
                Help("These can match instead of Relationship + Team colours. Owner-based entities use their owner where supported. Class and state filters still apply. Pickups and viewmodels ignore team filters.");
                Bits("dormant",g.m_iConditions,{{"Dormant only (otherwise active only)",ConditionsEnum::Dormant}});
                if(g.m_iTargets&TargetsEnum::Players){GroupMulti("Required player states (AND)",g.m_iPlayers,{{"Invulnerable",PlayerEnum::Invulnerable},{"Crit boosted",PlayerEnum::Crits},{"Invisible",PlayerEnum::Invisible},{"Disguised",PlayerEnum::Disguise},{"Hurt",PlayerEnum::Hurt}},"No state restriction");}
                if(g.m_iTargets&TargetsEnum::Buildings)Bits("building state",g.m_iBuildings,{{"Hurt buildings only",BuildingEnum::Hurt}});
                if(g.m_iTargets&TargetsEnum::Projectiles)GroupMulti("Required projectile states (AND)",g.m_iProjectiles,{{"Crit",ProjectileEnum::Crit},{"Minicrit",ProjectileEnum::Minicrit}},"No state restriction");
            }
            if(Card c{"Appearance"};c)
            {
                Checkbox("Show information",&g.m_bMoonlitInformation);
                GroupMulti("Information",g.m_iESP,{{"Name",ESPEnum::Name},{"Box",ESPEnum::Box},{"Distance",ESPEnum::Distance},{"Skeleton",ESPEnum::Bones},{"Health bar",ESPEnum::HealthBar},{"Health number",ESPEnum::HealthText},{"Uber bar",ESPEnum::UberBar},{"Uber number",ESPEnum::UberText},{"Class icon",ESPEnum::ClassIcon},{"Class name",ESPEnum::ClassText},{"Weapon icon",ESPEnum::WeaponIcon},{"Weapon name",ESPEnum::WeaponText},{"Priority",ESPEnum::Priority},{"Labels",ESPEnum::Labels},{"Buffs",ESPEnum::Buffs},{"Debuffs",ESPEnum::Debuffs},{"Status flags",ESPEnum::Flags},{"Lag compensation",ESPEnum::LagCompensation},{"Ping",ESPEnum::Ping},{"KDR",ESPEnum::KDR},{"Owner",ESPEnum::Owner},{"Level",ESPEnum::Level},{"Ammo bars",ESPEnum::AmmoBars},{"Ammo text",ESPEnum::AmmoText},{"Intel return timer",ESPEnum::IntelReturnTime}});
                ColourEdit("Group / outline colour",g.m_tColor);Checkbox("Player tags override colour",&g.m_bTagsOverrideColor);
                Checkbox("Custom name colour",&g.m_bCustomNameColor);if(g.m_bCustomNameColor)ColourEdit("Name colour",g.m_tNameColor);
                RawSlider("Outline width",g.m_tGlow.Stencil,0,10);RawSlider("Soft glow",g.m_tGlow.Blur,0.f,10.f);
                if(g.m_iTargets&TargetsEnum::Players)
                {
                    TextUnformatted("Dapper Mann ESP");SetNextItemWidth(-1);
                    Combo("##dapperesp",&g.m_iDapperESP,"Off\0Dapper Mann\0Dapper Mann - Money\0Giga Mann\0");
                }
                MaterialLayers("Visible materials",g.m_tChams.Visible);MaterialLayers("Behind-wall materials",g.m_tChams.Occluded);
                if(g.m_iTargets&(TargetsEnum::ViewmodelHands|TargetsEnum::ViewmodelWeapon))Help("Viewmodels use visible materials only. Behind-wall layers remain saved for any other targets in this group.");
            }
            if(Card c{"Other group effects",true};c)
            {
                Checkbox("Offscreen arrows",&g.m_bOffscreenArrows);RawSlider("Arrow offset",g.m_iOffscreenArrowsOffset,0,1000);RawSlider("Arrow range (HU)",g.m_flOffscreenArrowsMaxDistance,0.f,5000.f);Checkbox("Pickup timers",&g.m_bPickupTimer);
                GroupMulti("Backtrack",g.m_iBacktrack,{{"Enabled",BacktrackEnum::Enabled},{"Last",BacktrackEnum::Last},{"First",BacktrackEnum::First},{"Always",BacktrackEnum::Always}});
                MaterialLayers("Backtrack visible",g.m_tBacktrackChams.Visible);MaterialLayers("Backtrack behind walls",g.m_tBacktrackChams.Occluded);
                RawSlider("Backtrack outline",g.m_tBacktrackGlow.Stencil,0,10);RawSlider("Backtrack glow",g.m_tBacktrackGlow.Blur,0.f,10.f);
                GroupMulti("Trajectories",g.m_iTrajectory,{{"Enabled",TrajectoryEnum::Enabled},{"Ignore Z",TrajectoryEnum::IgnoreZ},{"Predict",TrajectoryEnum::Predict},{"Radius",TrajectoryEnum::Radius},{"Trace",TrajectoryEnum::Trace},{"Sphere",TrajectoryEnum::Sphere},{"Path",TrajectoryEnum::Path}});
                GroupMulti("Sightlines",g.m_iSightlines,{{"Enabled",SightlinesEnum::Enabled},{"Ignore Z",SightlinesEnum::IgnoreZ}});
            }
            EndDisabled();PopID();
        }
        else Help("No groups yet. Choose New group, then select who it applies to and how they should look.");
        EndTable();
    }
}
