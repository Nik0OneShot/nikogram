#pragma once
#include "AmmoConservationPolicy.h"
#include "AmmoLifetimeDiagnostics.h"
#include <chrono>

namespace AmmoConservation
{
    // No projectile pointers or damage promises survive a command. Only the
    // bounded action window persists; request acceptance is never assumed.
    inline AmmoConservationPolicy::Window window;
    inline int ownerHandle=0, weaponHandle=0;
    class PillPathFilter : public CTraceFilterCollideable
    {
    public:
        IHandleEntity* target=nullptr;
        bool ShouldHitEntity(IHandleEntity* entity,int mask) override
        { return entity!=target && CTraceFilterCollideable::ShouldHitEntity(entity,mask); }
    };
    inline bool Finite(const Vec3& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
    inline bool Uncertain(CTFPlayer* p)
    {
        return p->IsInvulnerable() || p->m_nNumHealers()>0 || p->InCond(TF_COND_STEALTHED) || p->m_bFeignDeathReady()
            || p->InCond(TF_COND_STEALTHED_USER_BUFF_FADING)
            || p->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST) || p->InCond(TF_COND_MEDIGUN_SMALL_BLAST_RESIST)
            || p->InCond(TF_COND_DEFENSEBUFF) || p->InCond(TF_COND_DEFENSEBUFF_NO_CRIT_BLOCK)
            || p->InCond(TF_COND_DEFENSEBUFF_HIGH) || p->InCond(TF_COND_RUNE_RESIST)
            || SDK::AttribHookValue(1.f,"mult_dmgtaken",p)!=1.f
            || SDK::AttribHookValue(1.f,"mult_dmgtaken_from_explosions",p)!=1.f;
    }
    struct Context
    {
        CTFPlayer* local; CTFWeaponBase* weapon; CUserCmd* cmd;
        bool enabled=false, permitted=false, windowChecked=false, stickyRequest=false, flightsChecked=false, impactsComplete=false;
        struct Impact { CBaseEntity* projectile; Vec3 origin; float damage, radius, delay; bool flare; };
        std::vector<Impact> impacts; // Command-local only; never retains entity pointers between commands.
        std::vector<CTFGrenadePipebombProjectile*> pills;
        int stickyContributions=0, flightContributions=0, pillContributions=0;
        int candidateHealth=0;
        float coverageMargin=10.f;
        struct Decision {int target=-1,health=0,stickies=0,flights=0,pills=0;float estimate=0,margin=10.f;} deferred;
        float horizon=0, baseHorizon=0, spentMs=0, estimate=0;
        const std::chrono::steady_clock::time_point created=std::chrono::steady_clock::now();
        int traces=0;
        Context(CTFPlayer* p, CTFWeaponBase* w, CUserCmd* c):local(p),weapon(w),cmd(c)
        {
            if (!Vars::Aimbot::Projectile::AmmoConservation.Value) return;
            const auto& request=AmmoLifetimeDiagnostics::commandRequests.current;
            stickyRequest=request.requested && (request.sources&1) && request.originalCommand==cmd->command_number && (cmd->buttons&IN_ATTACK2);
            const int id=weapon->GetWeaponID();
            const bool rocket=id==TF_WEAPON_ROCKETLAUNCHER || id==TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT;
            const bool scorch=id==TF_WEAPON_FLAREGUN && weapon->As<CTFFlareGun>()->GetFlareGunType()==FLAREGUN_SCORCHSHOT;
            const bool stickyWeapon=id==TF_WEAPON_PIPEBOMBLAUNCHER;
            const char* reason=nullptr;
            if (!Vars::Aimbot::General::AutoShoot.Value) reason="autoshoot_off";
            else if (G::OriginalCmd.buttons&(IN_ATTACK|IN_ATTACK2|IN_USE)) reason="manual_input";
            else if (!local->IsAlive() || local->IsAGhost()) reason="local_inactive";
            else if (id!=TF_WEAPON_GRENADELAUNCHER && !stickyWeapon && !rocket && !scorch) reason="unsupported_weapon";
            else if (stickyWeapon && weapon->As<CTFPipebombLauncher>()->GetDetonateType()==TF_DETONATE_MODE_DOT) reason="selective_launcher_unsupported";
            else if (stickyWeapon && !AmmoConservationPolicy::chargeOwnership.Owned(weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime())) reason="manual_or_unknown_charge";
            else if (weapon->m_iItemDefinitionIndex()==Soldier_m_TheBeggarsBazooka || weapon->m_iItemDefinitionIndex()==Soldier_m_RocketJumper) reason="special_firing_weapon";
            else if (!G::CanPrimaryAttack || G::Throwing || cmd->weaponselect) reason="weapon_not_ready";
            else if (I::ClientState->chokedcommands) reason="choked_command";
            else if (!I::EngineClient->GetNetChannelInfo()) reason="no_network";
            if (!reason)
            {
                // Full measured latency (including artificial latency), interpolation,
                // and two ticks of scheduling margin. High delays disable the action.
                horizon=F::Backtrack.GetReal(MAX_FLOWS,false)+std::max(G::Lerp,F::Backtrack.GetFakeInterp())+2*I::GlobalVars->interval_per_tick;
                if (!std::isfinite(horizon) || horizon<=0 || horizon>.15f) reason="delay_uncertain";
                else if (stickyWeapon && !StickySafe(horizon)) reason="sticky_charge_limit";
                else if (!Finite(local->GetAbsOrigin())) reason="invalid_local";
                else
                {
                    int count=0;
                    for (auto entity:H::Entities.GetGroup(EntityEnum::PlayerAll))
                    {
                        if (++count>64) {reason="player_budget";break;}
                        auto enemy=entity->As<CTFPlayer>();
                        if (enemy==local || enemy->m_iTeamNum()==local->m_iTeamNum() || !enemy->IsAlive() || enemy->IsDormant() || enemy->IsAGhost()) continue;
                        if (!Finite(enemy->GetAbsOrigin()) || enemy->GetAbsOrigin().DistTo(local->GetAbsOrigin())<200.f)
                        {reason="nearby_threat";break;}
                    }
                }
            }
            if (reason)
            {
                static unsigned long long last=0; const auto now=GetTickCount64();
                if (now-last>=500) {last=now; Log(reason,-1,-1);}
                return;
            }
            const int owner=local->As<IHandleEntity>()->GetRefEHandle().ToInt();
            const int held=weapon->As<IHandleEntity>()->GetRefEHandle().ToInt();
            if (owner!=ownerHandle || held!=weaponHandle) {window={};ownerHandle=owner;weaponHandle=held;}
            baseHorizon=horizon; enabled=true;
        }
        void Log(const char* reason,int target,int replacement,bool suppression=false)
        {
            if(!SelfDamageDiagnostics::Enabled()) return;
            const bool original=target>=0 && target==deferred.target;
            SelfDamageDiagnostics::Write("ammo_live",std::format(
                "cmd={} weapon={} item={} reason={} target={} replacement={} health={} estimate={} margin={} horizon={} traces={} budget_ms={} sticky_sources={} impact_sources={} pill_sources={} own_fresh_pills={} suppression={} alternative_validated={} attack={} secondary={} charge_begin={} request_confirmed=0 fuse_verified=0",
                cmd->command_number,weapon->GetWeaponID(),weapon->m_iItemDefinitionIndex(),reason,target,replacement,original?deferred.health:candidateHealth,original?deferred.estimate:estimate,original?deferred.margin:coverageMargin,horizon,traces,spentMs,original?deferred.stickies:stickyContributions,original?deferred.flights:flightContributions,original?deferred.pills:pillContributions,pills.size(),suppression,replacement>=0,bool(cmd->buttons&IN_ATTACK),bool(cmd->buttons&IN_ATTACK2),weapon->GetWeaponID()==TF_WEAPON_PIPEBOMBLAUNCHER?weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime():0.f));
        }
        void Remember(int target)
        {deferred={target,candidateHealth,stickyContributions,flightContributions,pillContributions,estimate,coverageMargin};}
        bool Allow()
        {
            if (!Fresh()) return false;
            if (!windowChecked) {windowChecked=true;permitted=window.Allow(GetTickCount64());}
            else permitted=permitted && GetTickCount64()-window.start<100;
            return permitted;
        }
        bool Fresh()
        {
            const float age=std::chrono::duration<float>(std::chrono::steady_clock::now()-created).count();
            horizon=baseHorizon+age;
            return enabled && age<.025f && horizon<=.15f && (!stickyRequest || (cmd->buttons&IN_ATTACK2))
                && (weapon->GetWeaponID()!=TF_WEAPON_PIPEBOMBLAUNCHER || StickySafe(horizon))
                && !(G::OriginalCmd.buttons&(IN_ATTACK|IN_ATTACK2|IN_USE));
        }
        bool StickySafe(float delay)
        {
            const float begin=weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime();
            return AmmoConservationPolicy::chargeOwnership.Owned(begin)
                && AmmoConservationPolicy::StickyHoldSafe(begin,TICKS_TO_TIME(local->m_nTickBase()),
                    SDK::AttribHookValue(4.f,"stickybomb_charge_rate",weapon),delay);
        }
        bool Wait()
        {
            if (!Allow()) return false;
            const bool stickyWeapon=weapon->GetWeaponID()==TF_WEAPON_PIPEBOMBLAUNCHER;
            const bool charging=stickyWeapon && weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime()>0;
            if (charging) cmd->buttons|=IN_ATTACK; // Do NOT release an existing charge to save ammo.
            else cmd->buttons&=~IN_ATTACK; // Prevent starting an unnecessary new charge/shot.
            Log(charging?"brief_hold_sticky_charge":"brief_wait",deferred.target,-1,true);
            return true;
        }
        bool CollectImpacts()
        {
            if (flightsChecked) return impactsComplete;
            flightsChecked=true;
            const auto start=std::chrono::steady_clock::now();
            auto elapsed=[&] {return std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-start).count();};
            bool complete=true; int count=0;
            for (auto projectile:H::Entities.GetGroup(EntityEnum::WorldProjectile))
            {
                if (++count>64 || spentMs+elapsed()>=2.f) {complete=false;break;}
                const auto cls=projectile->GetClassID();
                if (weapon->GetWeaponID()==TF_WEAPON_GRENADELAUNCHER || weapon->GetWeaponID()==TF_WEAPON_PIPEBOMBLAUNCHER)
                {
                    if (cls!=ETFClassID::CTFGrenadePipebombProjectile || projectile->IsDormant()) continue;
                    auto pill=projectile->As<CTFGrenadePipebombProjectile>();
                    if (pill->m_hThrower().Get()!=local || pill->m_iDeflected() || pill->m_iType()!=TF_GL_MODE_REGULAR || pill->m_bTouched()) continue;
                    auto source=pill->m_hOriginalLauncher().Get();
                    auto launcher=source?source->As<CTFWeaponBase>():nullptr;
                    if (!launcher || launcher->m_hOwnerEntity().Get()!=local || launcher->GetWeaponID()!=TF_WEAPON_GRENADELAUNCHER) continue;
                    const float fuse=SDK::AttribHookValue(2.f,"fuse_mult",launcher);
                    if (!AmmoConservationPolicy::FreshPill(I::GlobalVars->curtime-pill->m_flCreationTime(),fuse,pill->m_flCreationTime(),launcher->m_flLastFireTime())) continue;
                    pills.push_back(pill);
                    continue;
                }
                const bool flare=cls==ETFClassID::CTFProjectile_Flare;
                if (cls!=ETFClassID::CTFProjectile_Rocket && !flare) continue;
                auto rocket=projectile->As<CTFBaseRocket>();
                if (projectile->IsDormant() || rocket->m_iDeflected() || projectile->m_hOwnerEntity().Get()!=local) continue;
                auto source=rocket->m_hOriginalLauncher().Get();
                auto launcher=source?source->As<CTFWeaponBase>():nullptr;
                if (!launcher || rocket->m_hLauncher().Get()!=source || launcher->m_hOwnerEntity().Get()!=local) continue;
                const int id=launcher->GetWeaponID();
                if (flare ? (id!=TF_WEAPON_FLAREGUN || launcher->As<CTFFlareGun>()->GetFlareGunType()!=FLAREGUN_SCORCHSHOT)
                    : (id!=TF_WEAPON_ROCKETLAUNCHER && id!=TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT)) continue;
                if (launcher->m_iItemDefinitionIndex()==Soldier_m_RocketJumper) continue;
                const float age=I::GlobalVars->curtime-projectile->m_flSimulationTime();
                if (!AmmoConservationPolicy::ImpactWindow(horizon,0,age)) continue;
                Vec3 origin=projectile->m_vecOrigin(), velocity=F::ProjSim.GetVelocity(projectile);
                const float gravity=F::ProjSim.GetGravity(projectile,launcher);
                if (!Finite(origin) || !Finite(velocity) || velocity.Length()<100.f || velocity.Length()>4000.f
                    || !std::isfinite(gravity) || gravity<0 || gravity>2000.f) continue;
                const float radius=F::AimbotProjectile.GetSplashRadius(projectile,launcher,local);
                // Rocket distance falloff can reduce damage to half nominal. Never
                // credit crits, minicrits, afterburn, or Scorch Shot's second explosion.
                const float nominal=launcher->GetDamage(true)*(flare?1.f:.5f);
                if (!std::isfinite(radius) || radius<=0 || !std::isfinite(nominal) || nominal<=0) continue;
                CTraceFilterCollideable filter; filter.pSkip=local; filter.iType=SKIP_CHECK;
                for (int step=0;step<10;++step)
                {
                    if (traces>=48 || spentMs+elapsed()>=2.f) {complete=false;break;}
                    constexpr float dt=.01f;
                    Vec3 next=origin+velocity*dt; next.z-=gravity*dt*dt*.5f;
                    CGameTrace hit={}; ++traces;
                    SDK::Trace(origin,next,MASK_SHOT,&filter,&hit);
                    if (hit.startsolid || hit.allsolid) break;
                    if (hit.DidHit())
                    {
                        // Only fixed world impacts qualify. Player hits, moving props,
                        // buildings and Scorch bounces are too uncertain to promise.
                        const float flight=(step+hit.fraction)*dt;
                        if (hit.m_pEnt && hit.m_pEnt->entindex()==0 && Finite(hit.endpos) && Finite(hit.plane.normal)
                            && AmmoConservationPolicy::ImpactWindow(horizon,flight,age))
                            impacts.push_back({projectile,hit.endpos+hit.plane.normal*.5f,nominal,radius,flight+age,flare});
                        break;
                    }
                    origin=next; velocity.z-=gravity*dt;
                }
                if (!complete) break;
            }
            spentMs+=elapsed();
            return impactsComplete=complete && spentMs<2.f;
        }
        bool Covered(CBaseEntity* entity)
        {
            estimate=0; stickyContributions=flightContributions=pillContributions=0; candidateHealth=0;coverageMargin=10.f;
            if (!Fresh() || !entity || !entity->IsPlayer()) return false;
            auto target=entity->As<CTFPlayer>();
            candidateHealth=target->m_iHealth();
            if (!target->IsAlive() || target->IsDormant() || target->IsAGhost() || target->m_iTeamNum()==local->m_iTeamNum()
                || Uncertain(target) || !target->IsOnGround() || H::Entities.GetChoke(target->entindex())>0) return false;
            const float observationAge=I::GlobalVars->curtime-target->m_flSimulationTime();
            if (!std::isfinite(observationAge) || std::abs(observationAge)>.25f) return false;
            const auto velocity=target->m_vecVelocity(), center=target->GetCenter();
            if (!Finite(velocity) || !Finite(center) || velocity.Length()>100.f
                || !std::isfinite(target->m_flMaxspeed()) || target->m_flMaxspeed()>600.f) return false;
            if (!CollectImpacts()) {Log("incomplete_impact_scan",target->entindex(),-1);return false;}
            // Reserve movement in any direction at 600 units/s, plus a geometry
            // margin. This is deliberately stricter than constant-velocity lead.
            const float travel=600.f*horizon+8.f;
            const auto start=std::chrono::steady_clock::now();
            auto elapsed=[&] {return std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-start).count();};
            bool complete=true; int count=0;
            if (stickyRequest) for (auto entity:H::Entities.GetGroup(EntityEnum::LocalStickies))
            {
                if (++count>32 || spentMs+elapsed()>=2.f) {complete=false;break;}
                auto sticky=entity->As<CTFGrenadePipebombProjectile>();
                if (sticky->m_hThrower().Get()!=local || sticky->m_iDeflected() || sticky->m_iType()!=TF_GL_MODE_REMOTE_DETONATE) continue;
                auto source=sticky->m_hOriginalLauncher().Get();
                auto launcher=source?source->As<CTFWeaponBase>():nullptr;
                // Normal detonation only. Zero-damage bombs contribute nothing.
                if (!launcher || launcher->m_hOwnerEntity().Get()!=local || launcher->GetWeaponID()!=TF_WEAPON_PIPEBOMBLAUNCHER
                    || launcher->As<CTFPipebombLauncher>()->GetDetonateType()==TF_DETONATE_MODE_DOT) continue;
                const float age=I::GlobalVars->curtime-sticky->m_flCreationTime();
                const float arm=SDK::AttribHookValue(.8f,"sticky_arm_time",launcher);
                const auto origin=sticky->GetAbsOrigin(), speed=sticky->GetAbsVelocity();
                if (!std::isfinite(age) || !std::isfinite(arm) || sticky->m_flCreationTime()<=0 || age<std::max(5.f,arm)
                    || !sticky->m_bTouched() || !Finite(speed) || speed.Length()>=1.f || !Finite(origin)) continue;
                const float radius=SDK::AttribHookValue(TF_ROCKET_RADIUS,"mult_explosion_radius",launcher);
                const float damage=AmmoEvidencePolicy::SplashScreen(sticky->m_flDamage(),origin.DistTo(center)+travel,radius);
                if (damage<=0) continue;
                if (traces>=28) {complete=false;break;}
                ++traces;
                if (!SDK::VisPosCollideable(sticky,target,origin,center,MASK_SHOT)) continue;
                bool visible=true;
                // Check exposure at center and in each direction of the uncertainty
                // envelope. Any obstructed or unaudited probe fails open to shooting.
                const Vec3 offsets[]={{},{travel,0,0},{-travel,0,0},{0,travel,0},{0,-travel,0},{0,0,travel},{0,0,-travel}};
                for (const auto& offset:offsets)
                {
                    if (traces>=28 || spentMs+elapsed()>=2.f) {complete=false;visible=false;break;}
                    ++traces;
                    // Ignore players for envelope probes so a hit on the current
                    // target cannot conceal a wall behind its future position.
                    if (!SDK::VisPosWorld(sticky,nullptr,origin,center+offset,MASK_SHOT)) {visible=false;break;}
                }
                if (!complete) break;
                if (visible) {estimate+=damage;++stickyContributions;}
            }
            if (complete) for (const auto& impact:impacts)
            {
                // Recheck elapsed time after each target's normal solver pass.
                if (horizon+impact.delay>.15f) continue;
                if (impact.flare && (target->InCond(TF_COND_FIRE_IMMUNE) || target->InCond(TF_COND_MEDIGUN_UBER_FIRE_RESIST)
                    || target->InCond(TF_COND_MEDIGUN_SMALL_FIRE_RESIST) || SDK::AttribHookValue(1.f,"mult_dmgtaken_from_fire",target)!=1.f)) continue;
                const float envelope=600.f*(horizon+impact.delay)+8.f;
                const float damage=AmmoEvidencePolicy::SplashScreen(impact.damage,impact.origin.DistTo(center)+envelope,impact.radius);
                if (damage<=0) continue;
                const Vec3 offsets[]={{},{envelope,0,0},{-envelope,0,0},{0,envelope,0},{0,-envelope,0},{0,0,envelope},{0,0,-envelope}};
                bool visible=true;
                for (const auto& offset:offsets)
                {
                    if (traces>=48 || spentMs+elapsed()>=2.f) {complete=false;visible=false;break;}
                    ++traces;
                    if (!SDK::VisPosWorld(impact.projectile,nullptr,impact.origin,center+offset,MASK_SHOT)) {visible=false;break;}
                }
                if (!complete) break;
                if (visible) {estimate+=damage;++flightContributions;}
            }
            if (complete) for (auto pill:pills)
            {
                const float age=I::GlobalVars->curtime-pill->m_flSimulationTime();
                if (!AmmoConservationPolicy::ImpactWindow(horizon,0,age) || pill->m_bTouched() || pill->m_iDeflected()) continue;
                Vec3 origin=pill->m_vecOrigin(), speed=pill->GetAbsVelocity();
                if (!Finite(origin) || !Finite(speed) || speed.Length()<100.f || speed.Length()>4000.f) continue;
                const float gravity=F::ProjSim.GetGravity(pill);
                if (!std::isfinite(gravity) || gravity<0 || gravity>2000.f) continue;
                const auto mins=target->m_vecMins(),maxs=target->m_vecMaxs(),targetOrigin=target->GetAbsOrigin();
                if (!Finite(mins) || !Finite(maxs) || !Finite(targetOrigin)) continue;
                const float nominal=pill->m_flDamage(); // Already-discounted network damage; never restore direct-hit/crit bonuses.
                if (!std::isfinite(nominal) || nominal<=0) continue;
                PillPathFilter filter;filter.pSkip=local;filter.iType=SKIP_CHECK;filter.target=target->As<IHandleEntity>();
                const auto array=[](const Vec3& v){return std::array<float,3>{v.x,v.y,v.z};};
                bool touchSeen=false;float firstContact=0;
                for (int step=0;step<10;++step)
                {
                    if (traces+2>48 || spentMs+elapsed()>=2.f) {complete=false;break;}
                    constexpr float dt=.01f;
                    Vec3 next=origin+speed*dt;next.z-=gravity*dt*dt*.5f;
                    CGameTrace world={},direct={};
                    const Vec3 hull={5,5,5};
                    ++traces;SDK::TraceHull(origin,next,-hull,hull,MASK_SHOT,&filter,&world);
                    Ray_t ray;ray.Init(origin,next,-hull,hull);
                    ++traces;I::EngineTrace->ClipRayToEntity(ray,MASK_SHOT,target,&direct);
                    if (world.startsolid || world.allsolid || ((direct.startsolid || direct.allsolid) && !touchSeen)) break;
                    if (direct.DidHit() && direct.m_pEnt==target && std::isfinite(direct.fraction)
                        && (!world.DidHit() || world.m_pEnt==target || direct.fraction<world.fraction-.0001f))
                    {
                        if (!touchSeen) {touchSeen=true;firstContact=(step+direct.fraction)*dt;}
                    }
                    if (touchSeen)
                    {
                        const float total=horizon+age+(step+1)*dt;
                        if (!AmmoConservationPolicy::ImpactWindow(horizon,(step+1)*dt,age)) break;
                        // Short-horizon constant-velocity lead, with an acceleration
                        // allowance and an inset to reject marginal/grazing shots.
                        const Vec3 predicted=targetOrigin+velocity*total;
                        const float inset=4.f+1000.f*total*total;
                        const float core=AmmoConservationPolicy::CoreFraction(array(origin),array(next),array(predicted+mins),array(predicted+maxs),inset);
                        if (core>=0 && (!world.DidHit() || world.m_pEnt==target || core<world.fraction-.0001f))
                        {
                            estimate+=nominal*.5f;++pillContributions;
                            if(SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("ammo_pill_contact",std::format("cmd={} target={} projectile={} flight={} delay={} inset={} estimate={} touched=0 fuse_verified=0 kill_confirmed=0",cmd->command_number,target->entindex(),pill->As<IHandleEntity>()->GetRefEHandle().ToInt(),firstContact,total,inset,nominal*.5f));
                            break; // Credit a projectile once, never accumulate trajectory steps.
                        }
                    }
                    if (world.DidHit()) break; // No invented bounce or splash-on-world contact.
                    origin=next;speed.z-=gravity*dt;
                }
                if (!complete) break;
            }
            spentMs+=elapsed();
            if (!complete || spentMs>=2.f) {Log("incomplete_exposure",target->entindex(),-1);return false;}
            // Preserve the old sticky margin. Low-damage flares otherwise could
            // never qualify even at 1 HP; their already-halved estimate still
            // requires a 25% health margin plus two damage points.
            coverageMargin=(stickyContributions || pillContributions)?10.f:2.f;
            const bool covered=AmmoConservationPolicy::Covered(estimate,target->m_iHealth(),coverageMargin);
            if (covered) Log("covered_candidate",target->entindex(),-1);
            else if (flightContributions || !pills.empty())
            {
                static unsigned long long last=0; const auto now=GetTickCount64();
                if (now-last>=500) {last=now;Log(pillContributions || flightContributions?"impact_damage_insufficient":"pill_contact_unconfirmed",target->entindex(),-1);}
            }
            return covered;
        }
    };
}
