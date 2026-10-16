#include "../../SDK/SDK.h"
#include "NeuralMovement.h"
#include "ClientOutcomeBinding.h"
#include "ExplosionObserver.h"
#include "../../NeuralCore/outcome_tracker.h"
#include <deque>
#include <mutex>
#include <chrono>
#include <limits>

namespace NeuralMovement::ClientOutcomes {
namespace {
namespace ot=movement::outcomes;
ot::Tracker tracker;std::mutex mutex;
uint32_t targetHandle=0,lastLocal=0;int targetUser=0,targetSpawn=-1,lastServer=-1,lastSpawn=-1,lastSequence=-1;
uintptr_t lastChannel=0;uint64_t lastShotID=0;
double selectedAt=0,lastReceived=0;
struct NativeKey {int server,command,weapon;bool operator==(const NativeKey&)const=default;};
std::deque<NativeKey>nativeKeys;
enum class Kind { Explosion,Hurt,Death };
struct PendingEvent {
    Kind kind{};double time=0;movement::Vec origin{};
    int weapon=0,item=-1,victim=0,attacker=0,damage=0,health=0,inflictor=0,flags=0;
    bool reliable=false;
};
std::array<PendingEvent,64>events{};size_t eventCount=0;bool eventOverflow=false;
movement::Vec Vec(const Vec3&v){return {v.x,v.y,v.z};}
double Now(){return I::GlobalVars?I::GlobalVars->realtime:0.;}
bool Enabled(){return Vars::NeuralMovement::Collect.Value||HasIndividualTraining()||HasIndividualForce()||Vars::Aimbot::Projectile::SplashPrediction.Value==Vars::Aimbot::Projectile::SplashPredictionEnum::Learned;}
void Drain(){ot::Completed done;while(tracker.Take(done))TrackedOutcome(done);}
void ResetLocked(ot::Reason reason=ot::Reason::Cancelled){
    tracker.Reset(reason);Drain();targetHandle=lastLocal=0;targetUser=0;targetSpawn=lastServer=lastSpawn=lastSequence=-1;
    lastChannel=0;selectedAt=lastReceived=0;nativeKeys.clear();eventCount=0;eventOverflow=false;
    // Shot IDs remain monotonic across session resets and wall-clock adjustments.
}
void Enqueue(const PendingEvent&e){if(eventCount<events.size())events[eventCount++]=e;else eventOverflow=true;}
void Explosion(const ExplosionObserver::Event&e)noexcept{
    // PostDataUpdate runs during packet parsing: copy only, never correlate or publish here.
    try{std::lock_guard lock(mutex);PendingEvent pending;pending.kind=Kind::Explosion;pending.time=Now();pending.origin=e.origin;pending.weapon=e.weaponID;pending.item=e.itemDefinition;Enqueue(pending);}catch(...){}
}
bool Supported(CTFWeaponBase*w){return w&&(w->GetWeaponID()==TF_WEAPON_ROCKETLAUNCHER||w->GetWeaponID()==TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT)&&w->m_iItemDefinitionIndex()!=Soldier_m_TheBeggarsBazooka&&w->m_iItemDefinitionIndex()!=Soldier_m_RocketJumper;}
bool Projectile(ETFClassID id){switch(id){
        case ETFClassID::CBaseProjectile:
        case ETFClassID::CBaseGrenade:
        case ETFClassID::CTFWeaponBaseGrenadeProj:
        case ETFClassID::CTFWeaponBaseMerasmusGrenade:
        case ETFClassID::CTFGrenadePipebombProjectile:
        case ETFClassID::CTFStunBall:
        case ETFClassID::CTFBall_Ornament:
        case ETFClassID::CTFProjectile_Jar:
        case ETFClassID::CTFProjectile_Cleaver:
        case ETFClassID::CTFProjectile_JarGas:
        case ETFClassID::CTFProjectile_JarMilk:
        case ETFClassID::CTFProjectile_SpellBats:
        case ETFClassID::CTFProjectile_SpellKartBats:
        case ETFClassID::CTFProjectile_SpellMeteorShower:
        case ETFClassID::CTFProjectile_SpellMirv:
        case ETFClassID::CTFProjectile_SpellPumpkin:
        case ETFClassID::CTFProjectile_SpellSpawnBoss:
        case ETFClassID::CTFProjectile_SpellSpawnHorde:
        case ETFClassID::CTFProjectile_SpellSpawnZombie:
        case ETFClassID::CTFProjectile_SpellTransposeTeleport:
        case ETFClassID::CTFProjectile_Throwable:
        case ETFClassID::CTFProjectile_ThrowableBreadMonster:
        case ETFClassID::CTFProjectile_ThrowableBrick:
        case ETFClassID::CTFProjectile_ThrowableRepel:
        case ETFClassID::CTFBaseRocket:
        case ETFClassID::CTFFlameRocket:
        case ETFClassID::CTFProjectile_Arrow:
        case ETFClassID::CTFProjectile_GrapplingHook:
        case ETFClassID::CTFProjectile_HealingBolt:
        case ETFClassID::CTFProjectile_Rocket:
        case ETFClassID::CTFProjectile_BallOfFire:
        case ETFClassID::CTFProjectile_MechanicalArmOrb:
        case ETFClassID::CTFProjectile_SentryRocket:
        case ETFClassID::CTFProjectile_SpellFireball:
        case ETFClassID::CTFProjectile_SpellLightningOrb:
        case ETFClassID::CTFProjectile_SpellKartOrb:
        case ETFClassID::CTFProjectile_EnergyBall:
        case ETFClassID::CTFProjectile_Flare:
        case ETFClassID::CTFBaseProjectile:
        case ETFClassID::CTFProjectile_EnergyRing:

        return true;
    default:return false;
}}
bool LocalProjectile(CBaseEntity*entity,CTFPlayer*local){
    if(entity->m_hOwnerEntity().Get()==local)return true;
    switch(entity->GetClassID()){
    case ETFClassID::CBaseGrenade:case ETFClassID::CTFWeaponBaseGrenadeProj:
    case ETFClassID::CTFWeaponBaseMerasmusGrenade:case ETFClassID::CTFGrenadePipebombProjectile:
        return entity->As<CBaseGrenade>()->m_hThrower().Get()==local;
    default:return false;
    }
}
CTFPlayer* SelectedTarget(){
    auto entity=targetHandle?reinterpret_cast<CBaseEntity*>(I::ClientEntityList->GetClientEntityFromHandle(targetHandle)):nullptr;
    if(!entity||!entity->IsPlayer())return nullptr;
    auto player=entity->As<CTFPlayer>();player_info_t info{};
    return player->m_iSpawnCounter()==targetSpawn&&I::EngineClient->GetPlayerInfo(player->entindex(),&info)&&info.userID==targetUser?player:nullptr;
}
void DeliverEvents(CTFPlayer*local){
    if(eventOverflow){ResetLocked(ot::Reason::Overflow);return;}
    const auto count=eventCount;eventCount=0;
    for(size_t i=0;i<count;++i){
        const auto&e=events[i];if(e.time<selectedAt)continue;
        if(e.kind==Kind::Explosion){tracker.OnExplosion({e.time,e.origin,e.weapon,e.item});continue;}
        if(e.victim!=targetUser)continue;
        auto target=SelectedTarget();
        if(!target||target->IsDormant()){tracker.Reset(ot::Reason::TargetUnavailable);continue;}
        const bool localAttacker=I::EngineClient->GetPlayerForUserID(e.attacker)==local->entindex();
        if(!localAttacker){tracker.Reset(e.kind==Kind::Hurt?ot::Reason::DamageConflict:ot::Reason::UnknownDeath);continue;}
        if(e.kind==Kind::Hurt){
            if(e.victim==e.attacker)continue;
            tracker.OnHurt({e.time,uint32_t(local->GetRefEHandle().ToInt()),e.victim,e.weapon,e.damage,e.health,Vec(target->m_vecOrigin()),e.reliable});
        }else tracker.OnDeath({e.time,e.victim,e.inflictor,bool(e.flags&32),e.reliable});
    }
}
}
bool ObserverAvailable()noexcept{return ExplosionObserver::Available();}
void Frame()noexcept{
    try{
        if(!Enabled()){Reset();return;}
        ExplosionObserver::Initialize(Explosion);std::lock_guard lock(mutex);
        if(eventOverflow){ResetLocked(ot::Reason::Overflow);return;}
        // Settlement requires a fresh received batch, never render-frame polling.
        if(tracker.Pending()&&Now()-lastReceived>.15)ResetLocked(ot::Reason::Gap);
        Drain();
    }catch(...){}
}
void Reset()noexcept{try{std::lock_guard lock(mutex);ResetLocked();}catch(...){}}
void Shutdown()noexcept{ExplosionObserver::Shutdown();Reset();}
void NativeFire(CTFPlayer*local,CTFWeaponBase*weapon,int command,bool selected,uint32_t target,const movement::trajectory::Outcome&selection)noexcept{
    try{
        if(!local||!weapon||!Enabled())return;
        std::lock_guard lock(mutex);NativeKey key{I::ClientState->m_nServerCount,command,weapon->GetRefEHandle().ToInt()};
        if(std::find(nativeKeys.begin(),nativeKeys.end(),key)!=nativeKeys.end())return;
        if(nativeKeys.size()>=512)nativeKeys.pop_front();nativeKeys.push_back(key);
        ot::Launch launch;launch.time=Now();launch.local=uint32_t(local->GetRefEHandle().ToInt());launch.weapon=uint32_t(key.weapon);launch.weaponID=weapon->GetWeaponID();launch.itemDef=weapon->m_iItemDefinitionIndex();launch.muzzle=Vec(local->GetShootPos());
        auto entity=target?reinterpret_cast<CBaseEntity*>(I::ClientEntityList->GetClientEntityFromHandle(target)):nullptr;player_info_t info{};
        const int mode=int(selection.chosen);
        launch.selected=selected&&Supported(weapon)&&selection.weapon==unsigned(launch.weaponID)
            &&selection.context.itemDef>=0&&selection.context==ShooterWeaponContext(local,weapon)
            &&entity&&entity->IsPlayer()&&I::EngineClient->GetPlayerInfo(entity->entindex(),&info)&&mode>=1&&mode<=3;
        if(launch.selected){launch.target=target;launch.targetUser=info.userID;auto player=entity->As<CTFPlayer>();launch.targetState={target,info.userID,player->m_iHealth(),player->m_iClass(),true,player->IsDormant(),player->IsAlive(),Vec(player->m_vecOrigin())};targetHandle=target;targetUser=info.userID;targetSpawn=player->m_iSpawnCounter();selectedAt=launch.time;launch.blastRadius=SDK::AttribHookValue(146.f,"mult_explosion_radius",weapon);
            const auto clock=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            if(lastShotID==std::numeric_limits<uint64_t>::max()){ResetLocked(ot::Reason::Overflow);return;}
            lastShotID=(std::max)(clock>0?uint64_t(clock):uint64_t(1),lastShotID+1);launch.chosen=selection;launch.chosen.shot=lastShotID;
            launch.blastRadius=selection.context.radius;}
        tracker.NativeFire(launch);Drain();
    }catch(...){}
}
void Network()noexcept{
    try{
        std::lock_guard lock(mutex);auto local=H::Entities.GetLocal();auto weapon=H::Entities.GetWeapon();auto channel=I::EngineClient->GetNetChannelInfo();
        if(!Enabled()||!local||!weapon||!channel||!I::EngineClient->IsInGame()||I::EngineClient->IsPlayingDemo()){ResetLocked();return;}
        const int server=I::ClientState->m_nServerCount,spawn=local->m_iSpawnCounter();const auto localHandle=uint32_t(local->GetRefEHandle().ToInt());const auto channelID=reinterpret_cast<uintptr_t>(channel);
        if(lastServer!=server||lastSpawn!=spawn||lastChannel!=channelID||lastLocal!=localHandle)ResetLocked(ot::Reason::ContextChanged);
        lastServer=server;lastSpawn=spawn;lastChannel=channelID;lastLocal=localHandle;
        const int sequence=channel->GetSequenceNr(FLOW_INCOMING);
        // Render frames without new received data are not independent observations.
        if(sequence==lastSequence)return;
        ot::Batch batch;batch.time=Now();batch.local=localHandle;batch.weapon=uint32_t(weapon->GetRefEHandle().ToInt());batch.weaponID=weapon->GetWeaponID();batch.itemDef=weapon->m_iItemDefinitionIndex();
        batch.eligibleSoldierLauncher=local->IsAlive()&&local->m_iClass()==TF_CLASS_SOLDIER&&Supported(weapon);
        batch.lossFree=channel->GetAvgLoss(FLOW_INCOMING)==0.f&&channel->GetAvgLoss(FLOW_OUTGOING)==0.f&&(lastSequence<0||sequence>lastSequence);lastSequence=sequence;lastReceived=batch.time;
        batch.settleDelay=channel->GetLatency(FLOW_INCOMING)+channel->GetLatency(FLOW_OUTGOING)+.1f;
        batch.complete=ExplosionObserver::Available();std::array<ot::Rocket,128>rockets{};size_t count=0;
        // Filtered WorldProjectile omits dormant entities. Raw inventory is required
        // before launch too, so an old re-entering rocket cannot appear newly born.
        constexpr int EntityLimit=8192;const int highest=I::ClientEntityList->GetHighestEntityIndex();
        if(highest<0||highest>=EntityLimit)batch.complete=false;
        const auto bound=tracker.BoundHandle();const auto scanStarted=std::chrono::steady_clock::now();
        for(int index=1;index<=(std::min)(highest,EntityLimit-1);++index){
            // Cooperative diagnostic budget; any incomplete scan censors the batch.
            if(index%32==0&&std::chrono::steady_clock::now()-scanStarted>=std::chrono::microseconds(250)){batch.complete=false;break;}
            auto unknown=I::ClientEntityList->GetClientEntity(index);auto entity=unknown?unknown->As<CBaseEntity>():nullptr;
            if(!entity||!Projectile(entity->GetClassID()))continue;
            const bool owned=LocalProjectile(entity,local),wasBound=uint32_t(entity->GetRefEHandle().ToInt())==bound;
            if(!owned&&!wasBound)continue;
            if(entity->GetClassID()!=ETFClassID::CTFProjectile_Rocket){batch.complete=false;continue;}
            if(count>=rockets.size()){batch.complete=false;continue;}
            auto rocket=entity->As<CTFBaseRocket>();auto&r=rockets[count++];r.handle=uint32_t(entity->GetRefEHandle().ToInt());r.index=entity->entindex();r.owner=uint32_t(rocket->m_hOwnerEntity().ToInt());r.launcher=uint32_t(rocket->m_hLauncher().ToInt());r.deflected=rocket->m_iDeflected();r.dormant=entity->IsDormant();r.position=Vec(rocket->m_vecOrigin());r.velocity=Vec(rocket->m_vInitialVelocity());
        }
        auto player=SelectedTarget();
        if(player){batch.target.handle=targetHandle;batch.target.present=true;batch.target.user=targetUser;batch.target.health=player->m_iHealth();batch.target.playerClass=player->m_iClass();batch.target.alive=player->IsAlive();batch.target.dormant=player->IsDormant();batch.target.position=Vec(player->m_vecOrigin());
            if(player->InCond(TF_COND_BURNING)||player->InCond(TF_COND_BLEEDING)||player->InCond(TF_COND_HEALTH_BUFF)||player->m_iClass()==TF_CLASS_MEDIC)batch.complete=false;
        }
        // Process copied events only after native packet processing has completed,
        // and before Observe can settle a pending outcome in this same batch.
        DeliverEvents(local);batch.rockets={rockets.data(),count};tracker.Observe(batch);Drain();
    }catch(...){try{std::lock_guard lock(mutex);ResetLocked(ot::Reason::InvalidInput);}catch(...){}}
}
void Event(IGameEvent*event)noexcept{
    try{
        if(!event||!Enabled()||I::EngineClient->IsPlayingDemo())return;
        const auto hash=FNV1A::Hash32(event->GetName());PendingEvent pending;pending.time=Now();pending.reliable=event->IsReliable();
        if(hash==FNV1A::Hash32Const("player_hurt")){
            pending.kind=Kind::Hurt;pending.victim=event->GetInt("userid");pending.attacker=event->GetInt("attacker");pending.weapon=event->GetInt("weaponid");pending.damage=event->GetInt("damageamount");pending.health=event->GetInt("health");
        }else if(hash==FNV1A::Hash32Const("player_death")){
            pending.kind=Kind::Death;pending.victim=event->GetInt("userid");pending.attacker=event->GetInt("attacker");pending.inflictor=event->GetInt("inflictor_entindex");pending.flags=event->GetInt("death_flags");
        }else return;
        std::lock_guard lock(mutex);Enqueue(pending);
    }catch(...){try{std::lock_guard lock(mutex);eventOverflow=true;}catch(...){}}
}
}
