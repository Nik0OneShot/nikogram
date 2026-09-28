#pragma once
#include "SelfDamageDiagnostics.h"
#include "AmmoEvidencePolicy.h"
#include <chrono>

namespace DetonationExposureDiagnostics
{
    // Request-triggered snapshot; never calls aiming, detonation or shot control.
    // Estimates deliberately do not claim a lethal outcome: target defenses,
    // latency, movement and server acceptance are not modeled here.
    inline void Run(CTFPlayer* local, CTFWeaponBase* active, const CUserCmd* cmd, bool automatic, int sources, int originalCommand)
    {
        const bool manual = (G::OriginalCmd.buttons & IN_ATTACK2) != 0;
        const bool finalRequest = (cmd->buttons & IN_ATTACK2) != 0;
        if (!(Vars::Aimbot::Projectile::AmmoEvidenceSources.Value & 2)) return;
        if (!finalRequest || (automatic && !(sources & 1))) return;
        const auto start=std::chrono::steady_clock::now();
        int players=0, pairs=0, traces=0, exposed=0, records=0;
        bool complete=true;
        auto budget=[&] { return std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-start).count()<2.f; };
        for(auto entity : H::Entities.GetGroup(EntityEnum::PlayerAll))
        {
            if(++players>64 || !budget()) {complete=false;break;}
            auto target=entity->As<CTFPlayer>();
            if(target==local || target->IsDormant() || !target->IsAlive() || target->IsAGhost() || target->m_iTeamNum()==local->m_iTeamNum()) continue;
            float screenDamage=0; int contributions=0;
            for(auto projectile : H::Entities.GetGroup(EntityEnum::LocalStickies))
            {
                if(++pairs>512 || !budget()) {complete=false;break;}
                auto sticky=projectile->As<CTFGrenadePipebombProjectile>();
                if(sticky->m_hThrower().Get()!=local || sticky->m_iDeflected()!=0) continue;
                auto launcherEntity=sticky->m_hOriginalLauncher().Get();
                auto launcher=launcherEntity?launcherEntity->As<CTFWeaponBase>():nullptr;
                if(!launcher || launcher->m_hOwnerEntity().Get()!=local || launcher->GetWeaponID()!=TF_WEAPON_PIPEBOMBLAUNCHER) continue;
                // Selective detonation needs the exact selected set, not all bombs.
                if(launcher->As<CTFPipebombLauncher>()->GetDetonateType()==TF_DETONATE_MODE_DOT) continue;
                if(!automatic && active!=launcher) continue;
                const float age=I::GlobalVars->curtime-sticky->m_flCreationTime();
                if(!std::isfinite(age) || sticky->m_flCreationTime()<=0 || age<SDK::AttribHookValue(.8f,"sticky_arm_time",launcher)) continue;
                if(!sticky->m_bTouched() || sticky->GetAbsVelocity().Length()>=1.f) continue;
                const auto origin=sticky->GetAbsOrigin(); Vec3 nearest;
                target->m_Collision()->CalcNearestPoint(origin,&nearest);
                const float radius=SDK::AttribHookValue(TF_ROCKET_RADIUS,"mult_explosion_radius",launcher);
                const float damage=AmmoEvidencePolicy::SplashScreen(sticky->m_flDamage(),origin.DistTo(nearest),radius);
                if(damage<=0) continue;
                if(traces>=16) {complete=false;break;}
                ++traces;
                if(!SDK::VisPosCollideable(sticky,target,origin,target->GetCenter(),MASK_SHOT)) continue;
                ++exposed; ++contributions; screenDamage+=damage;
            }
            if(contributions && records++<8)
                SelfDamageDiagnostics::Write("ammo_request_exposure",std::format(
                    "cmd={} original_cmd={} target={} hp={} contributions={} screen_damage={} auto_requested={} manual={} invulnerable={} target_scan_complete={} damage_model=screen_only defenses_modeled=0 lethal_confirmed=0 suppression=0 switching=0",
                    cmd->command_number,originalCommand,target->entindex(),target->m_iHealth(),contributions,screenDamage,automatic,manual,target->IsInvulnerable(),complete));
            if(!complete) break;
        }
        SelfDamageDiagnostics::Write("ammo_request_audit",std::format(
            "cmd={} original_cmd={} auto_requested={} manual={} autoshoot={} players={} pairs={} traces={} exposed_pairs={} complete={} details_omitted={} elapsed_ms={} final_attack2=1 damage_model=screen_only",
            cmd->command_number,originalCommand,automatic,manual,Vars::Aimbot::General::AutoShoot.Value,players,pairs,traces,exposed,complete,std::max(records-8,0),
            std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-start).count()));
    }
}
