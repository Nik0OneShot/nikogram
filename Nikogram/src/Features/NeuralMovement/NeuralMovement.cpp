#include "../../SDK/SDK.h"
#include "NeuralMovement.h"
#include "ClientOutcomeBinding.h"
#include "../../NeuralCore/outcome_tracker.h"
#include "../../NeuralCore/datasets.h"
#include "../ImGui/Render.h"
#include "../ImGui/StableControls.h"
#include "../Aimbot/ProjectilePerformancePolicy.h"
#include <chrono>
#include <mutex>
#include <atomic>
#include <deque>
#include "IncomingSnapshot.h"
#include <memory>
#include <type_traits>

namespace NeuralMovement {
namespace {
using Clock=std::chrono::steady_clock;
std::atomic<std::shared_ptr<movement::Service>> service;
std::atomic<std::shared_ptr<movement::trajectory::Service>> richService;
std::atomic<bool> stopped=false,dataBusy=false;
std::atomic<uint64_t>dataRevision=0;uint64_t seenDataRevision=0;
std::atomic<std::shared_ptr<movement::datasets::Store>>datasetStore;
std::atomic<uint64_t>lastModelAccount=0;std::atomic<const char*>lastModel="No current learned prediction";
std::mutex front,serviceLifetime;
uintptr_t channel=0;
struct Track {movement::trajectory::History history; movement::trajectory::Input terrain{};double terrainSeen=-1;movement::Anchor anchor;unsigned long handle=0;int spawn=-1,user=-1;double seen=0;movement::strafe::LegTracker strafe{};};
std::array<Track,129> tracks{};
// Reconstruct in place: aggregate assignment materializes the entire 3.2 MiB
// array on the game thread's stack, including at Frame entry before any guards.
static_assert(std::is_nothrow_default_constructible_v<Track>);
static_assert(std::is_trivially_destructible_v<Track>);
void ResetTrack(Track& track) noexcept { std::destroy_at(&track);std::construct_at(&track); }
void ResetTracks() noexcept { for(auto& track:tracks)ResetTrack(track); }

uint64_t session=uint64_t(std::chrono::system_clock::now().time_since_epoch().count()),serial=1;
int server=-1,cursor=1;bool wasCollect=false,wasActive=false,wasForce=false;
std::atomic<const char*> predictor="ordinary movesim";
std::atomic<const char*> reason="learned prediction disabled";
movement::Vec V(const Vec3& v){return {v.x,v.y,v.z};}
Vec3 V(const movement::Vec& v){return {v[0],v[1],v[2]};}
const std::filesystem::path& DataRoot(){static const auto root=std::filesystem::current_path()/"Nikogram"/"LearningCompressed";return root;}
std::shared_ptr<movement::datasets::Store> Datasets();
bool DatabaseReady(){auto store=Datasets();return store&&store->Ready();}
std::shared_ptr<movement::Service> Get(bool create=false){
    if(stopped||!DatabaseReady()||dataBusy)return {};auto s=service.load();if(!s&&create){std::lock_guard lifecycle(serviceLifetime);if(stopped||dataBusy)return {};s=service.load();if(!s){s=std::make_shared<movement::Service>(movement::layout::Working(DataRoot()));service.store(s);}}return s;
}
std::shared_ptr<movement::trajectory::Service> Rich(){
    if(stopped||!DatabaseReady()||dataBusy)return {};auto s=richService.load();if(!s&&!stopped){std::lock_guard lifecycle(serviceLifetime);s=richService.load();if(!s&&!stopped&&!dataBusy){s=std::make_shared<movement::trajectory::Service>(movement::layout::Working(DataRoot()));richService.store(s);}}return s;
}
std::shared_ptr<movement::datasets::Store> Datasets(){
 auto store=datasetStore.load();if(store||stopped)return store;std::lock_guard lifecycle(serviceLifetime);store=datasetStore.load();if(store||stopped)return store;
 dataBusy=true;store=std::make_shared<movement::datasets::Store>(DataRoot(),[]{
  dataBusy=true;std::shared_ptr<movement::Service>old;std::shared_ptr<movement::trajectory::Service>rich;
  {std::lock_guard lock(serviceLifetime);old=service.exchange({});rich=richService.exchange({});}
  if(old)old->Stop();if(rich)rich->Stop();
 },[]{++dataRevision;dataBusy=false;},[](const std::filesystem::path&file,bool rich,const movement::storage::Cancel&cancel){if(rich){movement::trajectory::ValidateDataset(file,cancel);const auto name=file.filename().string();const int expected=name.find("sequence-observations-v4")!=std::string::npos?movement::trajectory::FrameSchema:name.find("sequence-observations-v3")!=std::string::npos?3:2;if(!movement::trajectory::EmptyDataset(file)&&movement::trajectory::DatasetSchema(file)!=expected)throw std::runtime_error("Sequence dataset slot/schema mismatch");}else movement::ValidateDataset(file,cancel);},[]{
  // Capture complete-row watermarks under each ingestion worker's IO mutex.
  // Copying/validation runs later without blocking collection or cancelling fits.
  std::lock_guard lifecycle(serviceLifetime);movement::datasets::Store::Snapshots snapshots;auto old=service.load();auto rich=richService.load();
  const auto root=movement::layout::Working(DataRoot());
  snapshots[0]=old?old->SnapshotData():std::make_shared<movement::storage::Snapshot>(root/"observations.txt");
  if(rich){auto corpora=rich->SnapshotCorpora();for(int i=0;i<3;++i)snapshots[i+1]=corpora[i];}else{int i=1;for(auto name:{"sequence-observations-v2.txt","sequence-observations-v3.txt","sequence-observations-v4.txt"})snapshots[i++]=std::make_shared<movement::storage::Snapshot>(root/name);}return snapshots;
 },std::chrono::minutes(5),DataRoot().parent_path()/"Learning");datasetStore.store(store);return store;
}
uint32_t Account(CTFPlayer*p){player_info_t info{};return p&&I::EngineClient->GetPlayerInfo(p->entindex(),&info)&&!info.fakeplayer&&!info.ishltv&&!info.isreplay?info.friendsID:0;}
movement::trajectory::Policy PolicyFor(uint64_t id){if(id)if(auto s=Rich()){auto registry=s->Policies();auto it=registry->players.find(id);if(it!=registry->players.end())return it->second;}return {};}
bool HasTraining(){if(auto s=Rich()){auto registry=s->Policies();for(const auto&entry:registry->players)if(entry.second.train)return true;}return false;}
movement::trajectory::Input ReceivedContext(CTFPlayer*p,Track&t,bool probe,const NeuralIncoming::Snapshot&incoming){
    movement::trajectory::Input x{};int cls=p->m_iClass();if(cls>=1&&cls<=9){x[20+cls-1]=1;x[63]=1;}
    auto active=p->m_hActiveWeapon().Get();if(active){auto weapon=active->As<CTFWeaponBase>();x[29+(unsigned(weapon->GetWeaponID())%8)]=1;x[64+(unsigned(weapon->m_iItemDefinitionIndex())%8)]=1;x[61]=1;}
    // Positive, resolved equipment only. An unresolved remote handle is unknown, never proof of absence.
    for(auto handle:p->m_hMyWeapons())if(auto weapon=handle.Get();weapon&&weapon!=active){x[37+(unsigned(weapon->m_iItemDefinitionIndex())%8)]=1;x[62]=1;}
    const ETFCond conditions[]={TF_COND_SPEED_BOOST,TF_COND_BLASTJUMPING,TF_COND_PARACHUTE_ACTIVE,TF_COND_STUNNED,TF_COND_LOST_FOOTING,TF_COND_AIR_CURRENT,TF_COND_BURNING,TF_COND_ZOOMED};
    for(int i=0;i<8;++i)x[45+i]=p->InCond(conditions[i])?1.f:0.f;
    if(probe){
        CTraceFilterWorldAndPropsOnly filter={};filter.pSkip=p;CGameTrace ground={},obstacle={},ledge={};
        Vec3 origin=p->m_vecOrigin(),direction=p->m_vecVelocity();direction.z=0;float speed=direction.Length();if(speed>1)direction/=speed;else direction={1,0,0};
        SDK::Trace(origin+Vec3(0,0,2),origin-Vec3(0,0,96),p->SolidMask(),&filter,&ground);
        SDK::TraceHull(origin+Vec3(0,0,2),origin+Vec3(0,0,2)+direction*64,p->m_vecMins(),p->m_vecMaxs(),p->SolidMask(),&filter,&obstacle);
        SDK::Trace(origin+direction*64+Vec3(0,0,18),origin+direction*64-Vec3(0,0,96),p->SolidMask(),&filter,&ledge);
        t.terrain={};if(!ground.startsolid&&!ground.allsolid){t.terrain[53]=ground.fraction;t.terrain[54]=ground.DidHit()?ground.plane.normal.z:0;t.terrain[57]=t.terrain[58]=1;}
        if(!obstacle.startsolid&&!obstacle.allsolid){t.terrain[55]=obstacle.fraction;t.terrain[59]=1;}
        if(!ledge.startsolid&&!ledge.allsolid){t.terrain[56]=ledge.DidHit()?1.f:0.f;t.terrain[60]=1;}
        t.terrainSeen=I::GlobalVars->realtime;
    }
    if(I::GlobalVars->realtime>=t.terrainSeen&&I::GlobalVars->realtime-t.terrainSeen<.2)for(int i=53;i<=60;++i)x[i]=t.terrain[i];
    const auto cues=incoming.For(p);std::copy(cues.begin(),cues.end(),x.begin()+movement::trajectory::LegacyFeatures);
    return x;
}
// Raw schema-4 extras: networked max speed, the local shooter's eye relative to the
// target, and the target's eye yaw. Anything unavailable stays unknown (zero).
movement::trajectory::Extra ReceivedExtra(CTFPlayer*p){
    movement::trajectory::Extra e;e.recorded=1;
    const float speed=p->m_flMaxspeed();if(std::isfinite(speed)&&speed>0&&speed<=3000){e.maxSpeed=speed;e.maxSpeedKnown=1;}
    auto local=reinterpret_cast<CBaseEntity*>(I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer()));
    if(local&&local->IsPlayer()&&!local->IsDormant()){auto me=local->As<CTFPlayer>();if(me->IsAlive()){const Vec3 d=me->GetShootPos()-p->m_vecOrigin();
        if(std::isfinite(d.x)&&std::isfinite(d.y)&&std::isfinite(d.z)&&std::abs(d.x)<=40000&&std::abs(d.y)<=40000&&std::abs(d.z)<=40000){e.shooterKnown=1;e.dx=d.x;e.dy=d.y;e.dz=d.z;e.hostile=me->m_iTeamNum()!=p->m_iTeamNum()?1.f:0.f;}}}
    const float yaw=p->GetEyeAngles().y;if(std::isfinite(yaw)&&std::abs(yaw)<=720){e.yaw=yaw;e.yawKnown=1;}
    if(!movement::trajectory::ValidExtra(e))e=movement::trajectory::Extra{}; // unknown, never a dropped observation
    return e;
}
bool Supported(CTFWeaponBase*w){
    if(!w)return false;
    switch(w->GetWeaponID()){
    case TF_WEAPON_ROCKETLAUNCHER:case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
    case TF_WEAPON_COMPOUND_BOW:case TF_WEAPON_CROSSBOW:
    case TF_WEAPON_GRENADELAUNCHER:case TF_WEAPON_PIPEBOMBLAUNCHER:case TF_WEAPON_CANNON:
    case TF_WEAPON_FLAREGUN:case TF_WEAPON_FLAREGUN_REVENGE:
    case TF_WEAPON_RAYGUN:case TF_WEAPON_DRG_POMSON:
    case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:case TF_WEAPON_SYRINGEGUN_MEDIC:
    case TF_WEAPON_FLAME_BALL:case TF_WEAPON_CLEAVER:
    case TF_WEAPON_JAR:case TF_WEAPON_JAR_MILK:case TF_WEAPON_JAR_GAS:return true;
    case TF_WEAPON_PARTICLE_CANNON:
    {
        // Ordinary primary energy balls only. The timed secondary charge is not
        // an instantaneous release and needs a separate launch-delay contract.
        const float begin=w->As<CTFParticleCannon>()->m_flChargeBeginTime();
        return std::isfinite(begin)&&begin<=0.f;
    }
    default:return false;
    }
}
bool Active(){return !stopped&&!dataBusy&&I::EngineClient->IsInGame()&&!I::EngineClient->IsPlayingDemo()&&I::EngineClient->GetNetChannelInfo();}
bool Current(CTFPlayer*p,const Track&t){return p&&!p->IsDormant()&&p->IsAlive()&&!p->IsSwimming()&&server==I::ClientState->m_nServerCount&&channel==reinterpret_cast<uintptr_t>(I::EngineClient->GetNetChannelInfo())&&t.anchor.valid&&t.anchor.past[2].player==Account(p)&&t.handle==p->As<IHandleEntity>()->GetRefEHandle().ToInt()&&t.spawn==p->m_iSpawnCounter()&&std::abs(t.anchor.past[2].time-p->m_flSimulationTime())<.0001&&I::GlobalVars->realtime>=t.seen&&I::GlobalVars->realtime-t.seen<.2;}
}
movement::trajectory::Policy TargetPolicy(CTFPlayer*p){return PolicyFor(Account(p));}
bool PoliciesReady(){if(auto rich=Rich())return rich->GetStatus().policiesReady;return false;}
bool TargetForced(CTFPlayer*p){return ForceEnabled()||(p&&TargetPolicy(p).force);}
bool TargetEnabled(CTFPlayer*p){return TargetForced(p)||Vars::NeuralMovement::Use.Value;}
bool TargetFallback(CTFPlayer*p){return !TargetForced(p)||(p&&TargetPolicy(p).allowMovesim);}
bool HasIndividualTraining(){return HasTraining();}
bool HasIndividualForce(){if(auto s=Rich()){auto registry=s->Policies();for(const auto&entry:registry->players)if(entry.second.force)return true;}return false;}
void Hint(const char*text){ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);ImGui::TextWrapped("%s",text);ImGui::PopStyleColor();}
void DrawPlayer(uint32_t account,bool fake){
 using namespace ImGui;namespace tr=movement::trajectory;
 Separator();if(!StableControls::BeginMenu("Learning & prediction"))return;
 if(fake||!account){Hint("Personal learning needs a stable account ID.");TextWrapped("This player can still contribute to global learning.");ImGui::EndMenu();return;}
 auto rich=Rich();if(!rich){Hint("Learning is temporarily unavailable.");ImGui::EndMenu();return;}if(!rich->GetStatus().ready){Hint("Loading saved learning data. Player settings can be edited when loading finishes.");ImGui::EndMenu();return;}const auto appliedPolicy=PolicyFor(account);static std::map<uint32_t,tr::Policy>pendingPolicies;
 auto pending=pendingPolicies.find(account);if(pending!=pendingPolicies.end()&&pending->second==appliedPolicy){pendingPolicies.erase(pending);pending=pendingPolicies.end();}auto policy=pending==pendingPolicies.end()?appliedPolicy:pending->second;bool changed=false;
 if(pending!=pendingPolicies.end())Hint("Saving requested player settings. Current aiming changes after saving.");
 changed|=Checkbox("Learn this player",&policy.train);Hint("Collects and saves observations. Use Train saved observations now to fit updated models.");
 const char*chosen=policy.selection==tr::Selection::Imported?"Imported":policy.selection==tr::Selection::Personal?"Personal":policy.selection==tr::Selection::Global?"Global":"Auto";
 if(StableControls::BeginMenu((std::string("Model: ")+chosen+"###LearningModel").c_str())){
  const char*labels[]={"Auto","Global","Personal"};for(int i=0;i<3;++i)if(MenuItem(labels[i],nullptr,int(policy.selection)==i)){policy.selection=tr::Selection(i);changed=true;}
  auto models=rich->Published();if(StableControls::BeginMenu("Imported",!models->imported.empty())){for(const auto&[id,model]:models->imported){auto label="Model "+std::to_string(id);if(MenuItem(label.c_str(),nullptr,policy.selection==tr::Selection::Imported&&policy.imported==id)){policy.selection=tr::Selection::Imported;policy.imported=id;changed=true;}}ImGui::EndMenu();}
  if(models->imported.empty())Hint("Import a trained model in Data management.");ImGui::EndMenu();
 }
 if(policy.selection==tr::Selection::Personal)Hint("Prefers this player's model; falls back to global.");
 const bool forced=ForceEnabled()||appliedPolicy.force;TextWrapped("Aiming: %s",forced?(appliedPolicy.allowMovesim?"Force neural with explicit ordinary fallback":"Force neural"):Vars::NeuralMovement::Use.Value?"Auto":"Off");
 if(lastModelAccount==account){TextWrapped("Last used: %s",lastModel.load());TextWrapped("%s",reason.load());}
 else Hint("Waiting for a current prediction for this player.");
 if(StableControls::BeginMenu("Advanced overrides")){
  changed|=Checkbox("Force neural for this player",&policy.force);
  changed|=Checkbox("Allow ordinary fallback when forced",&policy.allowMovesim);
  changed|=Checkbox("Allow trained experimental models",&policy.unvalidated);
  if(MenuItem("Train saved observations now")){rich->Train(account);if(auto legacy=Get(true))legacy->Request(movement::Action::TrainExperimental,account);}
  Hint("Overrides are saved for this account only.");ImGui::EndMenu();
 }
 if(changed){if((pendingPolicies.size()<1024||pendingPolicies.contains(account))&&rich->SetPolicy(account,policy))pendingPolicies[account]=policy;else TextWrapped("Could not queue the change. Try again.");}ImGui::EndMenu();
}

namespace {
struct Opportunity{int command=-1,server=-1;int weapon=0,target=0;unsigned weaponId=0,range=0;bool air=false;int mode=0;movement::trajectory::WeaponContext context{};};
Opportunity opportunity;struct Committed{Opportunity selected;int finalCommand=-1;bool consumed=false;};std::deque<Committed>committed;std::mutex shotMutex;
std::atomic<bool> policyEvidence=false;
std::atomic<uint64_t> trackedKnown=0,trackedUnknown=0,trackedKills=0;
std::atomic<const char*> trackedReason="waiting for isolated native shots";
}
movement::trajectory::WeaponContext ShooterWeaponContext(CTFPlayer*local,CTFWeaponBase*weapon){
    movement::trajectory::WeaponContext c;
    // Only the existing isolated rocket-outcome tracker has supported launch semantics.
    // Charged/physics weapons remain unknown instead of borrowing rocket evidence.
    if(!local||!weapon||(weapon->GetWeaponID()!=TF_WEAPON_ROCKETLAUNCHER&&weapon->GetWeaponID()!=TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT)||weapon->m_iItemDefinitionIndex()==Soldier_m_TheBeggarsBazooka||weapon->m_iItemDefinitionIndex()==Soldier_m_RocketJumper)return c;
    c.itemDef=weapon->m_iItemDefinitionIndex();
    const bool precision=local->InCond(TF_COND_RUNE_PRECISION),jumping=local->InCond(TF_COND_BLASTJUMPING);
    c.speed=precision?3000.f:SDK::AttribHookValue(1100.f,"mult_projectile_speed",weapon);
    c.radius=SDK::AttribHookValue(TF_ROCKET_RADIUS,"mult_explosion_radius",weapon);
    if(jumping&&SDK::AttribHookValue(1.f,"rocketjump_attackrate_bonus",weapon)!=1.f)c.radius*=.8f;
    c.damageScale=SDK::AttribHookValue(1.f,"mult_dmg",weapon);
    c.gravity=0.f;c.charge=0.f;
    c.state=unsigned(precision)|(unsigned(jumping)<<1)|(unsigned(local->IsCritBoosted())<<2)
        |(unsigned(local->IsMiniCritBoosted())<<3)|(unsigned(int(SDK::AttribHookValue(0.f,"centerfire_projectile",weapon))==1)<<4)
        |(unsigned(bool(local->m_fFlags()&FL_DUCKING))<<5);
    return c;
}
int LearnedSplash(CTFPlayer*target,CTFWeaponBase*weapon,bool*evidence){
    bool enough=false;int mode=1;
    if(target&&weapon)if(auto local=H::Entities.GetLocal())if(auto rich=Rich()){
        unsigned range=unsigned(std::clamp(int(local->m_vecOrigin().DistTo(target->m_vecOrigin())/512.f),0,15));
        mode=int(rich->Outcomes()->Choose(unsigned(weapon->GetWeaponID()),ShooterWeaponContext(local,weapon),range,!target->IsOnGround(),movement::trajectory::Objective(Vars::NeuralMovement::SplashObjective.Value),enough,true));
    }
    policyEvidence=enough;if(evidence)*evidence=enough;return mode;
}
void ShotOpportunity(CTFPlayer*target,CTFWeaponBase*weapon,int mode,int command){
    if(!target||!weapon||mode<1||mode>3)return;auto local=H::Entities.GetLocal();if(!local)return;std::lock_guard lock(shotMutex);
    opportunity={command,I::ClientState->m_nServerCount,weapon->GetRefEHandle().ToInt(),target->GetRefEHandle().ToInt(),unsigned(weapon->GetWeaponID()),unsigned(std::clamp(int(local->m_vecOrigin().DistTo(target->m_vecOrigin())/512.f),0,15)),!target->IsOnGround(),mode};
    opportunity.context=ShooterWeaponContext(local,weapon);
}
void CommitOpportunity(CTFPlayer*local,CTFWeaponBase*weapon,CUserCmd*cmd){
    if(!local||!weapon||!cmd)return;std::lock_guard lock(shotMutex);
    if(opportunity.command!=G::OriginalCmd.command_number||opportunity.server!=I::ClientState->m_nServerCount||opportunity.weapon!=weapon->GetRefEHandle().ToInt()||!(cmd->buttons&IN_ATTACK)||cmd->weaponselect)return;
    if(std::any_of(committed.begin(),committed.end(),[&](const Committed&e){return e.finalCommand==cmd->command_number&&e.selected.server==opportunity.server&&e.selected.weapon==opportunity.weapon;}))return;
    if(committed.size()>=256)committed.pop_front();committed.push_back({opportunity,cmd->command_number,false});
}
void TrackedOutcome(const movement::outcomes::Completed&done){
    auto outcome=done.outcome;outcome.evidence=outcome.known?(done.confidence==movement::outcomes::Confidence::DirectInflictorKill?1u:2u):3u;
    if(outcome.known){++trackedKnown;trackedKills+=outcome.killed;}else ++trackedUnknown;
    trackedReason=movement::outcomes::Name(done.reason);if(auto rich=Rich())rich->RecordOutcome(outcome);
}
void ShotFired(CTFPlayer*local,CTFWeaponBase*weapon){
    if(stopped||!local||!weapon||local!=H::Entities.GetLocal()||I::EngineClient->IsPlayingDemo())return;
    Opportunity selected;bool matched=false;int command=-1;
    {std::lock_guard lock(shotMutex);auto native=local->m_pCurrentCommand();if(!native)return;command=native->command_number;
        auto match=std::find_if(committed.begin(),committed.end(),[&](const Committed&e){return e.finalCommand==command&&e.selected.server==I::ClientState->m_nServerCount&&e.selected.weapon==weapon->GetRefEHandle().ToInt();});
        if(match!=committed.end()){if(match->consumed)return;match->consumed=true;selected=match->selected;matched=true;}
    }
    movement::trajectory::Outcome chosen;chosen.weapon=selected.weaponId;chosen.rangeBand=selected.range;chosen.air=selected.air;
    chosen.chosen=movement::trajectory::Splash(selected.mode);chosen.context=selected.context;
    ClientOutcomes::NativeFire(local,weapon,command,matched,uint32_t(selected.target),chosen);
}
void Frame(){
    if(stopped)return;try{if(!DatabaseReady()||dataBusy)return;}catch(...){reason="selected database unavailable";return;}if(seenDataRevision!=dataRevision){seenDataRevision=dataRevision;ResetSession();}
    ClientOutcomes::Frame();
    try{
        const bool collect=Vars::NeuralMovement::Collect.Value,use=Vars::NeuralMovement::Use.Value,force=ForceEnabled();
        const bool individual=HasTraining()||HasIndividualForce();if(auto rich=Rich())rich->Configure(collect,Vars::NeuralMovement::TrainingSamples.Value,Vars::NeuralMovement::AllSamples.Value,Vars::NeuralMovement::IncludeBots.Value);
        if(!Active()){std::unique_lock lock(front,std::try_to_lock);if(lock){ResetTracks();wasActive=false;channel=0;}}
        if(auto s=Get(collect||use||force||individual))s->Configure({collect||HasTraining(),use||force||individual,Vars::NeuralMovement::AllSamples.Value,size_t(std::max(1024,Vars::NeuralMovement::TrainingSamples.Value)),false,Vars::NeuralMovement::IncludeBots.Value});
        if(force!=wasForce){predictor=force?"experimental neural only (unvalidated)":"ordinary movesim";reason=force?"waiting for valid trained prediction":"learned prediction disabled";wasForce=force;}
        if(!use&&!force&&!HasIndividualForce()){predictor="ordinary movesim";reason="learned prediction disabled";}
    }catch(...){reason="learning service unavailable";}
}
void ObserveNetwork(){
    if(stopped)return;
    const bool active=Active(),collect=Vars::NeuralMovement::Collect.Value||HasTraining(),use=Vars::NeuralMovement::Use.Value||ForceEnabled()||HasIndividualForce();
    std::unique_lock lock(front,std::try_to_lock);if(!lock)return;
    if(!active||server!=I::ClientState->m_nServerCount||channel!=reinterpret_cast<uintptr_t>(I::EngineClient->GetNetChannelInfo())||collect!=wasCollect||!wasActive){ResetTracks();++session;server=I::ClientState->m_nServerCount;channel=reinterpret_cast<uintptr_t>(I::EngineClient->GetNetChannelInfo());}
    wasCollect=collect;wasActive=active;
    if(!active||(!collect&&!use))return;
    auto s=Get();if(!s)return;
    bool probed=false;static int probeCursor=1;const auto start=Clock::now();const NeuralIncoming::Snapshot incoming;const int maximum=std::clamp(I::EngineClient->GetMaxClients(),0,128);
    if(probeCursor>maximum)probeCursor=1;
    // Round-robin visits all eligible observable slots. Time and count caps are independent.
    for(int visited=0;visited<std::min(maximum,32);++visited){
        if(Clock::now()-start>std::chrono::microseconds(350))break;
        if(cursor>maximum)cursor=1;const int i=cursor++;auto&t=tracks[i];
        auto entity=reinterpret_cast<CBaseEntity*>(I::ClientEntityList->GetClientEntity(i));
        if(!entity||!entity->IsPlayer()||i==I::EngineClient->GetLocalPlayer()||entity->IsDormant()){ResetTrack(t);continue;}
        auto p=entity->As<CTFPlayer>();if(!p->IsAlive()||p->IsSwimming()){ResetTrack(t);continue;}
        // Read network fields before resolver, interpolation reconstruction, or movement simulation.
        const double time=p->m_flSimulationTime();if(t.anchor.valid&&time==t.anchor.past[2].time&&t.handle==p->As<IHandleEntity>()->GetRefEHandle().ToInt()&&t.spawn==p->m_iSpawnCounter())continue;
        player_info_t info{};if(!I::EngineClient->GetPlayerInfo(i,&info)||info.ishltv||info.isreplay){ResetTrack(t);continue;}
        const uint64_t id=!info.fakeplayer&&info.friendsID?uint64_t(info.friendsID):0;
        const auto handle=p->As<IHandleEntity>()->GetRefEHandle().ToInt();const int spawn=p->m_iSpawnCounter();
        bool reset=t.handle!=handle||t.spawn!=spawn||t.user!=info.userID||t.anchor.past[2].player!=id;
        if(t.anchor.valid&&(time<=t.anchor.past[2].time||time-t.anchor.past[2].time>.2))reset=true;
        if(reset){ResetTrack(t);t.handle=handle;t.spawn=spawn;t.user=info.userID;t.anchor.past[2].track=++serial;}
        movement::Observation o;o.player=id;o.track=t.anchor.past[2].track?t.anchor.past[2].track:++serial;o.session=session;o.time=time;o.position=V(p->m_vecOrigin());o.velocity=V(p->m_vecVelocity());o.ground=p->IsOnGround();
        t.anchor.past[0]=t.anchor.past[1];t.anchor.past[1]=t.anchor.past[2];t.anchor.past[2]=o;t.anchor.valid=t.anchor.past[0].track==o.track&&t.anchor.past[0].session==session;t.seen=I::GlobalVars->realtime;
        const bool chosen=id&&PolicyFor(id).train;
        const bool shouldCollect=Vars::NeuralMovement::Collect.Value||chosen;
        if(shouldCollect)s->Observe(o);
        bool probe=!probed&&i>=probeCursor;if(probe){probed=true;probeCursor=i>=maximum?1:i+1;}
        movement::trajectory::Received received{o,ReceivedContext(p,t,probe,incoming),ReceivedExtra(p)};
        t.strafe.Update({o.time,o.position[0],o.position[1],o.velocity[0],o.velocity[1],o.ground});
        if(t.history.count==t.history.points.size()){std::move(t.history.points.begin()+1,t.history.points.end(),t.history.points.begin());--t.history.count;}
        t.history.points[t.history.count++]=received;
        if(shouldCollect)if(auto rich=Rich())rich->Observe(received);
    }
}
Context Begin(CTFPlayer*p,CTFWeaponBase*w){
    Context c;if(!Supported(w)||!p||p->entindex()<1||p->entindex()>128||!Active())return c;
    // Baselines are only recorded while collecting; nothing else uses this context.
    auto s=Get();if(!s||!Vars::NeuralMovement::Collect.Value)return c;
    std::unique_lock lock(front,std::try_to_lock);if(!lock)return c;const auto&t=tracks[p->entindex()];if(!Current(p,t)){reason="missing or stale received history";return c;}
    c.baseline.anchor=t.anchor;c.pool=s->Published();c.entity=p->entindex();c.handle=t.handle;c.spawn=t.spawn;c.server=server;c.session=session;c.account=Account(p);c.valid=true;
    c.baseline.points[0]={t.anchor.past[2].time,t.anchor.past[2].position};c.baseline.count=1;c.last=c.baseline.points[0];return c;
}
void End(Context&c){if(c.valid&&!c.trial&&Vars::NeuralMovement::Collect.Value){if(auto s=Get())s->RecordBaseline(c.baseline);if(auto rich=Rich())rich->RecordBaseline(c.baseline);}}
void RecordPoint(Context&c,double time,const movement::Vec& ordinary){
    if(dataBusy||!c.valid||c.trial)return;
    auto&base=c.baseline;
    if(base.count<base.points.size()&&time>base.points[base.count-1].time&&time-base.anchor.past[2].time<=1.25)base.points[base.count++]={time,ordinary};
}
void FallbackResult(bool available){predictor=available?"ordinary prediction":"no automated solution";lastModel=available?"Ordinary prediction":"No valid prediction";reason=available?"Learned motion was unavailable for this target; ordinary prediction is active":"Neither learned nor ordinary prediction found a valid shot";}
void OrdinaryKept(){predictor="ordinary prediction";lastModel="Ordinary prediction";reason="Auto: ordinary prediction found a valid shot; learned trial not needed";}
bool ForceEnabled(){return Vars::NeuralMovement::Force.Value;}
bool AimingEnabled(){return Vars::NeuralMovement::Use.Value||ForceEnabled();}
namespace {std::atomic<const char*> strafeReason="learned strafe timing: waiting for a strafing target";}
bool ReversalPlan(CTFPlayer*p,int hypothesis,float&first,float&leg,float&directionX,float&directionY){
    // Auto only: the learned part contributes timing; movesim keeps every piece of physics.
    if(!Vars::NeuralMovement::Use.Value||!p||p->entindex()<1||p->entindex()>128||TargetForced(p)||!Active())return false;
    auto rich=Rich();if(!rich)return false;const auto model=rich->StrafeModel();if(!model||!model->accepted){strafeReason="learned strafe timing: no accepted model (train first)";return false;}
    const auto stats=rich->StrafeStats();std::unique_lock lock(front,std::try_to_lock);if(!lock)return false;
    const auto&t=tracks[p->entindex()];if(!Current(p,t)||!t.history.count||!t.strafe.Strafing())return false;
    // Just after a zero crossing the tracker still describes the old leg until it confirms: no plan.
    if(t.strafe.pending||t.strafe.Lateral()*float(t.strafe.sign)<=0.f){strafeReason="learned strafe timing: reversal in progress";return false;}
    const auto&last=t.history.points[t.history.count-1];if(last.observation.time!=t.anchor.past[2].time||!last.observation.ground)return false;
    static const movement::strafe::Stats noStats;const auto context=movement::trajectory::StrafeContext(last,stats?*stats:noStats);
    const auto plan=movement::strafe::MakePlan(*model,t.strafe,context,t.anchor.past[2].time,hypothesis?.8f:.5f);strafeReason=plan.reason;
    if(!plan.valid)return false;first=plan.first;leg=plan.leg;directionX=t.strafe.ax*float(t.strafe.sign);directionY=t.strafe.ay*float(t.strafe.sign);return true;
}
const char* ReversalReason(){return strafeReason.load();}
bool SupportsProjectile(CTFWeaponBase*w){return Supported(w);}
Context BeginForced(CTFPlayer*p,CTFWeaponBase*w){
    Context c;c.trial=true;predictor="experimental neural only (unvalidated)";
    if(!TargetEnabled(p)||!Active()){reason="experimental mode inactive or world unavailable";return c;}
    if(!Supported(w)){reason="weapon or current launch state unsupported by neural prediction";return c;}
    if(!p||p->entindex()<1||p->entindex()>128){reason="experimental mode requires a player target";return c;}
    auto s=Get();if(!s){reason="learning service unavailable; no automated solution";return c;}lastModelAccount=Account(p);lastModel="No applicable trained model";
    std::unique_lock lock(front,std::try_to_lock);if(!lock){reason="received history busy; no automated solution";return c;}
    const auto&t=tracks[p->entindex()];if(!Current(p,t)){reason="missing or stale received history; no automated solution";return c;}
    c.policy=TargetPolicy(p);c.pool=s->Published();c.candidates=s->Candidates();
    if(!c.policy.unvalidated||!TargetForced(p))c.candidates=std::make_shared<const movement::Pool>();
    if(c.policy.selection==movement::trajectory::Selection::Global||c.policy.selection==movement::trajectory::Selection::Imported){
        auto globalOnly=[](std::shared_ptr<const movement::Pool> pool){auto result=std::make_shared<movement::Pool>(*pool);std::erase_if(result->models,[](const movement::Model&m){return m.player!=0;});return result;};c.pool=globalOnly(c.pool);c.candidates=globalOnly(c.candidates);
    }
    if(auto rich=Rich()){auto models=rich->Published();auto policies=rich->Policies();movement::trajectory::Input features{};
        const auto&extra=t.history.points[t.history.count?t.history.count-1:0].extra;
        auto name=[](const movement::trajectory::Model&m,bool imported){return m.schema==2?(m.owner?"Sequence v2 model (incoming cues unavailable)":"Global sequence v2 model (cue-unaware)"):m.schema==movement::trajectory::FrameSchema?(imported?"Imported v4 frame model":m.owner?"Personal v4 frame model":"Global v4 frame model"):(imported?"Imported v3 cue model":m.owner?"Personal v3 cue model":"Global v3 cue model");};
        if(movement::trajectory::Features(t.history,features)){if(auto model=movement::trajectory::Select(*models,*policies,Account(p),c.policy.unvalidated)){c.distribution=movement::trajectory::Predict(*model,features,extra);if(c.distribution.valid)lastModel=name(*model,c.policy.selection==movement::trajectory::Selection::Imported);}
            if(!c.distribution.valid&&c.policy.unvalidated&&c.policy.selection!=movement::trajectory::Selection::Imported){auto global=models->local.find(0);if(global!=models->local.end()){c.distribution=movement::trajectory::Predict(global->second,features,extra);if(c.distribution.valid)lastModel=name(global->second,false);}}}
    }
    if(c.policy.selection==movement::trajectory::Selection::Imported&&!c.distribution.valid){reason="assigned imported model unavailable or outside coverage";return c;}
    if(std::find(c.pool->disabled.begin(),c.pool->disabled.end(),Account(p))!=c.pool->disabled.end()){reason="target learning disabled";return c;}
    if(!c.pool||!c.candidates||(c.pool->models.empty()&&c.candidates->models.empty()&&!c.distribution.valid)){reason="missing trained model; train existing data first";return c;}
    c.baseline.anchor=t.anchor;c.entity=p->entindex();c.handle=t.handle;c.spawn=t.spawn;c.server=server;c.session=session;c.account=Account(p);c.valid=true;
    c.last={t.anchor.past[2].time,t.anchor.past[2].position};return c;
}
bool ForcedPoint(Context&c,double time,movement::Vec&out,CTFPlayer*p){
    out={};if(!c.valid)return false;
    if(!p||!TargetEnabled(p)||c.entity!=p->entindex()||c.handle!=p->GetRefEHandle().ToInt()||c.spawn!=p->m_iSpawnCounter()||c.server!=I::ClientState->m_nServerCount||c.session!=session||c.account!=Account(p)||!Active()) {c.failed=c.invalidated=true;reason="target identity or force policy changed during neural solve";return false;}
    if(c.failed||!c.pool||!c.candidates)return false;
    const auto&a=c.baseline.anchor.past[2];movement::Vec displacement{};
    if(c.distribution.valid){
        if(c.branch>=c.distribution.branches.size())return false;const auto&branch=c.distribution.branches[c.branch];c.probability=branch.probability;
        for(int knot=0;knot<movement::trajectory::Knots&&movement::trajectory::Horizons[knot]<float(time-a.time);++knot)
            if(!branch.supported[knot]||branch.radius[knot]>Vars::NeuralMovement::MaximumPathUncertainty.Value){reason="unsupported intermediate path geometry";return false;}
        if(!movement::trajectory::Interpolate(branch,float(time-a.time),displacement,c.uncertainty)){reason="unsupported horizon in trained path distribution";return false;}
        if(branch.probability<Vars::NeuralMovement::MinimumPathProbability.Value/100.f||c.uncertainty>Vars::NeuralMovement::MaximumPathUncertainty.Value){reason="path probability or held-out uncertainty rejected";return false;}
    }else{
        movement::Features x;if(!movement::MakeFeatures(c.baseline.anchor,float(time-a.time),x)){reason="legacy model supports at most one second; rich horizon needs new evidence";return false;}
        const uint64_t identity=c.policy.selection==movement::trajectory::Selection::Global||c.policy.selection==movement::trajectory::Selection::Imported?0:a.player;
        const auto prediction=movement::PredictExperimental(*c.candidates,*c.pool,identity,x);reason=prediction.reason;if(!prediction.valid)return false;displacement=prediction.displacement;lastModel=prediction.personal?"Personal legacy model":"Global legacy model";
    }
    for(int k=0;k<3;++k){out[k]=a.position[k]+displacement[k];if(!std::isfinite(out[k])||std::abs(out[k])>16384.f){c.failed=true;reason="nonfinite or out-of-world neural position";return false;}}

    double square=0;for(int k=0;k<3;++k){double d=out[k]-c.last.position[k];square+=d*d;}
    if(!std::isfinite(time)||time<=c.last.time||std::sqrt(square)>3000.*(time-c.last.time)+32.){c.failed=true;reason="experimental trajectory continuity rejected";return false;}
    if(!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Geometry)){c.failed=true;reason="experimental geometry budget exhausted";return false;}
    CTraceFilterWorldAndPropsOnly filter={};filter.pSkip=p;movement::Vec previous=c.last.position;
    const auto clear=[&](const movement::Vec&next){if(!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Geometry)){reason="incomplete search: terrain budget exhausted";return false;}CGameTrace trace={};SDK::TraceHull(V(previous)+Vec3(0,0,.1f),V(next)+Vec3(0,0,.1f),p->m_vecMins(),p->m_vecMaxs(),p->SolidMask(),&filter,&trace);previous=next;return !trace.DidHit()&&!trace.startsolid&&!trace.allsolid;};
    if(c.distribution.valid)for(size_t k=0;k<movement::trajectory::Horizons.size();++k){double knot=a.time+movement::trajectory::Horizons[k];if(knot<=c.last.time||knot>=time)continue;movement::Vec position{};for(int axis=0;axis<3;++axis)position[axis]=a.position[axis]+c.distribution.branches[c.branch].displacement[k][axis];if(!clear(position)){c.failed=true;reason="learned path intersects terrain";return false;}}
    if(!clear(out)){c.failed=true;reason="learned path intersects terrain or search incomplete";return false;}
    c.last={time,out};c.changed=true;c.lastLearned=true;
    predictor=c.distribution.valid?"joint neural path (unvalidated)":"legacy neural path";
    return true;
}
void ForcedResult(bool applied,const char*detail){
    if(detail)reason=detail;
    else if(applied)reason="experimental neural trajectory selected (unvalidated)";
    if(!applied)predictor="experimental neural only: no automated solution";
}
ObserverModel ObserverBegin(CTFPlayer*p,CTFWeaponBase*w,double time,const movement::Vec& origin){
    ObserverModel c;
    if(!p||p->entindex()<1||p->entindex()>128||!Active()||!Supported(w)){c.state=AimObservation::Availability::Unsupported;return c;}
    // Loading a service is never an observer side effect.
    auto old=service.load();auto rich=richService.load();
    auto policies=rich?rich->Policies():nullptr;const auto account=Account(p);
    if(policies){auto it=policies->players.find(account);if(it!=policies->players.end())c.policy=it->second;}
    if(!Vars::NeuralMovement::Use.Value&&!Vars::NeuralMovement::Force.Value&&!c.policy.force){c.state=AimObservation::Availability::Disabled;return c;}
    if(!old||!rich||!policies||!rich->GetStatus().policiesReady)return c;
    movement::trajectory::History history;
    {
        std::unique_lock lock(front,std::try_to_lock);if(!lock){c.state=AimObservation::Availability::History;return c;}
        const auto&t=tracks[p->entindex()];
        if(!Current(p,t)||std::abs(t.anchor.past[2].time-time)>.0001){c.state=AimObservation::Availability::History;return c;}
        for(int k=0;k<3;++k)if(std::abs(t.anchor.past[2].position[k]-origin[k])>.01f){c.state=AimObservation::Availability::History;return c;}
        c.anchor=t.anchor;history=t.history;
    }
    c.pool=old->Published();c.experimental=c.policy.unvalidated&&(Vars::NeuralMovement::Force.Value||c.policy.force);
    if(c.experimental)c.candidates=old->Candidates();
    if(!c.pool||std::find(c.pool->disabled.begin(),c.pool->disabled.end(),account)!=c.pool->disabled.end())return c;
    auto models=rich->Published();movement::trajectory::Input features{};const auto extra=history.count?history.points[history.count-1].extra:movement::trajectory::Extra{};
    if(models&&movement::trajectory::Features(history,features)){
        if(auto model=movement::trajectory::Select(*models,*policies,account,c.policy.unvalidated))c.distribution=movement::trajectory::Predict(*model,features,extra);
        if(!c.distribution.valid&&c.policy.unvalidated&&c.policy.selection!=movement::trajectory::Selection::Imported){auto it=models->local.find(0);if(it!=models->local.end())c.distribution=movement::trajectory::Predict(it->second,features,extra);}
    }
    c.probability=Vars::NeuralMovement::MinimumPathProbability.Value/100.f;c.radius=Vars::NeuralMovement::MaximumPathUncertainty.Value;
    if(c.distribution.valid){
        for(size_t i=1;i<c.distribution.branches.size();++i)if(c.distribution.branches[i].probability>c.distribution.branches[c.branch].probability)c.branch=i;
        c.state=movement::trajectory::CoveredHorizon(c.distribution.branches[c.branch],c.probability,c.radius)>0?AimObservation::Availability::Available:AimObservation::Availability::Uncertainty;
    }else if(c.policy.selection!=movement::trajectory::Selection::Imported&&(!c.pool->models.empty()||(c.candidates&&!c.candidates->models.empty())))c.state=AimObservation::Availability::Available;
    return c;
}
bool ObserverPoint(const ObserverModel&c,double time,const movement::Vec&,movement::Vec&out){
    if(c.state!=AimObservation::Availability::Available)return false;
    const auto&a=c.anchor.past[2];const float h=float(time-a.time);movement::Vec displacement{};
    if(c.distribution.valid){const auto&b=c.distribution.branches[c.branch];float radius=0;
        if(h>movement::trajectory::CoveredHorizon(b,c.probability,c.radius)||!movement::trajectory::Interpolate(b,h,displacement,radius))return false;
    }else{
        movement::Features features;if(!movement::MakeFeatures(c.anchor,h,features))return false;
        const uint64_t id=c.policy.selection==movement::trajectory::Selection::Global?0:a.player;
        movement::Prediction prediction;
        if(c.experimental&&c.candidates)prediction=movement::PredictExperimental(*c.candidates,*c.pool,id,features);
        else {const movement::Pool empty;prediction=movement::PredictExperimental(empty,*c.pool,id,features);}
        if(!prediction.valid)return false;displacement=prediction.displacement;
    }
    for(int k=0;k<3;++k){out[k]=a.position[k]+displacement[k];if(!std::isfinite(out[k])||std::abs(out[k])>16384.f)return false;}
    return true;
}
void Draw(){
 using namespace ImGui;PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(8,8));PushStyleColor(ImGuiCol_Text,ImVec4(.90f,.91f,.93f,1.f));
 bool collect=Vars::NeuralMovement::Collect.Value;if(Checkbox("Learn while playing",&collect)){Vars::NeuralMovement::Collect.Value=Vars::NeuralMovement::Collect.Map[DEFAULT_BIND]=collect;Frame();}
 Hint("Collects and saves automatically. Training is explicit; existing models stay usable until a new one is trained.");
 int mode=ForceEnabled()?2:Vars::NeuralMovement::Use.Value?1:0;const char*modes[]={"Off","Auto","Force neural"};
 if(Combo("Neural aiming",&mode,modes,3)){Vars::NeuralMovement::Force.Value=Vars::NeuralMovement::Force.Map[DEFAULT_BIND]=mode==2;Vars::NeuralMovement::Use.Value=Vars::NeuralMovement::Use.Map[DEFAULT_BIND]=mode==1;lastModelAccount=0;lastModel="Waiting for a current prediction";reason="waiting for a supported target";Frame();}
 if(mode==2)TextWrapped("Experimental. Needs a trained model; unavailable predictions stop automated shots.");
 else Hint(mode==1?"Ordinary prediction aims first, with learned strafe timing when that model is accepted. A learned path is tried only if ordinary prediction finds no shot.":"Ordinary aiming controls are unchanged.");
 if(HasIndividualForce())Hint("Some players have a saved Force neural override.");
 try{
  auto store=Datasets();auto files=store?store->GetState():movement::datasets::State{};
  auto s=Get(true);auto rich=Rich();
  static movement::Status shownLegacy;static movement::trajectory::Status shownRich;
  if(s)shownLegacy=s->GetStatus();if(rich)shownRich=rich->GetStatus();
  const auto&state=shownLegacy;const auto&rs=shownRich;
  Spacing();Separator();
  BeginChild("Learning status",ImVec2(0,GetTextLineHeightWithSpacing()*5),ImGuiChildFlags_None,ImGuiWindowFlags_NoSavedSettings);
  if(files.busy||dataBusy)TextWrapped("%s",files.message.c_str());
  else if(state.state=="Starting"||rs.operation=="Starting")TextUnformatted("Loading saved observations and models.");
  else if(state.training||rs.training)TextUnformatted("Training saved observations. Learning continues in the background.");
  else if(collect||HasTraining())TextUnformatted("Collecting and saving cue samples. Request training explicitly to update models.");
  else TextUnformatted("Learning paused. Saved observations and models are kept.");
  if(!files.error.empty())TextWrapped("%s",files.error.c_str());else if(!rs.error.empty())TextWrapped("%s",rs.error.c_str());else if(!state.error.empty())TextWrapped("%s",state.error.c_str());
  else if(mode&&state.state!="Starting"&&rs.operation!="Starting"){const bool savedModel=(s&&(!s->Published()->models.empty()||!s->Candidates()->models.empty()))||(rich&&(!rich->Published()->local.empty()||!rich->Published()->imported.empty()));if(!lastModelAccount&&savedModel)TextWrapped("Saved models are loaded. Waiting for a supported target; each prediction still needs coverage and geometry checks.");else if(!lastModelAccount&&!state.examples&&!rs.examples)TextWrapped(collect?"Waiting for observations. Collect new cue samples, then use Train saved observations now.":"No observations yet. Enable Learn while playing or load a saved dataset.");else TextWrapped("Last prediction: %s. %s",lastModel.load(),reason.load());}
  EndChild();
  Hint("Player-specific learning: right-click a player, then Learning & prediction.");
  Hint("Neural prediction supports player targets with rockets, arrows/bolts, grenades, stickies, flares, primary energy shots, syringes, Dragon's Fury, cleavers and jars. It still requires trained path coverage and collision validation.");
  if(TreeNode("Details / Advanced")){
   Text("Saved legacy examples: %llu | engine pairs: %llu",state.examples,state.paired);Text("Saved sequence anchors: %llu | labels: %llu | movesim pairs: %llu",rs.anchors,rs.examples,rs.paired);
   Text("Strafe timing landmarks: %llu | model: %s",rs.strafeRows,rich&&rich->StrafeModel()->trained?(rich->StrafeModel()->accepted?"accepted, used in Auto":"trained, not better than baseline (unused)"):"not trained");TextWrapped("%s",ReversalReason());
   Text("Accepted models: %zu | legacy candidates: %zu | sequence models: %zu",state.models,state.candidates,rs.models);
   Text("Queued: %zu / %zu | dropped: %llu / %llu",state.queued,rs.queued,state.dropped,rs.dropped);
   BeginChild("Training operations",ImVec2(0,GetTextLineHeightWithSpacing()*3),ImGuiChildFlags_None,ImGuiWindowFlags_NoSavedSettings);TextWrapped("Legacy: %s",state.state.c_str());TextWrapped("Sequences: %s",rs.operation.c_str());EndChild();
   int samples=Vars::NeuralMovement::TrainingSamples.Value;if(StableControls::BoundedInt("Training examples",samples,1024,1000000)){Vars::NeuralMovement::TrainingSamples.Value=Vars::NeuralMovement::TrainingSamples.Map[DEFAULT_BIND]=samples;Frame();}
   bool all=Vars::NeuralMovement::AllSamples.Value;if(Checkbox("Use all available examples",&all)){Vars::NeuralMovement::AllSamples.Value=Vars::NeuralMovement::AllSamples.Map[DEFAULT_BIND]=all;Frame();}
   bool bots=Vars::NeuralMovement::IncludeBots.Value;if(Checkbox("Include bots in training",&bots)){Vars::NeuralMovement::IncludeBots.Value=Vars::NeuralMovement::IncludeBots.Map[DEFAULT_BIND]=bots;Frame();}
   Hint("Bots move unlike people, so global models leave them out by default. Turn on for bot-only practice data.");
   TextWrapped("Both trainers stream the saved dataset without a total file-size cap. Use all includes every eligible snapshot record. Selection otherwise samples across the whole snapshot. Free disk space is needed for temporary training files.");
   BeginDisabled(!s||files.busy);if(Button("Train saved observations now")){s->Request(movement::Action::Train);if(rich)rich->Train();}SameLine();if(Button("Cancel training")){s->Request(movement::Action::Cancel);if(rich)rich->Cancel();}EndDisabled();
   if(TreeNode("Legacy evaluation")){BeginChild("Legacy report",ImVec2(0,GetTextLineHeightWithSpacing()*8));TextWrapped("%s",state.evaluation.empty()?"No evaluation report yet.":state.evaluation.c_str());EndChild();TreePop();}if(TreeNode("Sequence evaluation")){BeginChild("Sequence report",ImVec2(0,GetTextLineHeightWithSpacing()*8));TextWrapped("%s",rs.evaluation.empty()?"No evaluation report yet.":rs.evaluation.c_str());EndChild();TreePop();}if(TreeNode("Strafe timing evaluation")){BeginChild("Strafe report",ImVec2(0,GetTextLineHeightWithSpacing()*6));TextWrapped("%s",rs.strafeEvaluation.empty()?"No strafe timing report yet. Train saved observations after collecting strafing players.":rs.strafeEvaluation.c_str());EndChild();TreePop();}
        float probability=Vars::NeuralMovement::MinimumPathProbability.Value;if(SliderFloat("Minimum path probability",&probability,0.f,100.f,"%.0f%%"))Vars::NeuralMovement::MinimumPathProbability.Value=Vars::NeuralMovement::MinimumPathProbability.Map[DEFAULT_BIND]=probability;
        float uncertainty=Vars::NeuralMovement::MaximumPathUncertainty.Value;if(SliderFloat("Maximum path uncertainty",&uncertainty,1.f,512.f,"%.0f units"))Vars::NeuralMovement::MaximumPathUncertainty.Value=Vars::NeuralMovement::MaximumPathUncertainty.Map[DEFAULT_BIND]=uncertainty;
        const char*objectives[]={"Chance of any damage","Expected damage","Chance of kill"};int objective=Vars::NeuralMovement::SplashObjective.Value;if(Combo("Learned splash objective",&objective,objectives,3))Vars::NeuralMovement::SplashObjective.Value=Vars::NeuralMovement::SplashObjective.Map[DEFAULT_BIND]=objective;
        TextWrapped("Shot policy: %s",policyEvidence?"using observed chosen-outcome scores with occasional exploration":"collecting evidence: Auto explores least-tried actions");
        TextWrapped("Client tracker: %llu inferred target outcomes, %llu unknown, %llu attributed kills | explosion observer: %s",trackedKnown.load(),trackedUnknown.load(),trackedKills.load(),ClientOutcomes::ObserverAvailable()?"available":"unavailable");
        TextWrapped("Last attribution: %s",trackedReason.load());
        TextWrapped("Only isolated continuously observed rocket outcomes train this chooser. Terminal-zero labels are client-inferred, not server proof. Overlap, gaps, reflection and missing terminal evidence stay unknown. Expected damage means selected-target damage.");
   TreePop();
  }
  if(TreeNode("Data management")){
   Hint("The active database resumes between sessions and saves automatically. Named databases contain observations, not trained models.");
    if(!files.active.empty())Text("Active database: %s",files.active.c_str());
   if(!files.storage.empty())TextWrapped("%s",files.storage.c_str());
   static char name[49]{};static std::string selected,confirmedName;static int confirmation=0;bool openConfirmation=false;
    if(selected.empty()&&!files.active.empty()){selected=files.active;strncpy_s(name,selected.c_str(),_TRUNCATE);}
   InputText("Dataset name",name,sizeof name);
   if(BeginListBox("Saved datasets",ImVec2(0,100))){for(const auto&item:files.names)if(Selectable(item.c_str(),selected==item)){selected=item;strncpy_s(name,item.c_str(),_TRUNCATE);}EndListBox();}
   BeginDisabled(files.busy||!store);
   BeginDisabled(!files.ready);if(Button("Save dataset")){confirmedName=name;if(std::any_of(files.names.begin(),files.names.end(),[&](const std::string&item){return !_stricmp(item.c_str(),name);})){confirmation=1;openConfirmation=true;}else if(store)store->Request(movement::datasets::Action::Save,name);}EndDisabled();
   SameLine();BeginDisabled(selected.empty());if(Button("Load dataset")){confirmedName=selected;confirmation=2;openConfirmation=true;}SameLine();if(Button("Delete dataset")){confirmedName=selected;confirmation=3;openConfirmation=true;}EndDisabled();EndDisabled();
   const char*dialog=confirmation==1?"Overwrite dataset###NeuralDatasetConfirm":confirmation==2?"Load dataset###NeuralDatasetConfirm":"Delete dataset###NeuralDatasetConfirm";
   if(openConfirmation)OpenPopup(dialog);
   if(StableControls::BeginConfirmation(dialog)){
    TextWrapped("Dataset: %s",confirmedName.c_str());Separator();
    TextWrapped(confirmation==2?"Replace all active observation files. Current observations are backed up. Models and player settings stay unchanged.":confirmation==1?"Replace this saved dataset with current observations. Its previous version is backed up.":"Remove this dataset from the saved list. A recoverable copy stays in Recovery/Datasets.");
    const char*action=confirmation==1?"Overwrite":confirmation==2?"Load dataset": "Delete dataset";
    BeginDisabled(files.busy||!store);
    if(Button(action,ImVec2(120,0))){if(store&&store->Request(confirmation==1?movement::datasets::Action::Save:confirmation==2?movement::datasets::Action::Load:movement::datasets::Action::Delete,confirmedName,confirmation==1)){confirmation=0;CloseCurrentPopup();}}EndDisabled();
    SameLine();if(Button("Cancel",ImVec2(100,0))||IsKeyPressed(ImGuiKey_Escape)){confirmation=0;CloseCurrentPopup();}
    EndPopup();
   }
   BeginChild("Dataset status",ImVec2(0,GetTextLineHeightWithSpacing()*2),ImGuiChildFlags_None,ImGuiWindowFlags_NoSavedSettings);
   TextWrapped("%s",files.message.c_str());EndChild();
   if(TreeNode("Model transfer / legacy tools")){BeginDisabled(!s||files.busy);
        if(Button("Import user-supplied shot outcomes"))if(auto rich=Rich())rich->ImportOutcomes();
        static int catalog=1;InputInt("Imported sequence catalog ID",&catalog);if(catalog>0&&Button("Load trained sequence model"))if(auto rich=Rich())rich->Import(unsigned(catalog));
        TextWrapped("Import path: LearningCompressed/Transfers/SequenceImport/<catalog ID>.txt. Imports are trained but unvalidated and remain separate from accepted models.");
        static bool identified=false;Checkbox("Include player IDs / personal models in transfer",&identified);
        if(Button("Export learning files"))s->Request(identified?movement::Action::ExportIdentified:movement::Action::ExportAnonymous);
        SameLine();if(Button("Validate / import learning files"))s->Request(identified?movement::Action::ImportIdentified:movement::Action::ImportAnonymous);
        TextWrapped("Exports: Nikogram/LearningCompressed/Transfers/Exports. To import, place models.txt and observations.txt in LearningCompressed/Transfers/Import. Imported models need local validation; foreign evaluation claims cannot enable aiming.");
        if(Button("Reset legacy learning"))OpenPopup("Reset legacy learning?");if(StableControls::BeginConfirmation("Reset legacy learning?")){TextWrapped("Reset legacy v1 models/data only. Sequence data, models and playerlist assignments remain. Original v1 files are archived in Recovery/ResetBackups.");if(Button("Confirm reset")){Vars::NeuralMovement::Collect.Value=Vars::NeuralMovement::Collect.Map[DEFAULT_BIND]=false;Vars::NeuralMovement::Force.Value=Vars::NeuralMovement::Force.Map[DEFAULT_BIND]=false;Vars::NeuralMovement::Use.Value=Vars::NeuralMovement::Use.Map[DEFAULT_BIND]=false;Frame();s->Request(movement::Action::ResetAll);CloseCurrentPopup();}SameLine();if(Button("Cancel##all"))CloseCurrentPopup();EndPopup();}
    EndDisabled();TreePop();
   }
   TreePop();
  }
 }catch(const std::exception&e){TextWrapped("Learning unavailable: %s",e.what());}catch(...){TextUnformatted("Learning temporarily unavailable.");}
 PopStyleColor();PopStyleVar();
}

void ResetSession(){ClientOutcomes::Reset();{std::lock_guard shotLock(shotMutex);opportunity={};committed.clear();}std::lock_guard lock(front);ResetTracks();++session;wasActive=false;channel=0;server=-1;predictor=ForceEnabled()?"experimental neural only: no automated solution":"ordinary movesim";reason="waiting for fresh network history";if(auto s=Get())s->InvalidateHistory();if(auto rich=richService.load())rich->ResetHistory();}
void Shutdown(){stopped=true;if(auto store=datasetStore.exchange({}))store->Stop();ClientOutcomes::Shutdown();std::lock_guard lifecycle(serviceLifetime);if(auto s=service.exchange({}))s->Stop();if(auto s=richService.exchange({}))s->Stop();}
}
