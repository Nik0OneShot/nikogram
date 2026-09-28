#pragma once
#include "ProjectileDiagnostics.h"
#include "AmmoEvidencePolicy.h"
#include <chrono>

namespace AmmoEvidenceDiagnostics
{
    // Called only for the chosen, fire-capable projectile candidate. No command,
    // entity, simulation, or targeting state is written by this audit.
    inline void Selected(CTFPlayer* local, CTFWeaponBase* weapon, CBaseEntity* entity, const CUserCmd* cmd)
    {
        if (!Vars::Aimbot::Projectile::AmmoEvidenceDiagnostics.Value || !ProjectileDiagnostics::current
            || !SelfDamageDiagnostics::Enabled() || !Vars::Aimbot::General::AutoShoot.Value
            || !entity || !entity->IsPlayer() || !local || !weapon || !cmd) return;
        auto target = entity->As<CTFPlayer>();
        if (!target->IsAlive() || target->m_iTeamNum() == local->m_iTeamNum()) return;
        const bool manual = (G::OriginalCmd.buttons & (IN_ATTACK | IN_ATTACK2 | IN_USE)) != 0;
        const bool uncertain = target->IsInvulnerable() || target->InCond(TF_COND_STEALTHED) || target->m_bFeignDeathReady()
            || target->InCond(TF_COND_STEALTHED_USER_BUFF_FADING)
            || target->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST) || target->InCond(TF_COND_MEDIGUN_SMALL_BLAST_RESIST)
            || target->InCond(TF_COND_DEFENSEBUFF) || target->InCond(TF_COND_DEFENSEBUFF_NO_CRIT_BLOCK)
            || target->InCond(TF_COND_DEFENSEBUFF_HIGH) || target->InCond(TF_COND_RUNE_RESIST)
            || SDK::AttribHookValue(1.f, "mult_dmgtaken", target) != 1.f
            || SDK::AttribHookValue(1.f, "mult_dmgtaken_from_explosions", target) != 1.f;
        const auto start = std::chrono::steady_clock::now();
        int scanned = 0, own = 0, pills = 0, stickies = 0, reflected = 0, details = 0, traces = 0;
        int unknownFuse = 0, eligible = 0, unsupported = 0;
        bool complete = true;
        float screenedDamage = 0;
        const float now = I::GlobalVars->curtime;
        for (auto projectile : H::Entities.GetGroup(EntityEnum::WorldProjectile))
        {
            if (++scanned > 128 || std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now()-start).count() >= 2.f)
            { complete = false; break; }
            if (projectile->GetClassID() != ETFClassID::CTFGrenadePipebombProjectile)
            {
                if (projectile->m_hOwnerEntity().Get() == local) ++unsupported;
                continue;
            }
            auto pipe = projectile->As<CTFGrenadePipebombProjectile>();
            if (pipe->m_hThrower().Get() != local) continue;
            ++own;
            if (pipe->m_iDeflected() != 0) { ++reflected; continue; }
            const bool sticky = pipe->m_iType() == TF_GL_MODE_REMOTE_DETONATE;
            if (!sticky && pipe->m_iType() != TF_GL_MODE_REGULAR) { ++unsupported; continue; }
            sticky ? ++stickies : ++pills;
            const int sourceBit = sticky ? 2 : 1;
            if (!(Vars::Aimbot::Projectile::AmmoEvidenceSources.Value & sourceBit)) continue;
            auto launcherEntity = pipe->m_hOriginalLauncher().Get();
            auto launcher = launcherEntity ? launcherEntity->As<CTFWeaponBase>() : nullptr;
            const bool ownerVerified = launcher && launcher->m_hOwnerEntity().Get() == local;
            const float age = now - pipe->m_flCreationTime();
            // Never substitute the launcher's charge timer for the pill's fuse.
            if (!sticky) ++unknownFuse;
            const bool armed = sticky && ownerVerified && pipe->m_flCreationTime() > 0 && std::isfinite(age)
                && age >= SDK::AttribHookValue(.8f, "sticky_arm_time", launcher);
            const bool request = sticky && launcher == weapon && weapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER
                && weapon->As<CTFPipebombLauncher>()->GetDetonateType() != TF_DETONATE_MODE_DOT
                && (cmd->buttons & IN_ATTACK2) && !(cmd->buttons & IN_ATTACK) && !I::ClientState->chokedcommands;
            // Moving explosives need a real future trajectory, not current proximity.
            const bool settled = pipe->m_bTouched() && pipe->GetAbsVelocity().Length() < 1.f;
            float radius = ownerVerified ? SDK::AttribHookValue(TF_ROCKET_RADIUS, "mult_explosion_radius", launcher) : 0;
            const Vec3 origin = pipe->GetAbsOrigin();
            Vec3 nearest; target->m_Collision()->CalcNearestPoint(origin, &nearest);
            const float distance = origin.DistTo(nearest);
            float estimate = AmmoEvidencePolicy::SplashScreen(pipe->m_flDamage(), distance, radius);
            bool exposed = false;
            if (estimate > 0)
            {
                if (traces >= 16) { complete = false; break; }
                ++traces;
                exposed = SDK::VisPosCollideable(pipe, target, origin, target->GetCenter(), MASK_SHOT);
            }
            if (!exposed) estimate = 0;
            const bool timingCandidate = AmmoEvidencePolicy::TimingCandidate(sticky, armed, request, settled, manual, uncertain);
            if (timingCandidate && estimate > 0) { screenedDamage += estimate; ++eligible; }
            if (details++ < 6)
            {
                const auto velocity = pipe->GetAbsVelocity();
                SelfDamageDiagnostics::Write("ammo_evidence", std::format(
                    "cmd={} target={} projectile={} source={} age={} touched={} owner_verified={} armed={} detonate_request={} settled={} distance={} radius={} exposed={} nominal={} screen_damage={} timing={} eligible={} actual_damage_confirmed=0 origin={},{},{} velocity={},{},{}",
                    cmd->command_number, target->entindex(), pipe->As<IHandleEntity>()->GetRefEHandle().ToInt(), sticky ? "sticky" : "pill", age,
                    pipe->m_bTouched(), ownerVerified, armed, request, settled, distance, radius, exposed, pipe->m_flDamage(), estimate,
                    sticky ? (request ? "request_only" : "no_request") : "fuse_unverified", timingCandidate && estimate > 0,
                    origin.x, origin.y, origin.z, velocity.x, velocity.y, velocity.z));
            }
        }
        const bool emergency = target->GetAbsOrigin().DistTo(local->GetAbsOrigin()) < 200.f;
        const bool covered = AmmoEvidencePolicy::ScreenCovered(screenedDamage, target->m_iHealth(), complete, uncertain || manual || emergency);
        const auto targetOrigin = target->GetAbsOrigin(), targetVelocity = target->m_vecVelocity();
        SelfDamageDiagnostics::Write("ammo_shadow", std::format(
            "cmd={} target={} hp={} autoshoot=1 manual={} uncertain_defense={} close_threat={} sources={} own_pipes={} pills={} stickies={} reflected={} other_unsupported={} fuse_unknown={} eligible={} traces={} complete={} screen_damage={} decision={} alternative_validated=0 suppression=0 switching=0 damage_model=screen_only elapsed_ms={} weapon={} can_fire={} buttons_before_autoshoot={} target_origin={},{},{} target_velocity={},{},{}",
            cmd->command_number, target->entindex(), target->m_iHealth(), manual, uncertain, emergency,
            Vars::Aimbot::Projectile::AmmoEvidenceSources.Value, own, pills, stickies, reflected, unsupported, unknownFuse, eligible, traces, complete, screenedDamage,
            covered ? "would_search_alternative" : "keep_normal_fire",
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now()-start).count(), weapon->GetWeaponID(), G::CanPrimaryAttack,
            cmd->buttons, targetOrigin.x, targetOrigin.y, targetOrigin.z, targetVelocity.x, targetVelocity.y, targetVelocity.z));
    }
}
