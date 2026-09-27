#include "../../SDK/SDK.h"
#include "Statistics.h"
#include "Model.h"
#include "Storage.h"
#include "../Players/PlayerUtils.h"
#include "../ImGui/Render.h"
#include <boost/property_tree/json_parser.hpp>
#include <filesystem>
#include <mutex>
#include <chrono>

namespace Statistics
{
    namespace
    {
        std::mutex guard;
        Model model;
        uint32_t owner=0;
        bool dirty=false,readOnly=false,stopped=false,deathEvents=false;
        std::string message="Waiting for your account.",visit;
        double lastSave=0;
        constexpr double AutoSaveSeconds=15;
        std::filesystem::path file;
        double Now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
        Person Player(int index)
        {
            player_info_t p{};
            if(index<=0)return {};
            if(!I::EngineClient->GetPlayerInfo(index,&p))return {0,"Unknown",false,true};
            return {p.friendsID,p.name,!p.fakeplayer && !p.ishltv && !p.isreplay && p.friendsID!=0,true};
        }
        std::string Priority(uint32_t id)
        {
            // Only manually assigned targeting tags count. Automatic friend,
            // party and F2P priority must not create a tracking record.
            if(!id || F::PlayerUtils.GetPriority(id,false)<1)return {};
            auto it=F::PlayerUtils.m_mPlayerTags.find(id);
            if(it==F::PlayerUtils.m_mPlayerTags.end())return {};
            std::vector<AssignedTag> tags;
            for(int tag:it->second)
            {
                const bool manual=!(tag==F::PlayerUtils.TagToIndex(CHEATER_TAG) && F::PlayerUtils.m_sAutomaticCheaterTags.contains(id));
                if(auto p=F::PlayerUtils.GetTag(tag))tags.push_back({p->m_sName,p->m_iPriority,!p->m_bLabel,manual});
            }
            return ManualPriorityLabel(tags,F::PlayerUtils.GetPriority(id,false));
        }
        bool Active()
        {
            auto net=I::EngineClient->GetNetChannelInfo();
            return !stopped && TrackingAllowed(owner && Player(I::EngineClient->GetLocalPlayer()).id==owner,
                I::EngineClient->IsInGame(),I::EngineClient->IsPlayingDemo(),net!=nullptr,net && net->IsLoopback(),net && net->IsPlayback());
        }
        void Save()
        {
            if(!owner || !dirty || readOnly)return;
            try
            {
                Storage::Save(file,model.lifetime,owner);
                dirty=false;message="Lifetime saved locally.";
            }
            catch(...){message="Could not save statistics. Existing file retained; will retry.";}
            lastSave=Now();
        }
        bool Load(uint32_t id)
        {
            Save();
            // Do not lose unsaved data when switching accounts after a write
            // failure. Tracking remains paused until the previous save succeeds.
            if(owner && dirty && !readOnly){message="Account changed, but the previous account could not be saved. Tracking paused.";return false;}
            owner=id;model={};dirty=false;readOnly=false;visit.clear();
            file=std::filesystem::current_path()/"Nikogram"/"Statistics"/(std::to_string(id)+".json");
            message="Tracking started. No earlier matches can be reconstructed.";
            try
            {
                if(std::filesystem::exists(file)){model.lifetime=Storage::Load(file,id);message="Lifetime statistics loaded.";}
            }
            catch(...){readOnly=true;message="Statistics file unreadable: preserved unchanged. This session will not overwrite it.";}
            return true;
        }
        void Sync()
        {
            if(stopped)return;
            const auto local=Player(I::EngineClient->GetLocalPlayer());
            if(!I::EngineClient->IsPlayingDemo() && local.human && local.id!=owner &&
                (!owner || !dirty || readOnly || Now()-lastSave>=AutoSaveSeconds))Load(local.id);
            auto net=I::EngineClient->GetNetChannelInfo();
            std::string current=Active()?std::string(net->GetAddress())+"|"+I::EngineClient->GetLevelName():"";
            if(current!=visit){model.NewMatch();visit=current;}
        }
        void CountsTable(const Counts& counts,bool pair,bool breakdown=false)
        {
            using namespace ImGui;
            if(BeginTable("Values",2,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_SizingStretchProp))
            {
                TableSetupColumn("Statistic");TableSetupColumn("Recorded value");TableHeadersRow();
                for(int i=0;i<MetricCount;++i)
                {
                    if(((pair || breakdown) && i==PeakStreak) || (!pair && i==AssistedDeaths))continue;
                    TableNextRow();TableNextColumn();TextUnformatted(Labels[i]);TableNextColumn();
                    if(counts.Known(i))Text("%llu",static_cast<unsigned long long>(counts.n[i]));else TextDisabled("Not observed");
                }
                EndTable();
            }
        }
    }
    void Tick(){std::lock_guard lock(guard);Sync();if(Now()-lastSave>=AutoSaveSeconds)Save();}
    void Event(IGameEvent* e)
    {
        if(!e)return;
        const std::string type=e->GetName();
        if(type!="player_death" && type!="player_hurt" && type!="player_spawn" &&
            type!="client_disconnect" && type!="game_newmap")return;
        std::lock_guard lock(guard);Sync();
        if(type=="game_newmap" || type=="client_disconnect"){model.NewMatch();visit.clear();Save();return;}
        if(!Active())return;
        CombatEvent event;
        event.kind=type=="player_hurt"?CombatEvent::Hurt:type=="player_death"?CombatEvent::Death:CombatEvent::Spawn;
        event.local=owner;
        event.victim=Player(I::EngineClient->GetPlayerForUserID(e->GetInt("userid")));
        event.attacker=Player(I::EngineClient->GetPlayerForUserID(e->GetInt("attacker")));
        event.assister=Player(I::EngineClient->GetPlayerForUserID(e->GetInt("assister",-1)));
        for(Person* p:{&event.victim,&event.attacker,&event.assister})p->priority=p->human && !Priority(p->id).empty();
        const int flags=e->GetInt("death_flags"),custom=e->GetInt("customkill");
        event.feign=(flags&TF_DEATH_FEIGN_DEATH)!=0;
        event.headshot=custom==TF_DMG_CUSTOM_HEADSHOT || custom==TF_DMG_CUSTOM_HEADSHOT_DECAPITATION;
        event.backstab=custom==TF_DMG_CUSTOM_BACKSTAB;
        event.domination=(flags&TF_DEATH_DOMINATION)!=0;event.revenge=(flags&TF_DEATH_REVENGE)!=0;
        event.assistDomination=(flags&TF_DEATH_ASSISTER_DOMINATION)!=0;event.assistRevenge=(flags&TF_DEATH_ASSISTER_REVENGE)!=0;
        event.damage=e->GetInt("damageamount");event.crit=e->GetBool("crit");event.mini=e->GetBool("minicrit");
        event.hasFlags=!e->IsEmpty("death_flags");event.hasCustom=!e->IsEmpty("customkill");
        event.hasCrit=!e->IsEmpty("crit");event.hasMini=!e->IsEmpty("minicrit");
        const auto local=H::Entities.GetLocal();
        const int cls=local?local->m_iClass():0,weapon=e->GetInt("weaponid",-1);
        const std::string map=I::EngineClient->GetLevelName();
        event.keys={"class:"+std::to_string(cls),"weapon:"+std::to_string(weapon),"map:"+map};
        event.names={cls?SDK::GetClassByIndex(cls):"Unknown class",weapon>=0?"Weapon ID "+std::to_string(weapon):"Unknown weapon",map};
        if(Apply(model,event))dirty=true;
    }
    void Draw()
    {
        using namespace ImGui;
        std::lock_guard lock(guard);
        static int range=0;static uint32_t selected=0;static char search[96]{};
        const auto& style=GetStyle();
        const float available=GetContentRegionAvail().x;
        const float labelWidth=CalcTextSize("Period").x;
        const float saveWidth=CalcTextSize("Save now").x+style.FramePadding.x*2;
        const float fixedWidth=labelWidth+saveWidth+style.ItemSpacing.x*2;
        const float selectorWidth=std::min(GetFontSize()*15.f,std::max(GetFrameHeight(),available-fixedWidth));
        AlignTextToFramePadding();TextUnformatted("Period");SameLine();
        SetNextItemWidth(selectorWidth);Combo("##StatisticsPeriod",&range,"Match\0Session\0Lifetime\0");
        SameLine();if(Button("Save now"))Save();
        TextWrapped("%s",range==0?"Match: current server/map visit (not each round).":range==1?"Session: since this Nikogram load.":"Lifetime: recorded locally across launches.");
        TextWrapped("%s",Active()?"Recording human-player events. Bots, loopback practice and demos excluded.":"Tracking paused: not in a supported live server session.");
        TextWrapped("%s",message.c_str());
        if(!deathEvents)TextWrapped("Death events unavailable: kills, assists and related counters cannot be recorded.");
        TextDisabled("Observed events only; missing server events cannot be reconstructed.");
        const View& view=range==0?model.match:range==1?model.session:model.lifetime;
        // Scope all native tab states to the current workspace theme; ImGui's
        // default blue otherwise leaks into the tabs and their separator line.
        const ImVec4 accent=F::Render.Accent.Value,background=F::Render.Background0.Value;
        const auto tint=[&](float amount){return ImVec4(background.x+(accent.x-background.x)*amount,
            background.y+(accent.y-background.y)*amount,background.z+(accent.z-background.z)*amount,1.f);};
        PushStyleColor(ImGuiCol_Tab,tint(.18f));
        PushStyleColor(ImGuiCol_TabHovered,tint(.75f));
        PushStyleColor(ImGuiCol_TabSelected,tint(.55f));
        PushStyleColor(ImGuiCol_TabSelectedOverline,accent);
        PushStyleColor(ImGuiCol_TabDimmed,tint(.10f));
        PushStyleColor(ImGuiCol_TabDimmedSelected,tint(.35f));
        PushStyleColor(ImGuiCol_TabDimmedSelectedOverline,accent);
        if(BeginTabBar("StatisticsTabs"))
        {
            if(BeginTabItem("Overview"))
            {
                TextUnformatted("Your totals against all human players");CountsTable(view.total,false);
                if(CollapsingHeader("Class / weapon / map breakdowns"))
                {
                    static int group=0;static std::string choice;
                    Combo("Group",&group,"Class\0Weapon\0Map\0");
                    const char* prefix=group==0?"class:":group==1?"weapon:":"map:";
                    TextWrapped("Class is your class at the event. Weapon is the event's weapon ID (incoming events use the attacker's weapon).");
                    for(const auto& [key,r]:view.breakdowns)if(key.starts_with(prefix))
                    {PushID(key.c_str());if(Selectable(r.name.c_str(),choice==key))choice=key;PopID();}
                    if(auto it=view.breakdowns.find(choice);it!=view.breakdowns.end() && choice.starts_with(prefix)){PushID("Breakdown");CountsTable(it->second.counts,false,true);PopID();}
                }
                EndTabItem();
            }
            if(BeginTabItem("Players"))
            {
                InputText("Search",search,sizeof(search));
                TextWrapped("Only manually assigned targeting priorities >= 1. Records are hidden, not deleted, when priority is lowered.");
                if(BeginTable("Players",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_ScrollY,ImVec2(0,GetTextLineHeightWithSpacing()*9)))
                {
                    TableSetupColumn("Player");TableSetupColumn("Current label");TableSetupColumn("Kills");TableSetupColumn("Deaths");TableHeadersRow();
                    for(const auto& [id,r]:view.players)
                    {
                        const auto label=Priority(id);if(label.empty() || (search[0] && r.name.find(search)==std::string::npos))continue;
                        TableNextRow();TableNextColumn();PushID(int(id));
                        if(Selectable(r.name.c_str(),selected==id,ImGuiSelectableFlags_SpanAllColumns))selected=id;
                        PopID();TableNextColumn();TextUnformatted(label.c_str());
                        for(int metric:{Kills,Deaths}){TableNextColumn();if(r.counts.Known(metric))Text("%llu",(unsigned long long)r.counts.n[metric]);else TextDisabled("--");}
                    }
                    EndTable();
                }
                if(auto it=view.players.find(selected);it!=view.players.end() && !Priority(selected).empty())
                {Text("%s | Account %u",it->second.name.c_str(),selected);CountsTable(it->second.counts,true);}
                else TextWrapped("Select a tracked player. Records begin only after a qualifying priority is assigned.");
                EndTabItem();
            }
            if(BeginTabItem("Labels"))
            {
                auto groups=GroupPlayers(view,Priority);
                TextWrapped("Historical player records are grouped by their current qualifying targeting label.");
                for(const auto& [label,counts]:groups)if(CollapsingHeader(label.c_str())){PushID(label.c_str());CountsTable(counts,true);PopID();}
                if(groups.empty())TextUnformatted("No qualifying priority-player records yet.");
                EndTabItem();
            }
            EndTabBar();
        }
        PopStyleColor(7);
    }
    void Shutdown(){std::lock_guard lock(guard);Save();stopped=true;}
    void DeathEventsAvailable(bool available){std::lock_guard lock(guard);deathEvents=available;}
}
