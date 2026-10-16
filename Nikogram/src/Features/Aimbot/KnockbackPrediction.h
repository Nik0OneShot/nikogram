#pragma once
#include "KnockbackPolicy.h"
#include "ReceivedProjectileFlight.h"
#include "PipeContactPolicy.h"
#include "ExplicitPushPolicy.h"
#include "ReceivedFlyMotion.h"
#include "../Simulation/MovementSimulation/MovementSimulation.h"
#include "../Simulation/MovementSimulation/MovementImpact.h"
#include "../NeuralMovement/NeuralMovement.h"

namespace KnockbackPrediction {
inline KnockbackPolicy::V P(const Vec3& v){return {v.x,v.y,v.z};}
inline Vec3 V(KnockbackPolicy::V v){return {v[0],v[1],v[2]};}
inline bool Enabled(CTFPlayer* p){return p&&!NeuralMovement::ForceEnabled()&&!NeuralMovement::TargetForced(p)
    &&bool(Vars::Aimbot::Projectile::StrafePrediction.Value&Vars::Aimbot::Projectile::StrafePredictionEnum::Knockback);}
inline float Cvar(const char* name,float fallback){auto* c=H::ConVars.FindVar(name);return c?c->GetFloat():fallback;}
inline bool OrdinaryTarget(CTFPlayer* p){
    if(!p||!p->IsAlive()||p->IsDormant()||p->IsAGhost()||p->IsSwimming()||p->m_MoveType()!=MOVETYPE_WALK
        ||p->m_bIsMiniBoss()||p->IsInvulnerable()||p->InCond(TF_COND_DISGUISED)
        ||p->InCond(TF_COND_SHIELD_CHARGE)||p->InCond(TF_COND_PARACHUTE_ACTIVE)
        ||p->InCond(TF_COND_IMMUNE_TO_PUSHBACK)||p->InCond(TF_COND_MEGAHEAL)||p->InCond(TF_COND_HALLOWEEN_TINY)
        ||p->InCond(TF_COND_RUNE_RESIST)||p->InCond(TF_COND_RUNE_PRECISION)||p->InCond(TF_COND_RUNE_KNOCKOUT))return false;
    if(auto* rules=I::TFGameRules();rules&&rules->m_bPlayingMannVsMachine())return false;
    if(p->InCond(TF_COND_HASRUNE)||p->InCond(TF_COND_DEFENSEBUFF)||p->InCond(TF_COND_DEFENSEBUFF_NO_CRIT_BLOCK)
        ||p->InCond(TF_COND_DEFENSEBUFF_HIGH)||p->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST)||p->InCond(TF_COND_MEDIGUN_SMALL_BLAST_RESIST))return false;
    return SDK::AttribHookValue(0.f,"spunup_push_force_immunity",p)==0.f
        &&SDK::AttribHookValue(1.f,"mult_dmgtaken",p)==1.f
        &&SDK::AttribHookValue(1.f,"mult_dmgtaken_from_explosions",p)==1.f
        &&SDK::AttribHookValue(0.f,"no_knockback",p)==0.f;
}
// Native ApplyOnDamageModifyRules multipliers. A projectile's crit is fixed
// when it is created (m_bCritical); mini-crit sources are read at capture.
// Jarate/Mark-for-Death targets are covered here instead of being skipped.
struct DamageMode{bool crit=false,minicrit=false,hypothesis=false;};
inline DamageMode Mode(bool critical,CTFPlayer* attacker,CTFPlayer* target,int deflected){
    DamageMode m;m.crit=critical;
    if(!m.crit){
        if(attacker&&attacker->IsMiniCritBoosted())m.minicrit=true;
        if(target&&target->IsMarked())m.minicrit=true;
        // Reflected rockets/pipes mini-crit in public TF2.
        if(deflected>0)m.minicrit=true;
    }
    // Not verified against this build, and reflected velocity is estimated:
    // keep every reflected encounter a hypothesis, crit or not.
    if(deflected>0)m.hypothesis=true;
    return m;
}
inline float ModeScale(bool crit,bool minicrit){return crit?3.f:minicrit?1.35f:1.f;}
// All native calls run synchronously inside the command's existing solver scope.
// No server entity is mutated, no command-only projectile is invented, and no
// cross-command damage promise survives a despawn, deflection or new snapshot.
struct Snapshot {
    struct Rocket {
        CBaseEntity* entity=nullptr;unsigned handle=0,owner=0,launcher=0;int team=0,deflected=0;
        Vec3 origin,velocity,attackerCenter,attackerOrigin,worldPoint,normal;
        float sim=0,worldTime=0,damage=0,radius=0,selfDamage=0,selfMultiplier=1;
        bool world=false,self=false,noClose=false,stationaryAttacker=false;
        bool pipe=false,touched=false,direct=false,cannon=false,scorch=false,burning=false;
        bool crit=false,minicrit=false,hypothesis=false,sentry=false,fixedDamage=false;int classId=0;
        // charged: Cow Mangler charged shot (native push x1.25). arrow/bolt: Huntsman
        // arrow (push has no Z) / Crusader's Crossbow bolt (damage grows with age).
        bool charged=false,arrow=false,bolt=false;float seen=0;
        float fuseDeadline=std::numeric_limits<float>::infinity();Vec3 inflictorCenter;std::shared_ptr<const ReceivedProjectileFlight::Path> flight;
        ReceivedProjectileFlight::Key flightKey;
    };
    CTFPlayer* target=nullptr;unsigned handle=0;int spawn=0,server=0;void* channel=nullptr;
    Vec3 origin,mins,maxs;float anchor=0,observed=0,range=.5f,reduction=1;
    std::array<Rocket,KnockbackPolicy::MaxProjectiles> rockets{};size_t count=0;
    bool complete=true;
    class Filter:public CTraceFilterCollideable {
    public:IHandleEntity* victim=nullptr;
        bool ShouldHitEntity(IHandleEntity* e,int mask)override{return e!=victim&&CTraceFilterCollideable::ShouldHitEntity(e,mask);}
    };
    // First simulation time each pipe was received, per server/channel.
    static float FirstSeen(unsigned handle,float sim){
        struct Entry{unsigned handle=0;float seen=0;};
        struct Cache{int server=-1;void* channel=nullptr;std::array<Entry,256> entries{};};static Cache cache;
        auto* channel=I::EngineClient->GetNetChannelInfo();
        if(cache.server!=I::ClientState->m_nServerCount||cache.channel!=channel){cache={};cache.server=I::ClientState->m_nServerCount;cache.channel=channel;}
        auto& e=cache.entries[handle%cache.entries.size()];
        if(e.handle!=handle||sim<e.seen){e.handle=handle;e.seen=sim;}
        return e.seen;
    }
    void CapturePipe(CBaseEntity* entity,float now){
        auto* pipe=entity->As<CTFGrenadePipebombProjectile>();
        // Remote detonation is a future owner input. Cannon contact is a
        // separate penetrating push plus movement stun, never direct explosion.
        const bool cannon=pipe->m_iType()==TF_GL_MODE_CANNONBALL;
        if(pipe->m_iType()!=TF_GL_MODE_REGULAR&&!cannon)return;
        auto* owner=pipe->m_iDeflected()?pipe->m_hDeflectOwner().Get():pipe->m_hThrower().Get();
        auto* source=pipe->m_hLauncher().Get();
        if(!owner||!owner->IsPlayer()||!source||!source->IsBaseCombatWeapon())return;
        auto* attacker=owner->As<CTFPlayer>();auto* weapon=source->As<CTFWeaponBase>();
        if(!attacker->IsAlive()||attacker->IsDormant()||entity->m_iTeamNum()==target->m_iTeamNum())return;
        if(weapon->GetWeaponID()!=(cannon?TF_WEAPON_CANNON:TF_WEAPON_GRENADELAUNCHER)
            ||attacker->InCond(TF_COND_HASRUNE)||SDK::AttribHookValue(0.f,"damage_causes_airblast",weapon)!=0
            ||SDK::AttribHookValue(1.f,"damage_blast_push",weapon)!=1)return;
        {
            // Both native radius damage and Cannon contact reach these shared
            // damage/status hooks. Hidden donk alternatives are Cannon-only.
            if((cannon&&pipe->m_iDeflected())||attacker->InCond(TF_COND_TMPDAMAGEBONUS)
                ||target->InCond(TF_COND_ENERGY_BUFF)||target->InCond(TF_COND_OBSCURED_SMOKE))return;
            auto* active=attacker->m_hActiveWeapon().Get();
            if(!active||!active->IsBaseCombatWeapon()||SDK::AttribHookValue(0.f,"stun_enemies_wielding_same_weapon",active)!=0.f)return;
            if(!cannon&&SDK::AttribHookValue(1.f,"bodyshot_damage_modify",active)!=1.f)return;
            for(const char* hook:{"mult_dmg_vs_players","mult_dmg_vs_same_class","mult_dmg_vs_nonburning"}){
                if(!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Prediction)){complete=false;return;}
                if(SDK::AttribHookValue(1.f,hook,weapon)!=1.f)return;
            }
            for(const char* hook:{"or_crit_vs_playercond","crit_vs_wet_players","or_crit_vs_not_playercond","axtinguisher_properties",
                "crit_while_airborne","air_dash_count","or_minicrit_vs_playercond_burning","mini_crit_airborne","closerange_backattack_minicrits",
                "set_dmgtype_ignite","stun_on_damage","aoe_blast_on_damage","mult_onhit_enemyspeed","mult_onhit_enemyspeed_major",
                "stun_waist_high_airborne","mark_for_death","minicrits_become_crits"}){
                if(!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Prediction)){complete=false;return;}
                if(SDK::AttribHookValue(0.f,hook,weapon)!=0.f)return;
            }
            if(SDK::AttribHookValue(0.f,"uber_on_damage_taken",target)!=0.f||SDK::AttribHookValue(0.f,"generate_rage_on_dmg",attacker)!=0.f)return;
        }
        const Vec3 start=pipe->m_vecOrigin(),velocity=pipe->CBaseGrenade::m_vecVelocity();
        const float sim=pipe->m_flSimulationTime();
        if(!KnockbackPolicy::Fresh(now-sim)||!KnockbackPolicy::Finite(P(start))||!KnockbackPolicy::Finite(P(velocity))
            ||velocity.Length()>4000||std::abs(sim-anchor)>.1f)return;
        if(start.DistTo(origin)>velocity.Length()*(KnockbackPolicy::Horizon+.1f)+600.f)return;
        if(count==rockets.size()){complete=false;return;}
        Rocket r;r.pipe=true;r.cannon=cannon;r.touched=pipe->m_bTouched();r.entity=entity;r.handle=entity->GetRefEHandle().ToInt();
        r.owner=owner->GetRefEHandle().ToInt();r.launcher=source->GetRefEHandle().ToInt();r.deflected=pipe->m_iDeflected();
        r.team=entity->m_iTeamNum();r.sim=sim;r.origin=start;r.velocity=velocity;r.attackerCenter=attacker->GetCenter();r.attackerOrigin=attacker->m_vecOrigin();
        {const auto mode=Mode(pipe->m_bCritical(),attacker,target,pipe->m_iDeflected());r.crit=mode.crit;r.minicrit=mode.minicrit;
         // Crit/mini-crit Cannon contact is not verified; keep it a hypothesis.
         r.hypothesis=mode.hypothesis||(cannon&&(mode.crit||mode.minicrit));}
        // Native Create stores timer damage at60%; untouched enemy touch restores
        // full damage. A touched physics contact keeps its current received damage.
        const float penalty=SDK::AttribHookValue(1.f,"grenade_detonation_damage_penalty",weapon);
        if(!std::isfinite(penalty)||penalty<=0.f||penalty>10.f)return;
        r.damage=cannon?50.f:pipe->m_flDamage()/(r.touched?1.f:.6f*penalty);
        r.radius=SDK::AttribHookValue(pipe->m_DmgRadius(),"mult_explosion_radius",weapon);
        if(!std::isfinite(r.damage)||r.damage<=0||r.damage>1000||!std::isfinite(r.radius)||r.radius<=0||r.radius>512)return;
        const auto* model=entity->GetModel();const auto* name=model?I::ModelInfoClient->GetModelName(model):nullptr;
        if(!name)return;
        ReceivedProjectileFlight::Key key{r.handle,r.owner,r.launcher,r.deflected,sim,F::ProjSim.GetGravity(entity,weapon),start,velocity,pipe->m_angRotation(),FNV1A::Hash32(name)};
        if(!std::isfinite(key.gravity)||key.gravity<0||!KnockbackPolicy::Finite(P(key.angles)))return;
        key.touched=r.touched;
        key.noBounce=SDK::AttribHookValue(0.f,"grenade_no_bounce",weapon)!=0.f;
        key.wallShatter=SDK::AttribHookValue(0.f,"set_detonate_mode",weapon)==2.f;
        if(cannon&&(target->InCond(TF_COND_STUNNED)||target->m_flStunLerpTarget()!=0.f))return;
        if(cannon){if(auto* rules=I::TFGameRules();rules&&rules->m_bTruceActive())return;}
        r.flightKey=key;r.flight=ReceivedProjectileFlight::Pipe(key,entity,attacker,weapon);
        // A pill resting or rolling on the ground often has no resolvable
        // path (a single node). Keep it: Pass bounds where it can still be.
        if(!r.flight||r.flight->count<1){complete=false;return;}
        // Native fuse: SetDetonateTimerLength(2.0) scaled by fuse_mult
        // (server.dll). It was created no later than we first saw it, so it
        // has exploded by firstSeen+fuse. Cannon fuses follow the owner's
        // charge and get no deadline.
        if(!cannon){
            const float fuse=2.f*SDK::AttribHookValue(1.f,"fuse_mult",weapon);
            const float seen=FirstSeen(unsigned(r.handle),sim);
            if(std::isfinite(fuse)&&fuse>0.f&&fuse<10.f&&std::isfinite(seen))r.fuseDeadline=seen+fuse+TICK_INTERVAL;
            if(sim>r.fuseDeadline)return; // already past its fuse: stale, cannot push
        }
        rockets[count++]=std::move(r);
    }
    void CaptureCleaver(CBaseEntity* entity,float now){
        auto* projectile=entity->As<CTFWeaponBaseGrenadeProj>();
        // Cleaver's current launcher is server-only. Original launcher is valid
        // only before any deflection; never substitute it after owner changes.
        if(projectile->m_iDeflected()||entity->m_usSolidFlags()&FSOLID_NOT_SOLID)return;
        auto* owner=projectile->m_hThrower().Get();auto* source=projectile->m_hOriginalLauncher().Get();
        if(!owner||!owner->IsPlayer()||!source||!source->IsBaseCombatWeapon()||owner==target||entity->m_iTeamNum()==target->m_iTeamNum())return;
        auto* attacker=owner->As<CTFPlayer>();auto* weapon=source->As<CTFWeaponBase>();
        if(weapon->GetWeaponID()!=TF_WEAPON_CLEAVER||weapon->m_hOwner().Get()!=owner||!attacker->IsAlive()||attacker->IsDormant()||projectile->m_bCritical()||attacker->IsCritBoosted()
            ||attacker->IsMiniCritBoosted()||attacker->InCond(TF_COND_HASRUNE)||target->InCond(TF_COND_INVULNERABLE_WEARINGOFF))return;
        if(SDK::AttribHookValue(0.f,"damage_causes_airblast",weapon)!=0||SDK::AttribHookValue(1.f,"damage_blast_push",weapon)!=1)return;
        if(auto* rules=I::TFGameRules();rules&&rules->m_bTruceActive())return;
        if(attacker->InCond(TF_COND_TMPDAMAGEBONUS)||target->InCond(TF_COND_ENERGY_BUFF)||target->InCond(TF_COND_OBSCURED_SMOKE))return;
        auto* active=attacker->m_hActiveWeapon().Get();
        if(!active||!active->IsBaseCombatWeapon()||SDK::AttribHookValue(1.f,"bodyshot_damage_modify",active)!=1.f
            ||SDK::AttribHookValue(0.f,"stun_enemies_wielding_same_weapon",active)!=0.f)return;
        for(const char* hook:{"mult_dmg_vs_players","mult_dmg_vs_same_class","mult_dmg_vs_nonburning"}){
            if(!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Prediction)){complete=false;return;}
            if(SDK::AttribHookValue(1.f,hook,weapon)!=1.f)return;
        }
        for(const char* hook:{"or_crit_vs_playercond","crit_vs_wet_players","or_crit_vs_not_playercond","axtinguisher_properties",
            "crit_while_airborne","air_dash_count","or_minicrit_vs_playercond_burning","mini_crit_airborne","closerange_backattack_minicrits",
            "set_dmgtype_ignite","stun_on_damage","aoe_blast_on_damage","mult_onhit_enemyspeed","mult_onhit_enemyspeed_major",
            "stun_waist_high_airborne","mark_for_death"}){
            if(!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Prediction)){complete=false;return;}
            if(SDK::AttribHookValue(0.f,hook,weapon)!=0.f)return;
        }
        if(SDK::AttribHookValue(0.f,"uber_on_damage_taken",target)!=0.f||SDK::AttribHookValue(0.f,"generate_rage_on_dmg",attacker)!=0.f)return;
        const Vec3 start=projectile->m_vecOrigin(),velocity=projectile->CBaseGrenade::m_vecVelocity();
        const float sim=projectile->m_flSimulationTime();
        if(!KnockbackPolicy::Fresh(now-sim)||std::abs(sim-anchor)>.1f||!KnockbackPolicy::Finite(P(start))
            ||!KnockbackPolicy::Finite(P(velocity))||velocity.Length()>10000)return;
        if(start.DistTo(origin)>velocity.Length()*(KnockbackPolicy::Horizon+.1f)+300.f)return;
        if(count==rockets.size()){complete=false;return;}
        const auto* model=entity->GetModel();const auto* name=model?I::ModelInfoClient->GetModelName(model):nullptr;if(!name)return;
        Rocket r;r.direct=true;r.entity=entity;r.handle=entity->GetRefEHandle().ToInt();r.owner=owner->GetRefEHandle().ToInt();
        // Flying Guillotine converts would-be mini-crits into crits (public TF2
        // behaviour, unverified here): a marked target is a crit hypothesis.
        if(target->IsMarked()){r.crit=true;r.hypothesis=true;}
        r.launcher=source->GetRefEHandle().ToInt();r.team=entity->m_iTeamNum();r.sim=sim;r.origin=start;r.velocity=velocity;
        r.inflictorCenter=source->GetCenter();r.attackerCenter=attacker->GetCenter();r.attackerOrigin=attacker->m_vecOrigin();r.damage=50.f;r.radius=1.f;
        if(!KnockbackPolicy::Finite(P(r.inflictorCenter)))return;
        ReceivedProjectileFlight::Key key{r.handle,r.owner,r.launcher,0,sim,F::ProjSim.GetGravity(entity,weapon),start,velocity,projectile->m_angRotation(),FNV1A::Hash32(name)};
        if(!std::isfinite(key.gravity)||key.gravity<0||!KnockbackPolicy::Finite(P(key.angles)))return;
        key.terminalWorld=true;r.flightKey=key;r.flight=ReceivedProjectileFlight::Pipe(key,entity,attacker,weapon);
        if(!r.flight||r.flight->count<2){complete=false;return;}rockets[count++]=std::move(r);
    }
    void CaptureScorch(CBaseEntity* entity,float now){
        auto* flare=entity->As<CTFProjectile_Flare>();auto* owner=flare->m_hOwnerEntity().Get();auto* source=flare->m_hLauncher().Get();
        if(!owner||!owner->IsPlayer()||!source||!source->IsBaseCombatWeapon()||owner==target||entity->m_iTeamNum()==target->m_iTeamNum())return;
        auto* attacker=owner->As<CTFPlayer>();auto* weapon=source->As<CTFWeaponBase>();
        if(weapon->GetWeaponID()!=TF_WEAPON_FLAREGUN||SDK::AttribHookValue(0.f,"set_weapon_mode",weapon)!=3.f
            ||entity->m_CollisionGroup()==COLLISION_GROUP_DEBRIS||entity->m_usSolidFlags()&FSOLID_NOT_SOLID
            ||entity->m_MoveType()!=MOVETYPE_FLYGRAVITY
            ||SDK::AttribHookValue(1.f,"mult_projectile_speed",weapon)!=1.f
            ||!attacker->IsAlive()||attacker->IsDormant()||target->InCond(TF_COND_STUNNED)||target->m_flStunLerpTarget()!=0.f)return;
        if(auto* rules=I::TFGameRules();rules&&rules->m_bTruceActive())return;
        auto* active=attacker->m_hActiveWeapon().Get();
        if(!active||!active->IsBaseCombatWeapon()||SDK::AttribHookValue(0.f,"stun_enemies_wielding_same_weapon",active)!=0.f)return;
        for(const char* hook:{"stun_on_damage","aoe_blast_on_damage","mult_onhit_enemyspeed","mult_onhit_enemyspeed_major","stun_waist_high_airborne"}){
            if(!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Prediction)){complete=false;return;}
            if(SDK::AttribHookValue(0.f,hook,weapon)!=0.f)return;
        }
        const Vec3 start=flare->m_vecOrigin();const float sim=flare->m_flSimulationTime(),gravity=F::ProjSim.GetGravity(entity,weapon);
        if(!KnockbackPolicy::Fresh(now-sim)||std::abs(sim-anchor)>.1f)return;
        const ReceivedFlyMotion::Identity identity{unsigned(entity->GetRefEHandle().ToInt()),unsigned(owner->GetRefEHandle().ToInt()),unsigned(source->GetRefEHandle().ToInt()),flare->m_iDeflected()};
        const auto velocity=ReceivedFlyMotion::Estimate(identity,sim,start,gravity);if(!velocity)return;
        if(start.DistTo(origin)>velocity->Length()*(KnockbackPolicy::Horizon+.1f)+300.f)return;
        if(count==rockets.size()){complete=false;return;}
        Rocket r;r.direct=true;r.scorch=true;r.burning=target->InCond(TF_COND_BURNING);r.entity=entity;r.handle=identity.handle;r.owner=identity.owner;r.launcher=identity.launcher;
        r.deflected=identity.deflections;r.team=entity->m_iTeamNum();r.sim=sim;r.origin=start;r.velocity=*velocity;r.radius=1.f;
        ReceivedProjectileFlight::Key key{r.handle,r.owner,r.launcher,r.deflected,sim,gravity,start,*velocity,flare->m_angRotation()};key.analytic=true;
        r.flightKey=key;r.flight=ReceivedProjectileFlight::Pipe(key,entity,attacker,weapon);
        if(!r.flight||r.flight->count<2){complete=false;return;}rockets[count++]=std::move(r);
    }
    // Huntsman arrows and Crusader's Crossbow bolts push on a direct hit
    // (CTFPlayer::ApplyPushFromDamage: tf_damageforcescale_other; the bow's push
    // has no Z). Headshot (crit) and bolt age are not observable, so every arrow
    // encounter stays a hypothesis: a shot must work with and without the push.
    void CaptureArrow(CBaseEntity* entity,float now){
        auto* arrow=entity->As<CTFProjectile_Arrow>();
        if(arrow->m_iDeflected()||entity->m_usSolidFlags()&FSOLID_NOT_SOLID||entity->m_MoveType()!=MOVETYPE_FLYGRAVITY)return;
        auto* owner=arrow->m_hOwnerEntity().Get();auto* source=arrow->m_hLauncher().Get();
        if(!owner||!owner->IsPlayer()||!source||!source->IsBaseCombatWeapon()||owner==target||entity->m_iTeamNum()==target->m_iTeamNum())return;
        auto* attacker=owner->As<CTFPlayer>();auto* weapon=source->As<CTFWeaponBase>();
        const int id=weapon->GetWeaponID();const bool bow=id==TF_WEAPON_COMPOUND_BOW,bolt=id==TF_WEAPON_CROSSBOW;
        if((!bow&&!bolt)||!attacker->IsAlive()||attacker->IsDormant()||attacker->InCond(TF_COND_HASRUNE))return;
        if(SDK::AttribHookValue(0.f,"damage_causes_airblast",weapon)!=0||SDK::AttribHookValue(1.f,"damage_blast_push",weapon)!=1)return;
        if(auto* rules=I::TFGameRules();rules&&rules->m_bTruceActive())return;
        const Vec3 start=arrow->m_vecOrigin();const float sim=entity->m_flSimulationTime(),gravity=F::ProjSim.GetGravity(entity,weapon);
        if(!KnockbackPolicy::Fresh(now-sim)||std::abs(sim-anchor)>.1f||!KnockbackPolicy::Finite(P(start)))return;
        const ReceivedFlyMotion::Identity identity{unsigned(entity->GetRefEHandle().ToInt()),unsigned(owner->GetRefEHandle().ToInt()),unsigned(source->GetRefEHandle().ToInt()),0};
        const auto velocity=ReceivedFlyMotion::Estimate(identity,sim,start,gravity);if(!velocity)return;
        if(start.DistTo(origin)>velocity->Length()*(KnockbackPolicy::Horizon+.1f)+300.f)return;
        const float multiplier=SDK::AttribHookValue(1.f,"mult_dmg",weapon);
        if(!std::isfinite(multiplier)||multiplier<=0||multiplier>4)return;
        float damage=0;
        if(bow){
            // CTFCompoundBow: speed 1800..2600 and damage 50..120 both follow charge.
            const float launch=arrow->m_vInitialVelocity().Length();
            if(!std::isfinite(launch)||launch<1700.f||launch>2700.f)return;
            damage=(50.f+70.f*std::clamp((launch-1800.f)/800.f,0.f,1.f))*multiplier;
        }else damage=75.f*multiplier; // scaled by flight age in the pass (0.5x..1x over 0.6 s)
        if(count==rockets.size()){complete=false;return;}
        Rocket r;r.direct=true;r.arrow=bow;r.bolt=bolt;r.hypothesis=true;r.entity=entity;r.handle=identity.handle;r.owner=identity.owner;r.launcher=identity.launcher;
        r.team=entity->m_iTeamNum();r.sim=sim;r.origin=start;r.velocity=*velocity;r.radius=1.f;r.damage=damage;r.classId=int(entity->GetClassID());
        r.seen=Snapshot::FirstSeen(unsigned(r.handle),sim);r.attackerCenter=attacker->GetCenter();r.attackerOrigin=attacker->m_vecOrigin();
        {const auto mode=Mode(arrow->m_bCritical(),attacker,target,0);r.crit=mode.crit;r.minicrit=mode.minicrit;}
        ReceivedProjectileFlight::Key key{r.handle,r.owner,r.launcher,0,sim,gravity,start,*velocity,arrow->m_angRotation()};key.analytic=true;key.terminalWorld=true;
        r.flightKey=key;r.flight=ReceivedProjectileFlight::Pipe(key,entity,attacker,weapon);
        if(!r.flight||r.flight->count<2){complete=false;return;}rockets[count++]=std::move(r);
    }
    Snapshot(CTFPlayer* p,float now):target(p),observed(now){
        if(!Enabled(p)||!OrdinaryTarget(p)||!KnockbackPolicy::Fresh(now-p->m_flSimulationTime()))return;
        handle=p->GetRefEHandle().ToInt();spawn=p->m_iSpawnCounter();server=I::ClientState->m_nServerCount;
        channel=I::EngineClient->GetNetChannelInfo();anchor=p->m_flSimulationTime();origin=p->m_vecOrigin();mins=p->m_vecMins();maxs=p->m_vecMaxs();
        range=Cvar("tf_damage_range",.5f);reduction=SDK::AttribHookValue(1.f,"damage_force_reduction",p);
        if(!channel||!KnockbackPolicy::Finite(P(origin))||!std::isfinite(range)||range<0||range>1
            ||!std::isfinite(reduction)||reduction<0||reduction>4||Cvar("tf_damage_disablespread",1.f)==0)return;
        int scanned=0;
        for(auto entity:H::Entities.GetGroup(EntityEnum::WorldProjectile)){
            if(++scanned>128){complete=false;break;}
            if(!entity||entity->IsDormant())continue;
            if(entity->GetClassID()==ETFClassID::CTFGrenadePipebombProjectile){CapturePipe(entity,now);continue;}
            if(entity->GetClassID()==ETFClassID::CTFProjectile_Cleaver){CaptureCleaver(entity,now);continue;}
            if(entity->GetClassID()==ETFClassID::CTFProjectile_Flare){CaptureScorch(entity,now);continue;}
            if(entity->GetClassID()==ETFClassID::CTFProjectile_Arrow||entity->GetClassID()==ETFClassID::CTFProjectile_HealingBolt){CaptureArrow(entity,now);continue;}
            // Energy rings explicitly carry PREVENT_PHYSICS_FORCE. No generic
            // velocity producer is registered for their damage/resource effects.
            const auto cls=entity->GetClassID();
            const bool energy=cls==ETFClassID::CTFProjectile_EnergyBall,sentryRocket=cls==ETFClassID::CTFProjectile_SentryRocket;
            if(cls!=ETFClassID::CTFProjectile_Rocket&&!energy&&!sentryRocket)continue;
            auto* rocket=entity->As<CTFBaseRocket>();auto* owner=rocket->m_hOwnerEntity().Get();auto* source=rocket->m_hLauncher().Get();
            CTFPlayer* attacker=nullptr;CTFWeaponBase* weapon=nullptr;
            if(sentryRocket){
                // Sentry rockets are owned by the sentry; its builder decides the team.
                if(!owner||owner->IsDormant()||owner->GetClassID()!=ETFClassID::CObjectSentrygun)continue;
                auto* builder=owner->As<CBaseObject>()->m_hBuilder().Get();
                if(!builder||!builder->IsPlayer())continue;
                attacker=builder->As<CTFPlayer>();
            }else{
                if(!owner||!owner->IsPlayer()||!source||!source->IsBaseCombatWeapon())continue;
                attacker=owner->As<CTFPlayer>();weapon=source->As<CTFWeaponBase>();
                const int id=weapon->GetWeaponID();
                if(energy?id!=TF_WEAPON_PARTICLE_CANNON:(id!=TF_WEAPON_ROCKETLAUNCHER&&id!=TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT))continue;
                if(!attacker->IsAlive()||attacker->IsDormant())continue;
            }
            const bool self=owner==p;
            const int deflected=rocket->m_iDeflected();
            const bool critical=cls==ETFClassID::CTFProjectile_Rocket&&entity->As<CTFProjectile_Rocket>()->m_bCritical();
            const bool charged=energy&&entity->As<CTFProjectile_EnergyBall>()->m_bChargedShot();
            if(!self&&entity->m_iTeamNum()==p->m_iTeamNum())continue;
            if(attacker->InCond(TF_COND_HASRUNE))continue;
            // The self rocket-jump model stays on its established ordinary path:
            // exactly the self cases 0.7.0.21 already modelled.
            if(self&&(deflected||critical||energy||sentryRocket||attacker->IsMiniCritBoosted()||p->IsMarked()
                ||(weapon&&weapon->m_iItemDefinitionIndex()==Soldier_m_TheBeggarsBazooka)))continue;
            if(weapon&&(SDK::AttribHookValue(0.f,"damage_causes_airblast",weapon)!=0
                ||SDK::AttribHookValue(1.f,"damage_blast_push",weapon)!=1
                ||SDK::AttribHookValue(1.f,"mult_dmg_vs_burning",weapon)!=1
                ||(!self&&weapon->m_iItemDefinitionIndex()==Soldier_m_RocketJumper)))continue;
            const auto start=entity->m_vecOrigin();const float sim=entity->m_flSimulationTime();
            Vec3 velocity=F::ProjSim.GetVelocity(entity);
            if(deflected){
                // Reflected rockets keep no reliable networked velocity; estimate
                // it from two received snapshots instead of the client guess.
                const ReceivedFlyMotion::Identity identity{unsigned(entity->GetRefEHandle().ToInt()),unsigned(owner->GetRefEHandle().ToInt()),
                    source?unsigned(source->GetRefEHandle().ToInt()):0u,deflected};
                const auto estimate=ReceivedFlyMotion::Estimate(identity,sim,start,0.f);if(!estimate)continue;velocity=*estimate;
            }
            if(!KnockbackPolicy::Fresh(now-sim)||!KnockbackPolicy::Finite(P(start))||!KnockbackPolicy::Finite(P(velocity))
                ||velocity.Length()<100||velocity.Length()>4000||sim>anchor+.1f||sim<anchor-.1f)continue;
            if(start.DistTo(origin)>velocity.Length()*(KnockbackPolicy::Horizon+.1f)+250)continue;
            if(count==rockets.size()){complete=false;break;}
            if(!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Geometry)){complete=false;break;}
            Filter filter;filter.pSkip=owner;filter.victim=p;filter.iType=SKIP_CHECK;filter.iPlayer=PLAYER_ALL;
            CGameTrace trace={};const float flight=KnockbackPolicy::Horizon+std::max(0.f,anchor-sim);
            SDK::Trace(start,start+velocity*flight,MASK_SHOT,&filter,&trace);
            if(trace.startsolid||trace.allsolid||(trace.surface.flags&SURF_SKY))continue;
            // Static non-world contacts can move. Do not extrapolate through
            // their current collision or fabricate their explosion timing.
            if(trace.DidHit()&&(!trace.m_pEnt||trace.m_pEnt->entindex()!=0))continue;
            Rocket r;r.entity=entity;r.handle=entity->GetRefEHandle().ToInt();r.owner=owner->GetRefEHandle().ToInt();
            r.launcher=source?source->GetRefEHandle().ToInt():0;r.team=entity->m_iTeamNum();r.sim=sim;r.origin=start;r.velocity=velocity;
            r.classId=int(cls);r.deflected=deflected;r.sentry=sentryRocket;r.charged=charged;
            if(sentryRocket){
                // Fixed 100 damage, no distance modifier (public TF2 values).
                r.attackerCenter=owner->GetCenter();r.attackerOrigin=owner->m_vecOrigin();r.stationaryAttacker=true;
                r.damage=100.f;r.selfDamage=0.f;r.selfMultiplier=1.f;r.fixedDamage=true;
                r.radius=F::AimbotProjectile.GetSplashRadius(entity,nullptr,nullptr,1.f);
            }else{
                r.attackerCenter=attacker->GetCenter();r.attackerOrigin=attacker->m_vecOrigin();r.stationaryAttacker=attacker->m_vecVelocity().Length()<=1.f;r.self=self;r.noClose=bool(weapon->GetDamageType()&DMG_NOCLOSEDISTANCEMOD);
                r.damage=weapon->GetDamage(true);r.selfDamage=weapon->GetDamage(false);r.selfMultiplier=SDK::AttribHookValue(1.f,"mult_dmgself_push_force",weapon);
                r.radius=self?146.f:F::AimbotProjectile.GetSplashRadius(entity,weapon,attacker,1.f);
            }
            {const auto mode=Mode(critical,sentryRocket?nullptr:attacker,p,deflected);r.crit=mode.crit;r.minicrit=mode.minicrit||(charged&&!mode.crit);
             // Cow Mangler and sentry rockets stay hypotheses until verified
             // live: the encounter must pass with and without the push.
             // Beggar's rockets are ordinary rockets once in flight, so they are
             // not a hypothesis (a 3-rocket volley would otherwise block shots).
             r.hypothesis=mode.hypothesis||energy||sentryRocket;}
            if(!std::isfinite(r.damage)||r.damage<=0||!std::isfinite(r.radius)||r.radius<=0||r.radius>300)continue;
            r.world=trace.DidHit();r.worldTime=sim+flight*trace.fraction;r.worldPoint=trace.endpos+trace.plane.normal;r.normal=trace.plane.normal;
            rockets[count++]=r;
        }
    }
    bool Current()const{
        return handle&&target&&target->GetRefEHandle().ToInt()==handle&&target->m_iSpawnCounter()==spawn
            &&I::ClientState->m_nServerCount==server&&I::EngineClient->GetNetChannelInfo()==channel
            &&target->m_flSimulationTime()==anchor&&target->IsAlive()&&!target->IsDormant();
    }
    bool Current(const Rocket& r)const{
        auto* entity=reinterpret_cast<CBaseEntity*>(I::ClientEntityList->GetClientEntityFromHandle(CBaseHandle(r.handle)));
        if(entity!=r.entity||!entity||entity->IsDormant())return false;
        if(r.scorch){
            if(entity->GetClassID()!=ETFClassID::CTFProjectile_Flare)return false;
            auto* p=entity->As<CTFProjectile_Flare>();auto* owner=p->m_hOwnerEntity().Get();auto* source=p->m_hLauncher().Get();
            return owner&&source&&source->IsBaseCombatWeapon()&&source->As<CTFWeaponBase>()->GetWeaponID()==TF_WEAPON_FLAREGUN
                &&owner->GetRefEHandle().ToInt()==r.owner&&source->GetRefEHandle().ToInt()==r.launcher&&p->m_iDeflected()==r.deflected
                &&!(entity->m_usSolidFlags()&FSOLID_NOT_SOLID)&&entity->m_CollisionGroup()!=COLLISION_GROUP_DEBRIS
                &&entity->m_MoveType()==MOVETYPE_FLYGRAVITY
                &&entity->m_iTeamNum()==r.team&&entity->m_flSimulationTime()==r.sim&&p->m_vecOrigin()==r.origin;
        }
        if(r.arrow||r.bolt){
            if(int(entity->GetClassID())!=r.classId)return false;
            auto* p=entity->As<CTFProjectile_Arrow>();auto* owner=p->m_hOwnerEntity().Get();auto* source=p->m_hLauncher().Get();
            return owner&&source&&owner->GetRefEHandle().ToInt()==r.owner&&source->GetRefEHandle().ToInt()==r.launcher&&!p->m_iDeflected()
                &&!(entity->m_usSolidFlags()&FSOLID_NOT_SOLID)&&entity->m_MoveType()==MOVETYPE_FLYGRAVITY
                &&entity->m_iTeamNum()==r.team&&entity->m_flSimulationTime()==r.sim&&p->m_vecOrigin()==r.origin;
        }
        if(r.direct){
            if(entity->GetClassID()!=ETFClassID::CTFProjectile_Cleaver)return false;
            auto* p=entity->As<CTFWeaponBaseGrenadeProj>();auto* owner=p->m_hThrower().Get();auto* source=p->m_hOriginalLauncher().Get();
            return owner&&source&&source->IsBaseCombatWeapon()&&source->As<CTFWeaponBase>()->GetWeaponID()==TF_WEAPON_CLEAVER
                &&source->As<CTFWeaponBase>()->m_hOwner().Get()==owner&&owner->GetRefEHandle().ToInt()==r.owner&&source->GetRefEHandle().ToInt()==r.launcher
                &&!p->m_iDeflected()&&!(entity->m_usSolidFlags()&FSOLID_NOT_SOLID)&&entity->m_iTeamNum()==r.team
                &&entity->m_flSimulationTime()==r.sim&&p->m_vecOrigin()==r.origin&&p->CBaseGrenade::m_vecVelocity()==r.velocity;
        }
        if(r.pipe){
            if(entity->GetClassID()!=ETFClassID::CTFGrenadePipebombProjectile)return false;
            auto* p=entity->As<CTFGrenadePipebombProjectile>();auto* owner=p->m_iDeflected()?p->m_hDeflectOwner().Get():p->m_hThrower().Get();auto* source=p->m_hLauncher().Get();
            return owner&&source&&owner->GetRefEHandle().ToInt()==r.owner&&source->GetRefEHandle().ToInt()==r.launcher
                &&p->m_iType()==(r.cannon?TF_GL_MODE_CANNONBALL:TF_GL_MODE_REGULAR)&&p->m_iDeflected()==r.deflected&&p->m_bTouched()==r.touched
                &&p->m_flSimulationTime()==r.sim&&p->m_iTeamNum()==r.team;
        }
        if(int(entity->GetClassID())!=r.classId)return false;
        auto* rocket=entity->As<CTFBaseRocket>();auto* owner=rocket->m_hOwnerEntity().Get();auto* source=rocket->m_hLauncher().Get();
        return owner&&owner->GetRefEHandle().ToInt()==r.owner&&(source?source->GetRefEHandle().ToInt():0)==r.launcher
            &&entity->m_iTeamNum()==r.team&&rocket->m_iDeflected()==r.deflected&&entity->m_flSimulationTime()==r.sim;
    }
};
struct Pass {
    bool cannonDonk=false,cannonEncounter=false;
    // Pass-local stand-in for TF_COND_KNOCKED_INTO_AIR: the pass never adds a
    // condition to the real entity (AddCond also writes unrestored condition data).
    bool knockedIntoAir=false,receivedKnocked=false;
    const Snapshot& snapshot;bool apply=false;size_t ticks=0;float previousTime;Vec3 previousOrigin;float stunExpires=0,uncertainAfter=std::numeric_limits<float>::infinity();
    std::array<bool,KnockbackPolicy::MaxProjectiles> consumed{};KnockbackPolicy::Evidence evidence;
    struct FlightState{std::shared_ptr<const ReceivedProjectileFlight::Path> path;float start=0;unsigned bounces=0;
        // Unresolved remainder of a pipe path: last known point and speed.
        bool bounded=false;float boundedFrom=0,boundedSpeed=0;Vec3 boundedPos;};
    // A pipe whose path ends without a native explanation (resting/rolling pill,
    // unexplained world contact) may still roll and will explode by its fuse.
    // Instead of refusing every shot, refuse only while the target could be
    // inside its blast: reach grows with the pill's last speed and gravity,
    // and ends at its fuse deadline.
    bool BoundedHazard(const Snapshot::Rocket& r,FlightState& flight,float time,const Vec3& current){
        if(!flight.bounded){
            flight.bounded=true;flight.boundedFrom=flight.start+flight.path->end;
            const auto& last=flight.path->nodes[flight.path->count-1];
            flight.boundedPos=last.position;flight.boundedSpeed=last.velocity.Length();
        }
        if(time>r.fuseDeadline)return false;
        const float dt=std::max(0.f,time-flight.boundedFrom);
        const float reach=r.radius+16.f+flight.boundedSpeed*dt+.5f*Cvar("sv_gravity",800.f)*dt*dt;
        return KnockbackPolicy::HullDistance(P(flight.boundedPos),P(current),P(snapshot.mins),P(snapshot.maxs))<=reach;
    }
    std::array<FlightState,KnockbackPolicy::MaxProjectiles> flights{};
    explicit Pass(const Snapshot& s,bool hit,bool donk=false):cannonDonk(donk),snapshot(s),apply(hit),previousTime(s.anchor),previousOrigin(s.origin){
        evidence.complete=s.complete;
        // Received condition at the anchor; ground contact during the pass ends it (native).
        receivedKnocked=knockedIntoAir=s.target&&s.target->InCond(TF_COND_KNOCKED_INTO_AIR);
    }
    static void Before(void* data,MoveStorage& storage){
        auto& pass=*static_cast<Pass*>(data);if(!pass.apply)return;
        const auto scale=ExplicitPushPolicy::InputScale(storage.m_flSimTime,pass.stunExpires);
        storage.m_MoveData.m_flForwardMove*=scale;storage.m_MoveData.m_flSideMove*=scale;
    }
    static void Tick(void* data,MoveStorage& storage){static_cast<Pass*>(data)->Step(storage);}
    void Step(MoveStorage& storage){
        const float time=storage.m_flSimTime,elapsed=time-snapshot.anchor;
        if(!snapshot.Current()){evidence.complete=false;storage.m_bFailed=true;return;}
        if(time<previousTime){cannonEncounter=false;knockedIntoAir=receivedKnocked;uncertainAfter=std::numeric_limits<float>::infinity();stunExpires=0;ticks=0;previousTime=snapshot.anchor;previousOrigin=snapshot.origin;consumed.fill(false);flights={};evidence={};evidence.complete=snapshot.complete;}
        if(time>=uncertainAfter)evidence.complete=false;
        if(time<=previousTime)return;
        if(++ticks>KnockbackPolicy::MaxTicks||elapsed>KnockbackPolicy::Horizon)return;
        auto* target=storage.m_pPlayer;auto& move=storage.m_MoveData;const Vec3 current=move.m_vecAbsOrigin;
        if(knockedIntoAir&&target->IsOnGround())knockedIntoAir=false; // native: airborne conditions end on ground contact
        struct Event{size_t index;float time;Vec3 point,target;bool direct,strong;};
        std::array<Event,KnockbackPolicy::MaxProjectiles> events{};size_t count=0;
        for(size_t i=0;i<snapshot.count;++i){
            if(consumed[i])continue;const auto& r=snapshot.rockets[i];
            if(!snapshot.Current(r)){evidence.complete=false;consumed[i]=true;continue;}
            if(time<r.sim)continue;
            auto& flight=flights[i];if(r.flight&&!flight.path){flight.path=r.flight;flight.start=r.sim;}
            if(flight.bounded){
                if(time>r.fuseDeadline){consumed[i]=true;continue;}
                if(BoundedHazard(r,flight,time,current)){evidence.complete=false;consumed[i]=true;}
                continue;
            }
            const float begin=std::max(previousTime,r.flight?flight.start:r.sim),end=std::min(time,r.flight?flight.start+flight.path->end:r.world?r.worldTime:time);
            if(end<begin){
                if(r.flight&&r.pipe&&!flight.path->terminal){if(BoundedHazard(r,flight,time,current)){evidence.complete=false;consumed[i]=true;}continue;}
                if(r.flight&&!flight.path->terminal)evidence.complete=false;consumed[i]=true;continue;
            }
            const Vec3 a=r.flight?flight.path->Position(begin-flight.start):r.origin+r.velocity*(begin-r.sim),b=r.flight?flight.path->Position(end-flight.start):r.origin+r.velocity*(end-r.sim);
            const auto delta=current-previousOrigin;
            const float span=time-previousTime;
            const Vec3 ta=previousOrigin+delta*((begin-previousTime)/span),tb=previousOrigin+delta*((end-previousTime)/span);
            bool ambiguousContact=false;
            if(r.flight)for(size_t node=1;node<flight.path->count;++node){
                const auto& after=flight.path->nodes[node];const auto& before=flight.path->nodes[node-1];
                if(after.contactEnvelope<=0||flight.start+after.time<begin||flight.start+before.time>end)continue;
                // A reflected path can hit a player even when its endpoint chord
                // misses. Bound the complete interval before choosing its order.
                const float travel=(tb-ta).Length()+after.contactEnvelope;
                if(KnockbackPolicy::HullDistance(P(before.position),P(ta),P(snapshot.mins),P(snapshot.maxs))<=travel){ambiguousContact=true;break;}
            }
            if(ambiguousContact){evidence.complete=false;consumed[i]=true;continue;}
            const float entry=r.self?-1.f:KnockbackPolicy::Entry(P(a-ta),P(b-tb),P(snapshot.mins),P(snapshot.maxs));
            if(entry>=0){
                const float contact=begin+(end-begin)*entry;const auto pos=a+(b-a)*entry,victim=ta+(tb-ta)*entry;
                if(r.pipe&&(r.touched||flight.path->State(contact-flight.start).touched)){
                    // Player-ray Touch returns on an already touched pipe; native
                    // VPhysicsUpdate then bounces the source, not the player.
                    if(PipeContactPolicy::Inside(P(a-ta),P(snapshot.mins),P(snapshot.maxs)))continue;
                    if(++flight.bounces>2){evidence.complete=false;consumed[i]=true;continue;}
                    auto state=flight.path->State(contact-flight.start);auto key=r.flightKey;
                    key.touched=true;key.sim=contact;key.origin=pos;key.angles=state.angles;key.spin=state.spin*-.5f;key.useSpin=true;
                    key.velocity=V(PipeContactPolicy::PlayerBounce(P(state.velocity),PipeContactPolicy::EntryNormal(P(a-ta),P(b-tb),P(snapshot.mins),P(snapshot.maxs),entry),true));
                    auto* source=I::ClientEntityList->GetClientEntityFromHandle(CBaseHandle(r.launcher));
                    auto* owner=I::ClientEntityList->GetClientEntityFromHandle(CBaseHandle(r.owner));
                    if(!source||!owner){evidence.complete=false;consumed[i]=true;continue;}
                    auto next=ReceivedProjectileFlight::Pipe(key,r.entity,reinterpret_cast<CBaseEntity*>(owner)->As<CTFPlayer>(),reinterpret_cast<CBaseEntity*>(source)->As<CTFWeaponBase>());
                    if(!next||next->count<2){evidence.complete=false;consumed[i]=true;continue;}
                    flight.path=std::move(next);flight.start=contact;continue;
                }
                const float pad=6.f+600.f*elapsed*elapsed;
                const bool core=KnockbackPolicy::Entry(P(a-ta),P(b-tb),P(snapshot.mins),P(snapshot.maxs),pad)>=0;
                events[count++]={i,contact,pos,victim,true,!r.hypothesis&&!r.pipe&&!r.direct&&core&&r.stationaryAttacker&&elapsed<=.15f&&snapshot.observed-r.sim<=.05f};consumed[i]=true;
            }else if(r.flight&&time>=flight.start+flight.path->end){
                if(r.pipe&&!flight.path->terminal){if(BoundedHazard(r,flight,time,current)){evidence.complete=false;consumed[i]=true;}}
                else{if(!flight.path->terminal)evidence.complete=false;consumed[i]=true;}
            }else if(r.world&&r.worldTime<=time){
                consumed[i]=true;const float f=std::clamp((r.worldTime-previousTime)/span,0.f,1.f);const auto victim=previousOrigin+delta*f;
                if(KnockbackPolicy::HullDistance(P(r.worldPoint),P(victim),P(snapshot.mins),P(snapshot.maxs))>r.radius)continue;
                const auto center=victim+(snapshot.mins+snapshot.maxs)*.5f;
                const float distance=std::min(r.worldPoint.DistTo(victim),r.worldPoint.DistTo(center));
                const bool strong=!r.hypothesis&&(r.self||r.stationaryAttacker)&&elapsed<=.15f&&distance+8.f+600.f*elapsed*elapsed<r.radius*.65f&&snapshot.observed-r.sim<=.05f;
                events[count++]={i,r.worldTime,r.worldPoint,victim,false,strong};
            }
        }
        if(count>1){evidence.complete=false;previousTime=time;previousOrigin=current;return;} // unresolved within-tick order
        std::sort(events.begin(),events.begin()+count,[](const auto&a,const auto&b){return a.time<b.time;});
        for(size_t n=0;n<count;++n){const auto& event=events[n];const auto& r=snapshot.rockets[event.index];
            const auto center=event.target+(snapshot.mins+snapshot.maxs)*.5f;
            if(!event.direct){
                if(!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Geometry)){evidence.complete=false;break;}
                CTraceFilterWorldAndPropsOnly filter;filter.pSkip=r.entity;CGameTrace line={};SDK::Trace(event.point,center,MASK_SHOT,&filter,&line);
                if(line.startsolid||line.allsolid||line.fraction<1.f)continue;
            }
            float damage=r.bolt?r.damage*std::clamp(.5f+.5f*(event.time-r.seen)/.6f,.5f,1.f) // age >= time since first seen
                :r.direct?r.damage:KnockbackPolicy::BlastDamage(r.self?r.selfDamage:r.damage,event.direct?0.f:std::min(event.point.DistTo(event.target),event.point.DistTo(center)),r.radius);
            const bool ground=target->IsOnGround();Vec3 hull=snapshot.maxs-snapshot.mins;
            if(r.cannon){
                const float distanceSquared=(event.target-r.attackerOrigin).LengthSqr();
                // Crits ignore distance falloff; the strongest crit type wins
                // natively, so donk never stacks with a crit or mini-crit.
                damage=r.crit?50.f:50.f-25.f*std::clamp((distanceSquared-512.f*512.f)/(1024.f*1024.f-512.f*512.f),0.f,1.f);
                if(cannonDonk&&!r.crit&&!r.minicrit)damage+=(1.35f-1.f)*damage;
            }
            float scale=Cvar("tf_damageforcescale_other",6.f),mult=snapshot.reduction;
            if(r.self){
                if(target->m_iClass()!=TF_CLASS_SOLDIER){evidence.complete=false;continue;}
                if(hull.x==48.f&&hull.y==48.f&&hull.z==62.f)hull.z=55.f;
                if(!ground)damage*=Cvar("tf_damagescale_self_soldier",.6f);
                scale=ground?Cvar("tf_damageforcescale_self_soldier_badrj",5.f):Cvar("tf_damageforcescale_self_soldier_rj",10.f);mult=r.selfMultiplier;
            }else{
                // Airborne Direct Hit minicrit eligibility depends on unexposed
                // enemy-blast state. Do not invent its damage/force result.
                if(!r.sentry){
                    auto* source=reinterpret_cast<CBaseEntity*>(I::ClientEntityList->GetClientEntityFromHandle(CBaseHandle(r.launcher)));
                    if(!source){evidence.complete=false;continue;}auto* weapon=source->As<CTFWeaponBase>();
                    if(!r.pipe&&!r.direct&&weapon->GetWeaponID()==TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT&&!ground){evidence.complete=false;continue;}
                }
                // Native order: radial falloff above, then the distance modifier
                // (crits skip it; mini-crits keep ramp-up but lose falloff),
                // then the crit/mini-crit multiplier.
                if(!r.pipe&&!r.direct&&!r.fixedDamage&&!r.crit)
                    damage=KnockbackPolicy::RocketDistanceDamage(damage,r.attackerCenter.DistTo(center),snapshot.range,r.noClose,r.minicrit);
                damage*=ModeScale(r.crit,r.minicrit);
                if(target->m_iClass()==TF_CLASS_HEAVY)mult*=.5f;
            }
            const float scoped=target->m_iClass()==TF_CLASS_SNIPER&&target->InCond(TF_COND_AIMING)?SDK::AttribHookValue(1.f,"mult_aiming_knockback_resistance",target):1.f;
            if(!std::isfinite(scoped)||scoped<0||scoped>4){evidence.complete=false;continue;}mult*=scoped;
            Vec3 pushOrigin=r.direct?r.inflictorCenter:event.point;
            if(r.arrow||r.bolt){
                // CTFProjectile_Arrow::AdjustDamageDirection: push along
                // (victim - arrow + arrow velocity). Force() subtracts 10 Z itself.
                const auto& flight=flights[event.index];
                pushOrigin=event.point-flight.path->State(event.time-flight.start).velocity+Vec3(0,0,10);
            }
            auto impulse=V(KnockbackPolicy::Force(P(pushOrigin),P(center),P(hull),damage,scale,mult));
            if(r.charged)impulse*=1.25f; // TF_DMG_CUSTOM_PLASMA_CHARGED
            if(r.arrow)impulse.z=0.f;   // TF_WEAPON_COMPOUND_BOW push is horizontal
            if(r.scorch)impulse={}; // native PREVENT_PHYSICS_FORCE
            if(r.cannon||r.scorch){
                const float vulnerability=SDK::AttribHookValue(1.f,"airblast_vulnerability_multiplier",target);
                const float vertical=SDK::AttribHookValue(1.f,"airblast_vertical_vulnerability_multiplier",target);
                if(!std::isfinite(vulnerability)||vulnerability<0||vulnerability>4||!std::isfinite(vertical)||vertical<0||vertical>4){evidence.complete=false;continue;}
                const auto push=r.scorch?ExplicitPushPolicy::Scorch(P(flights[event.index].path->State(event.time-flights[event.index].start).velocity),r.burning)
                    :ExplicitPushPolicy::Cannon(P(r.attackerCenter),P(center));
                impulse+=V(ExplicitPushPolicy::Generic(push,ground,vulnerability,vertical))*scoped;
            }
            if(!KnockbackPolicy::Finite(P(impulse))||impulse.Length()>2000){evidence.complete=false;continue;}
            // Impact is quantized to the next native movement boundary, at most
            // one tick after this hypothesis contact. Native pipe player-ray
            // lookahead timing is additionally uncertain; no native-time parity
            // or interpolated aim-point claim is made.
            evidence.Add(event.time-snapshot.anchor,event.strong,(r.self||r.pipe||r.direct)?0.f:r.damage*.5f*(1.f-snapshot.range));
            if(r.cannon)cannonEncounter=true;
            if(r.scorch){
                // Post-hit source motion contains RandomVector(-2,2). A padded
                // sweep bounds that velocity uncertainty; its first possible
                // world encounter ends validity before an unmodeled explosion.
                auto key=r.flightKey;key.sim=event.time;key.origin=event.point;
                key.velocity=flights[event.index].path->State(event.time-flights[event.index].start).velocity;
                key.velocity.x*=.07f;key.velocity.y*=.07f;key.velocity.z=100.f;key.velocityUncertainty=2.f;
                auto* owner=I::ClientEntityList->GetClientEntityFromHandle(CBaseHandle(r.owner));
                auto* source=I::ClientEntityList->GetClientEntityFromHandle(CBaseHandle(r.launcher));
                if(!owner||!source){evidence.complete=false;continue;}
                auto after=ReceivedProjectileFlight::Pipe(key,r.entity,reinterpret_cast<CBaseEntity*>(owner)->As<CTFPlayer>(),reinterpret_cast<CBaseEntity*>(source)->As<CTFWeaponBase>());
                if(!after||!after->complete||after->worldContact)uncertainAfter=std::min(uncertainAfter,event.time+(after?after->end:0.f));
                if(time>=uncertainAfter){evidence.complete=false;continue;}
            }
            if(apply){
                if(r.cannon||r.scorch){
                    if(!knockedIntoAir)stunExpires=time+.5f;
                    target->m_fFlags()&=~FL_ONGROUND;knockedIntoAir=true;
                }
                move.m_vecVelocity+=impulse;target->SetAbsVelocity(move.m_vecVelocity);target->m_vecVelocity()=move.m_vecVelocity;
                storage.m_CounterStrafe={};storage.m_flAverageYaw=0;storage.m_bLedgeImpulse=true;storage.m_bLedgeBraking=false;
                storage.m_bObservedWish=false;if(!r.cannon&&!r.scorch)move.m_flForwardMove=move.m_flSideMove=0;
                // Native ground categorization, gravity, walls and landing run
                // next. ApplyAbsVelocityImpulse itself does not clear ground.
            }
        }
        previousTime=time;previousOrigin=current;
    }
};
}
