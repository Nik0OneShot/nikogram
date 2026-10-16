#pragma once
#include "AmmoConservationPolicy.h"
#include <chrono>

namespace AmmoConservation
{
    class DirectPathFilter : public CTraceFilterCollideable
    {
    public:
        IHandleEntity* target=nullptr;
        bool ShouldHitEntity(IHandleEntity* entity,int mask) override
        { return entity!=target && CTraceFilterCollideable::ShouldHitEntity(entity,mask); }
    };
    inline bool Finite(const Vec3& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
    inline AmmoConservationPolicy::Point Point(const Vec3& v) {return {v.x,v.y,v.z};}
    inline Vec3 Vector(const AmmoConservationPolicy::Point& p) {return {p[0],p[1],p[2]};}
    inline std::array<AmmoConservationPolicy::StationaryTrack,MAX_PLAYERS> stationaryTracks;
    inline void ObservePlayers(float observationTime)
    {
        // Called at projectile Run entry, before ShouldAim's cooldown gate.
        // Repeated observations of one network sample do not advance history.
        for(auto entity:H::Entities.GetGroup(EntityEnum::PlayerEnemy))
        {
            auto player=entity->As<CTFPlayer>();const int index=player->entindex();
            if(index<=0 || index>=MAX_PLAYERS) continue;
            const float age=observationTime-player->m_flSimulationTime();
            stationaryTracks[index].Observe(player->As<IHandleEntity>()->GetRefEHandle().ToInt(),player->m_flSimulationTime(),
                Point(player->GetAbsOrigin()),player->m_vecVelocity().Length(),player->IsAlive() && !player->IsDormant()
                && player->IsOnGround() && std::isfinite(age) && age>=-.02f && age<=.1f);
        }
    }
    inline bool Uncertain(CTFPlayer* p)
    {
        return p->IsInvulnerable() || p->m_nNumHealers()>0 || p->InCond(TF_COND_STEALTHED) || p->m_bFeignDeathReady()
            || p->InCond(TF_COND_STEALTHED_USER_BUFF_FADING)
            || p->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST) || p->InCond(TF_COND_MEDIGUN_SMALL_BLAST_RESIST)
            || p->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST) || p->InCond(TF_COND_MEDIGUN_SMALL_BULLET_RESIST)
            || p->InCond(TF_COND_MEDIGUN_UBER_FIRE_RESIST) || p->InCond(TF_COND_MEDIGUN_SMALL_FIRE_RESIST)
            || p->InCond(TF_COND_DEFENSEBUFF) || p->InCond(TF_COND_DEFENSEBUFF_NO_CRIT_BLOCK)
            || p->InCond(TF_COND_DEFENSEBUFF_HIGH) || p->InCond(TF_COND_RUNE_RESIST)
            || SDK::AttribHookValue(1.f,"mult_dmgtaken",p)!=1.f
            || SDK::AttribHookValue(1.f,"mult_dmgtaken_from_explosions",p)!=1.f
            || SDK::AttribHookValue(1.f,"mult_dmgtaken_from_bullets",p)!=1.f
            || SDK::AttribHookValue(1.f,"mult_dmgtaken_from_fire",p)!=1.f;
    }
    struct Context
    {
        CTFPlayer* local; CTFWeaponBase* weapon; CUserCmd* cmd;
        // Network entity simulation times must be aged on the unpredicted
        // observation clock, not the local tickbase used to simulate our command.
        const float observationTime;
        bool enabled=false, collected=false, complete=false, stickyRequest=false;
        float baseHorizon=0, horizon=0, estimate=0, spentMs=0;
        float deferredEstimate=0;
        int deferredHealth=0;
        int candidateHealth=0, traces=0, contributions=0, deferredTarget=-1;
        // All pointers, health and damage estimates belong to this command only.
        struct Impact
        {
            CBaseEntity* projectile; CTFWeaponBase* launcher; CTFPlayer* direct;
            Vec3 origin; float damage,radius,delay; bool distance,rocket,fire;
            bool backedSplash=false;
            Vec3 nearOrigin={};
            bool stationaryRocket=false;
        };
        std::vector<Impact> impacts;
        const std::chrono::steady_clock::time_point created=std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point workStart=created;
        struct WorkScope
        {
            Context& context;
            WorkScope(Context& c):context(c) {c.workStart=std::chrono::steady_clock::now();}
            ~WorkScope() {context.spentMs+=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-context.workStart).count();}
        };
        float Age() const { return std::chrono::duration<float>(std::chrono::steady_clock::now()-created).count(); }
        float ChargeBegin() const
        {
            const int id=weapon->GetWeaponID();
            return id==TF_WEAPON_COMPOUND_BOW || id==TF_WEAPON_PIPEBOMBLAUNCHER
                ?weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime():0.f;
        }
        Context(CTFPlayer* p,CTFWeaponBase* w,CUserCmd* c,float observedTime):local(p),weapon(w),cmd(c),observationTime(observedTime)
        {
            ObservePlayers(observationTime);
            // Observe even when disabled: a manual charge must never become ours
            // merely because the user releases the mouse or toggles conservation.
            AmmoConservationPolicy::chargeOwnership.Observe(local->As<IHandleEntity>()->GetRefEHandle().ToInt(),weapon->As<IHandleEntity>()->GetRefEHandle().ToInt(),
                ChargeBegin(),bool(G::OriginalCmd.buttons&(IN_ATTACK|IN_ATTACK2|IN_USE)));
            if (!Vars::Aimbot::Projectile::AmmoConservation.Value || !Vars::Aimbot::General::AutoShoot.Value
                || G::OriginalCmd.buttons&(IN_ATTACK|IN_ATTACK2|IN_USE) || !local->IsAlive() || local->IsAGhost()
                || !G::CanPrimaryAttack || G::Throwing || cmd->weaponselect || I::ClientState->chokedcommands
                || !I::EngineClient->GetNetChannelInfo()) return;
            // A loaded Beggar's or a lit cannon fuse cannot safely wait for damage.
            if (weapon->m_iItemDefinitionIndex()==Soldier_m_TheBeggarsBazooka
                || (weapon->GetWeaponID()==TF_WEAPON_CANNON && weapon->As<CTFGrenadeLauncher>()->m_flDetonateTime()>0)) return;
            baseHorizon=F::Backtrack.GetReal(MAX_FLOWS,false)+std::max(G::Lerp,F::Backtrack.GetFakeInterp())+2*I::GlobalVars->interval_per_tick;
            if (!std::isfinite(baseHorizon) || baseHorizon<=0 || baseHorizon>.15f || !Finite(local->GetAbsOrigin())) return;
            const auto& request=AmmoLifetimeDiagnostics::commandRequests.current;
            stickyRequest=request.requested && (request.sources&1) && request.originalCommand==cmd->command_number && (cmd->buttons&IN_ATTACK2);
            enabled=true;
        }
        bool Fresh()
        {
            const float age=Age(); horizon=baseHorizon+age;
            if (!enabled || age>=.025f || horizon>.15f || G::OriginalCmd.buttons&(IN_ATTACK|IN_ATTACK2|IN_USE)) return false;
            const float begin=ChargeBegin();
            if (!AmmoConservationPolicy::chargeOwnership.Owned(begin)) return false;
            if (weapon->GetWeaponID()==TF_WEAPON_PIPEBOMBLAUNCHER
                && !AmmoConservationPolicy::StickyHoldSafe(begin,TICKS_TO_TIME(local->m_nTickBase()),
                    SDK::AttribHookValue(4.f,"stickybomb_charge_rate",weapon),horizon)) return false;
            return true;
        }
        bool SkipCovered(CBaseEntity* target)
        {
            if(!Covered(target) || !Allow()) return false;
            if(deferredTarget<0)
            {
                deferredTarget=target->entindex();
                deferredEstimate=estimate;deferredHealth=candidateHealth;
            }
            return true;
        }
        bool SkipPredicted(CBaseEntity* entity,float lowerDamage)
        {
            // This is rebuilt from current received entities and a sequential
            // native-motion branch each command. It replaces, never adds to,
            // the old linear independent-projectile sum while impact mode runs.
            if(!Fresh() || !entity || !entity->IsPlayer() || !std::isfinite(lowerDamage))return false;
            auto* target=entity->As<CTFPlayer>();
            const float age=observationTime-target->m_flSimulationTime();
            if(!target->IsAlive()||target->IsDormant()||target->IsAGhost()||Uncertain(target)
                ||target->m_iTeamNum()==local->m_iTeamNum()||!std::isfinite(age)||age<-.02f||age>.1f
                ||!AmmoConservationPolicy::Covered(lowerDamage,target->m_iHealth()))return false;
            estimate=lowerDamage;candidateHealth=target->m_iHealth();
            if(deferredTarget<0){deferredTarget=target->entindex();deferredEstimate=estimate;deferredHealth=candidateHealth;}
            return true;
        }
        bool Finish()
        {
            if(deferredTarget<0) return false;
            estimate=deferredEstimate;candidateHealth=deferredHealth;
            return Suppress();
        }
        bool Allow() { return Fresh(); } // No persisted intervention/cooldown timer.
        void Log(const char* reason,int target,int replacement,bool suppression=false)
        {
            if (SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("ammo_live",std::format(
                "cmd={} weapon={} reason={} target={} replacement={} health={} estimate={} horizon={} traces={} budget_ms={} sources={} suppression={} request_confirmed=0",
                cmd->command_number,weapon->GetWeaponID(),reason,target,replacement,candidateHealth,estimate,horizon,traces,spentMs,contributions,suppression));
        }
        bool Suppress()
        {
            if (!Fresh()) return false;
            // Release-to-fire weapons must keep an owned charge, not accidentally
            // launch it. This is evidence-driven and rechecked on the next command.
            if (ChargeBegin()>0) cmd->buttons|=IN_ATTACK;
            else cmd->buttons&=~IN_ATTACK;
            Log("withhold_redundant_shot",deferredTarget,-1,true);
            return true;
        }
        bool Stable(CTFPlayer* target)
        {
            if (!target || !target->IsAlive() || target->IsDormant() || target->IsAGhost()
                || target->m_iTeamNum()==local->m_iTeamNum() || Uncertain(target)) return false;
            const float age=observationTime-target->m_flSimulationTime();
            // GetChoke is ticks since simtime advanced, including normal snapshot
            // gaps. The measured network age already bounds that uncertainty.
            return std::isfinite(age) && age>=-.02f && age<=.1f
                && target->IsOnGround() && Finite(target->GetAbsOrigin()) && Finite(target->m_vecVelocity())
                && target->m_vecVelocity().Length()<=100.f;
        }
        bool Budget(int cost=1)
        {
            const float active=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-workStart).count();
            if (traces+cost>192 || spentMs+active>=3.f) {complete=false;return false;}
            traces+=cost;return true;
        }
        bool Stationary(CTFPlayer* target)
        {
            if(!Stable(target) || target->m_vecVelocity().Length()>1.f) return false;
            const int index=target->entindex();
            return index>0 && index<MAX_PLAYERS && stationaryTracks[index].Ready(
                target->As<IHandleEntity>()->GetRefEHandle().ToInt(),target->m_flSimulationTime(),Point(target->GetAbsOrigin()));
        }
        bool Payload(CBaseEntity* projectile,Impact& out,Vec3& velocity,float& gravity)
        {
            out={projectile,nullptr,nullptr,{},0,0,0,false,false,false};
            const auto cls=projectile->GetClassID();
            bool grenade=false;
            switch(cls)
            {
            case ETFClassID::CTFGrenadePipebombProjectile:
            case ETFClassID::CTFProjectile_Cleaver:
            case ETFClassID::CTFStunBall:
            case ETFClassID::CTFBall_Ornament: grenade=true;break;
            case ETFClassID::CTFProjectile_Rocket:
            case ETFClassID::CTFProjectile_EnergyBall:
            case ETFClassID::CTFProjectile_Flare:
            case ETFClassID::CTFProjectile_Arrow:
            case ETFClassID::CTFProjectile_HealingBolt:
            case ETFClassID::CTFProjectile_EnergyRing: break;
            default:
                // Utility jars/hooks do no immediate lethal damage. Syringes and
                // flames are not authoritative network projectiles in this SDK.
                // Unknown, spell and conditional effects fail open to normal fire.
                return false;
            }
            if (projectile->IsDormant()) return false;
            auto source=projectile->As<CBaseProjectile>()->m_hOriginalLauncher().Get();
            out.launcher=source?source->As<CTFWeaponBase>():nullptr;
            if (!out.launcher || out.launcher->m_hOwnerEntity().Get()!=local) return false;
            if (grenade)
            {
                auto pipe=projectile->As<CTFGrenadePipebombProjectile>();
                if (pipe->m_hThrower().Get()!=local || pipe->m_iDeflected()) return false;
                if (cls==ETFClassID::CTFGrenadePipebombProjectile && pipe->m_bTouched()) return false;
                if (cls==ETFClassID::CTFGrenadePipebombProjectile && pipe->m_iType()!=TF_GL_MODE_REGULAR
                    && pipe->m_iType()!=TF_GL_MODE_CANNONBALL) return false;
                out.damage=pipe->m_flDamage();
                if (cls==ETFClassID::CTFGrenadePipebombProjectile)
                {
                    // No bounce/fuse or double-donk promise. Network damage is a
                    // lower bound for a fresh direct grenade contact.
                    out.radius=pipe->m_iType()==TF_GL_MODE_REGULAR?pipe->m_DmgRadius():0.f;
                    if (pipe->m_iType()==TF_GL_MODE_CANNONBALL) out.damage=std::min(out.damage,25.f);
                }
            }
            else
            {
                if (cls!=ETFClassID::CTFProjectile_EnergyRing)
                {
                    auto rocket=projectile->As<CTFBaseRocket>();
                    if (rocket->m_hOwnerEntity().Get()!=local || rocket->m_iDeflected()
                        || rocket->m_hLauncher().Get()!=source) return false;
                }
                else if (projectile->As<CTFBaseProjectile>()->m_hLauncher().Get()!=source) return false;
                out.damage=out.launcher->GetDamage(true);
                out.rocket=cls==ETFClassID::CTFProjectile_Rocket || cls==ETFClassID::CTFProjectile_EnergyBall;
                out.distance=out.rocket || cls==ETFClassID::CTFProjectile_EnergyRing;
                out.fire=cls==ETFClassID::CTFProjectile_Flare;
                if (cls==ETFClassID::CTFProjectile_Arrow)
                {
                    auto arrow=projectile->As<CTFProjectile_Arrow>();
                    if (!arrow->m_MoveType()) return false;
                    if (arrow->CanHeadshot())
                    {
                        const float speed=arrow->m_vInitialVelocity().Length();
                        if (!std::isfinite(speed) || speed<1790 || speed>2610) return false;
                        out.damage=SDK::AttribHookValue(50.f,"mult_dmg",out.launcher)
                            +out.damage*std::clamp((speed-1802.f)/800.f,0.f,1.f);
                    }
                    else out.distance=true; // Rescue Ranger, no headshot credit.
                }
                if (cls==ETFClassID::CTFProjectile_HealingBolt) out.damage*=.5f; // Minimum flight-distance damage.
                out.radius=F::AimbotProjectile.GetSplashRadius(projectile,out.launcher,local,1.f);
                if (out.launcher->m_iItemDefinitionIndex()==Soldier_m_RocketJumper) out.damage=0;
            }
            velocity=F::ProjSim.GetVelocity(projectile); gravity=F::ProjSim.GetGravity(projectile,out.launcher);
            return std::isfinite(out.damage) && out.damage>0 && std::isfinite(out.radius) && out.radius>=0
                && Finite(velocity) && velocity.Length()>=100 && velocity.Length()<=4000
                && std::isfinite(gravity) && gravity>=0 && gravity<=2000;
        }
        bool CollectImpacts()
        {
            if (collected) return complete;
            collected=true;complete=true;int count=0;
            for(auto projectile:H::Entities.GetGroup(EntityEnum::WorldProjectile))
            {
                if (++count>128) {complete=false;break;}
                Impact impact;Vec3 velocity;float gravity;
                if (!Payload(projectile,impact,velocity,gravity)) continue;
                const float rawAge=observationTime-projectile->m_flSimulationTime();
                if(!std::isfinite(rawAge) || rawAge<-.02f) continue;
                const float age=std::max(0.f,rawAge);
                if (!AmmoConservationPolicy::StationaryImpactWindow(horizon,0,age)) continue;
                Vec3 origin=projectile->GetAbsOrigin();if (!Finite(origin)) continue;
                const size_t previousImpacts=impacts.size();
                Impact extendedImpact={};bool extendedAvailable=false;
                if(impact.rocket && gravity==0 && impact.radius>0)
                {
                    const auto evidence=AmmoConservationPolicy::TraceStationaryRocket(Point(origin),Point(velocity),horizon,age,
                        [&](const auto& from,const auto& to,uintptr_t skip)
                        {
                            AmmoConservationPolicy::RocketContact contact;
                            if(!Budget()) {contact.solid=true;return contact;}
                            DirectPathFilter path;path.pSkip=local;path.iType=SKIP_CHECK;path.target=reinterpret_cast<IHandleEntity*>(skip);
                            CGameTrace hit={};SDK::Trace(Vector(from),Vector(to),MASK_SHOT,&path,&hit);
                            contact.blocked=hit.DidHit();contact.solid=hit.startsolid || hit.allsolid;
                            contact.world=hit.m_pEnt && hit.m_pEnt->entindex()==0;contact.sky=bool(hit.surface.flags&SURF_SKY);
                            if(hit.m_pEnt && hit.m_pEnt->IsPlayer()) contact.player=reinterpret_cast<uintptr_t>(hit.m_pEnt);
                            contact.fraction=hit.fraction;contact.point=Point(hit.endpos);contact.normal=Point(hit.plane.normal);return contact;
                        },[&](uintptr_t player){return Stationary(reinterpret_cast<CBaseEntity*>(player)->As<CTFPlayer>());});
                    if(evidence.valid)
                    {
                        extendedImpact=impact;extendedImpact.direct=nullptr;extendedImpact.backedSplash=true;extendedImpact.stationaryRocket=true;
                        extendedImpact.nearOrigin=Vector(evidence.nearPoint);extendedImpact.origin=Vector(evidence.worldPoint);extendedImpact.delay=age+evidence.flight;
                        if(AmmoConservationPolicy::PreferStationaryEvidence(false,horizon,evidence.firstFlight,age))
                        {impacts.push_back(extendedImpact);continue;}
                        extendedAvailable=true; // Preserve any stronger short direct-contact evidence.
                    }
                    if(!complete) break;
                }
                if(!AmmoConservationPolicy::ImpactWindow(horizon,0,age)) continue;
                CTraceFilterCollideable filter;filter.pSkip=local;filter.iType=SKIP_CHECK;
                // Crossbow bolts can hit teammates; do not credit an enemy beyond one.
                if(projectile->GetClassID()==ETFClassID::CTFProjectile_HealingBolt) filter.iPlayer=PLAYER_ALL;
                for(int step=0;step<10;++step)
                {
                    if (!Budget()) break;
                    constexpr float dt=.01f;
                    const Vec3 next=origin+velocity*dt-Vec3{0,0,gravity*dt*dt*.5f};
                    CGameTrace hit={};SDK::Trace(origin,next,MASK_SHOT,&filter,&hit);
                    if(hit.startsolid || hit.allsolid) break;
                    if(hit.DidHit())
                    {
                        impact.delay=age+(step+hit.fraction)*dt;
                        if (!AmmoConservationPolicy::ImpactWindow(horizon,(step+hit.fraction)*dt,age)) break;
                        if(hit.m_pEnt && hit.m_pEnt->IsPlayer())
                        {
                            auto victim=hit.m_pEnt->As<CTFPlayer>();
                            if(!Stable(victim)) break;
                            const float total=horizon+impact.delay;
                            const Vec3 predicted=victim->GetAbsOrigin()+victim->m_vecVelocity()*total;
                            const auto arr=[](const Vec3& v){return std::array<float,3>{v.x,v.y,v.z};};
                            // Require interior intersection across the whole remaining
                            // trajectory, not just a hull graze at this trace segment.
                            const Vec3 end=origin+velocity*(.1f-step*dt)-Vec3{0,0,gravity*std::pow(.1f-step*dt,2.f)*.5f};
                            const float core=AmmoConservationPolicy::CoreFraction(arr(origin),arr(end),arr(predicted+victim->m_vecMins()),
                                arr(predicted+victim->m_vecMaxs()),4.f+1000.f*total*total);
                            if(core<0)
                            {
                                // A rocket aimed at ankles often fails the eroded
                                // direct-body check. Require a real world surface
                                // behind it, and later bound both explosion outcomes.
                                if(!impact.rocket || impact.radius<=0 || gravity!=0
                                    || victim->m_vecVelocity().Length()>1.f || !Budget()) break;
                                const float remaining=.25f-horizon-age-step*dt;
                                if(remaining<=0) break;
                                DirectPathFilter backing;backing.pSkip=local;backing.iType=SKIP_CHECK;backing.target=victim;
                                CGameTrace world={};SDK::Trace(origin,origin+velocity*remaining,MASK_SHOT,&backing,&world);
                                if(world.startsolid || world.allsolid || !world.DidHit() || !world.m_pEnt
                                    || world.m_pEnt->entindex()!=0 || !Finite(world.endpos)) break;
                                const float flight=step*dt+remaining*world.fraction;
                                if(!AmmoConservationPolicy::BackedImpactWindow(horizon,flight,age)) break;
                                impact.direct=nullptr;impact.delay=age+flight;impact.backedSplash=true;
                                impact.nearOrigin=hit.endpos;impact.origin=world.endpos+world.plane.normal*.5f;
                                impacts.push_back(impact);
                                break;
                            }
                            if(!Budget()) break;
                            DirectPathFilter beyond; beyond.pSkip=local;beyond.iType=SKIP_CHECK;beyond.target=victim;
                            CGameTrace obstacle={};SDK::Trace(origin,end,MASK_SHOT,&beyond,&obstacle);
                            if(obstacle.startsolid || obstacle.allsolid || (obstacle.DidHit() && core>=obstacle.fraction)) break;
                            // Include the time needed to enter the robust interior,
                            // not only the first grazing collision time.
                            impact.delay=age+step*dt+core*(.1f-step*dt);
                            if(!AmmoConservationPolicy::ImpactWindow(horizon,impact.delay-age,age)) break;
                            impact.direct=victim;impact.origin=hit.endpos;
                            impacts.push_back(impact);
                        }
                        else if(hit.m_pEnt && hit.m_pEnt->entindex()==0 && impact.radius>0
                            && projectile->GetClassID()!=ETFClassID::CTFGrenadePipebombProjectile)
                        {
                            impact.origin=hit.endpos+hit.plane.normal*.5f;impacts.push_back(impact);
                        }
                        break; // Never invent a bounce or credit one projectile twice.
                    }
                    origin=next;velocity.z-=gravity*dt;
                }
                if(complete && extendedAvailable && impacts.size()==previousImpacts) impacts.push_back(extendedImpact);
                if(!complete) break;
            }
            return complete;
        }
        bool Covered(CBaseEntity* entity)
        {
            WorkScope work(*this);
            estimate=0;candidateHealth=0;contributions=0;
            if(!Fresh() || !entity || !entity->IsPlayer()) return false;
            auto target=entity->As<CTFPlayer>();candidateHealth=target->m_iHealth();
            if(!Stable(target) || !CollectImpacts()) return false;
            auto damageRange=H::ConVars.FindVar("tf_damage_range");
            auto disableSpread=H::ConVars.FindVar("tf_damage_disablespread");
            for(const auto& impact:impacts)
            {
                if(!Fresh() || horizon+impact.delay>(impact.stationaryRocket?AmmoConservationPolicy::StationaryRocketWindow:impact.backedSplash?.25f:.15f)) continue;
                if(impact.stationaryRocket && !Stationary(target)) continue;
                if(impact.backedSplash && target->m_vecVelocity().Length()>1.f) continue;
                if(impact.fire && target->InCond(TF_COND_FIRE_IMMUNE)) continue;
                const float total=horizon+impact.delay;
                const float envelope=impact.stationaryRocket?AmmoConservationPolicy::StationaryEnvelope(total):4.f+1000.f*total*total;
                const Vec3 predicted=target->GetCenter()+target->m_vecVelocity()*total;
                float damage=impact.damage;
                if(impact.distance)
                {
                    if(!damageRange || !disableSpread) continue;
                    damage=AmmoConservationPolicy::DistanceDamage(damage,local->GetCenter().DistTo(predicted)
                        +local->m_vecVelocity().Length()*total+envelope,damageRange->GetFloat(),disableSpread->GetBool(),impact.rocket);
                }
                if(impact.direct!=target)
                {
                    if(impact.radius<=0) continue;
                    // A moving direct-hit victim does not provide a fixed splash
                    // origin for another target's lethal accounting.
                    if(impact.direct && impact.direct->m_vecVelocity().Length()>1.f) continue;
                    damage=impact.backedSplash
                        ?AmmoConservationPolicy::BackedSplashDamage(damage,impact.nearOrigin.DistTo(predicted),impact.origin.DistTo(predicted),impact.radius,envelope)
                        :AmmoConservationPolicy::SplashDamage(damage,impact.origin.DistTo(predicted)+envelope,impact.radius);
                    if(damage<=0) continue;
                    const Vec3 offsets[]={{},{envelope,0,0},{-envelope,0,0},{0,envelope,0},{0,-envelope,0},{0,0,envelope},{0,0,-envelope}};
                    bool visible=true;
                    const int samples=impact.backedSplash?4:0;
                    for(int sample=0;sample<=samples && visible;++sample)
                    {
                        const Vec3 explosion=samples?impact.nearOrigin+(impact.origin-impact.nearOrigin)*(float(sample)/samples):impact.origin;
                        for(const auto& offset:offsets)
                        {
                            Vec3 probe=predicted+offset;
                            if(impact.backedSplash) probe.z=AmmoConservationPolicy::GroundedProbeHeight(probe.z,target->GetAbsOrigin().z);
                            if(!Budget() || !SDK::VisPosWorld(impact.projectile,nullptr,explosion,probe,MASK_SHOT)) {visible=false;break;}
                        }
                    }
                    if(!complete) return false;
                    if(!visible) continue;
                }
                // Every target gets its own sum. No global "this explosion kills
                // everyone nearby" flag, and no direct-plus-splash double count.
                if(std::isfinite(damage) && damage>0) {estimate+=std::floor(damage);++contributions;}
            }
            if(stickyRequest && (cmd->buttons&IN_ATTACK2)) for(auto entity:H::Entities.GetGroup(EntityEnum::LocalStickies))
            {
                auto sticky=entity->As<CTFGrenadePipebombProjectile>();
                const float age=I::GlobalVars->curtime-sticky->m_flCreationTime();
                auto source=sticky->m_hOriginalLauncher().Get();auto launcher=source?source->As<CTFWeaponBase>():nullptr;
                if(!launcher || launcher->GetWeaponID()!=TF_WEAPON_PIPEBOMBLAUNCHER || launcher->m_hOwnerEntity().Get()!=local
                    || launcher->As<CTFPipebombLauncher>()->GetDetonateType()==TF_DETONATE_MODE_DOT
                    || sticky->m_hThrower().Get()!=local || sticky->m_iDeflected() || !sticky->m_bTouched()
                    || !std::isfinite(age) || age<std::max(5.f,SDK::AttribHookValue(.8f,"sticky_arm_time",launcher))
                    || !Finite(sticky->GetAbsVelocity()) || sticky->GetAbsVelocity().Length()>1.f) continue;
                const Vec3 origin=sticky->GetAbsOrigin();if(!Finite(origin)) continue;
                const float envelope=4.f+1000.f*horizon*horizon;
                const Vec3 predicted=target->GetCenter()+target->m_vecVelocity()*horizon;
                const float radius=F::AimbotProjectile.GetSplashRadius(sticky,launcher,local,1.f);
                const float damage=AmmoConservationPolicy::SplashDamage(sticky->m_flDamage(),origin.DistTo(predicted)+envelope,radius);
                if(damage<=0) continue;
                const Vec3 offsets[]={{},{envelope,0,0},{-envelope,0,0},{0,envelope,0},{0,-envelope,0},{0,0,envelope},{0,0,-envelope}};
                bool visible=true;
                for(const auto& offset:offsets)
                    if(!Budget() || !SDK::VisPosWorld(sticky,nullptr,origin,predicted+offset,MASK_SHOT)) {visible=false;break;}
                if(!complete) return false;
                if(visible) {estimate+=std::floor(damage);++contributions;}
            }
            return Fresh() && complete && AmmoConservationPolicy::Covered(estimate,target->m_iHealth());
        }
    };
}
