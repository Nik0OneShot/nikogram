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
        bool enabled=false, permitted=false, windowChecked=false;
        float horizon=0, baseHorizon=0, spentMs=0, estimate=0;
        const std::chrono::steady_clock::time_point created=std::chrono::steady_clock::now();
        int traces=0;
        Context(CTFPlayer* p, CTFWeaponBase* w, CUserCmd* c):local(p),weapon(w),cmd(c)
        {
            if (!Vars::Aimbot::Projectile::AmmoConservation.Value) return;
            const auto& request=AmmoLifetimeDiagnostics::commandRequests.current;
            const char* reason=nullptr;
            if (!Vars::Aimbot::General::AutoShoot.Value) reason="autoshoot_off";
            else if (G::OriginalCmd.buttons&(IN_ATTACK|IN_ATTACK2|IN_USE)) reason="manual_input";
            else if (!local->IsAlive() || local->IsAGhost()) reason="local_inactive";
            else if (weapon->GetWeaponID()!=TF_WEAPON_GRENADELAUNCHER) reason="unsupported_weapon";
            else if (!G::CanPrimaryAttack || G::Throwing || cmd->weaponselect) reason="weapon_not_ready";
            else if (I::ClientState->chokedcommands) reason="choked_command";
            else if (!request.requested || !(request.sources&1) || request.originalCommand!=cmd->command_number || !(cmd->buttons&IN_ATTACK2)) reason="no_sticky_request";
            else if (!I::EngineClient->GetNetChannelInfo()) reason="no_network";
            if (!reason)
            {
                // Full measured latency (including artificial latency), interpolation,
                // and two ticks of scheduling margin. High delays disable the action.
                horizon=F::Backtrack.GetReal(MAX_FLOWS,false)+std::max(G::Lerp,F::Backtrack.GetFakeInterp())+2*I::GlobalVars->interval_per_tick;
                if (!std::isfinite(horizon) || horizon<=0 || horizon>.15f) reason="delay_uncertain";
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
            SelfDamageDiagnostics::Write("ammo_live",std::format(
                "cmd={} reason={} target={} replacement={} estimate={} horizon={} traces={} budget_ms={} suppression={} alternative_validated={} request_confirmed=0",
                cmd->command_number,reason,target,replacement,estimate,horizon,traces,spentMs,suppression,replacement>=0));
        }
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
            return enabled && age<.025f && horizon<=.15f && (cmd->buttons&IN_ATTACK2)
                && !(G::OriginalCmd.buttons&(IN_ATTACK|IN_ATTACK2|IN_USE));
        }
        bool Covered(CBaseEntity* entity)
        {
            estimate=0;
            if (!Fresh() || !entity || !entity->IsPlayer()) return false;
            auto target=entity->As<CTFPlayer>();
            if (!target->IsAlive() || target->IsDormant() || target->IsAGhost() || target->m_iTeamNum()==local->m_iTeamNum()
                || Uncertain(target) || !target->IsOnGround() || H::Entities.GetChoke(target->entindex())>0) return false;
            const float observationAge=I::GlobalVars->curtime-target->m_flSimulationTime();
            if (!std::isfinite(observationAge) || std::abs(observationAge)>.25f) return false;
            const auto velocity=target->m_vecVelocity(), center=target->GetCenter();
            if (!Finite(velocity) || !Finite(center) || velocity.Length()>100.f
                || !std::isfinite(target->m_flMaxspeed()) || target->m_flMaxspeed()>600.f) return false;
            // Reserve movement in any direction at 600 units/s, plus a geometry
            // margin. This is deliberately stricter than constant-velocity lead.
            const float travel=600.f*horizon+8.f;
            const auto start=std::chrono::steady_clock::now();
            auto elapsed=[&] {return std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-start).count();};
            bool complete=true; int count=0;
            for (auto entity:H::Entities.GetGroup(EntityEnum::LocalStickies))
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
                if (visible) estimate+=damage;
            }
            spentMs+=elapsed();
            if (!complete || spentMs>=2.f) {Log("incomplete_exposure",target->entindex(),-1);return false;}
            const bool covered=AmmoConservationPolicy::Covered(estimate,target->m_iHealth());
            if (covered) Log("covered_candidate",target->entindex(),-1);
            return covered;
        }
    };
}
