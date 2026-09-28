#pragma once
#include "SelfDamageDiagnostics.h"
#include "AmmoLifetimePolicy.h"
#include "DetonationExposureDiagnostics.h"
#include <array>

namespace AmmoLifetimeDiagnostics
{
    inline AmmoLifetimePolicy::CommandRequests commandRequests{};
    inline void Begin(int command) { commandRequests.Begin(command); }
    inline void AutoRequest(int command, int sources)
    {
        commandRequests.Record(command, sources);
    }
    struct Entry
    {
        bool used = false, seen = false, touched = false;
        int handle = 0, type = 0, misses = 0;
        float creation = 0, first = 0, last = 0;
    };
    inline std::array<Entry, 128> entries{};
    inline int previousLocal = 0, previousCommand = -1;
    inline float previousTime = 0;
    inline AmmoLifetimePolicy::RequestGate gate{};
    inline void Reset(const char* reason, int command = -1, int local = 0, float now = 0)
    {
        // Log only transitions out of an active session, not every dead/idle
        // command. The master diagnostic toggle still controls all file writes.
        if (previousCommand >= 0)
        {
            int tracked = 0;
            for (const auto& e : entries) if (e.used) ++tracked;
            SelfDamageDiagnostics::Write("ammo_lifetime_reset", std::format(
                "reason={} cmd={} previous_cmd={} local={} previous_local={} time={} previous_time={} tracked={}",
                reason, command, previousCommand, local, previousLocal, now, previousTime, tracked));
        }
        entries = {}; gate = {}; previousLocal = 0; previousCommand = -1; previousTime = 0;
    }
    // Final command observation is independent of Auto Shoot, aim selection and
    // the 500ms projectile sample. No entity pointers survive this call.
    inline void Final(CTFPlayer* local, CTFWeaponBase* weapon, const CUserCmd* cmd)
    {
        // Consume once per CreateMove invocation, even on disabled/dead paths.
        // Later rewrites of cmd->command_number cannot lose this request.
        const auto observedRequest = commandRequests.Take();
        const bool autoRequest = observedRequest.requested;
        const int autoRequestSources = observedRequest.sources;
        const char* inactiveReason = !SelfDamageDiagnostics::Enabled() ? "logging_disabled"
            : !Vars::Aimbot::Projectile::AmmoEvidenceDiagnostics.Value ? "audit_disabled"
            : !local ? "no_local" : !local->IsAlive() ? "local_dead"
            : !cmd ? "no_command" : !I::GlobalVars ? "no_globals" : nullptr;
        if (inactiveReason)
        { Reset(inactiveReason, cmd ? cmd->command_number : -1); return; }
        const float rawTime = I::GlobalVars->curtime;
        const int localHandle = local->As<IHandleEntity>()->GetRefEHandle().ToInt();
        if (const auto reason = AmmoLifetimePolicy::ResetReason(localHandle, previousLocal, cmd->command_number, previousCommand, rawTime, previousTime))
            Reset(reason, cmd->command_number, localHandle, rawTime);
        if (!std::isfinite(rawTime)) return;
        // Keep the observation clock monotonic across tolerated corrections.
        // Compare future corrections against this high-water mark, not a
        // repeatedly decreasing clock that could hide a cumulative rewind.
        const float now=std::max(rawTime,previousTime);
        previousLocal = localHandle; previousCommand = cmd->command_number; previousTime = now;
        const bool attack2 = (cmd->buttons & IN_ATTACK2) != 0;
        const bool explosiveSecondary = weapon && (weapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER
            || weapon->GetWeaponID() == TF_WEAPON_GRENADELAUNCHER || weapon->GetWeaponID() == TF_WEAPON_CANNON);
        const bool request = (attack2 && explosiveSecondary) || autoRequest;
        const int requestContext = (weapon ? weapon->GetWeaponID() * 16 : 0)
            + (autoRequest ? autoRequestSources : 0) + (attack2 ? 4 : 0) + ((G::OriginalCmd.buttons & IN_ATTACK2) ? 8 : 0);
        const bool recordRequest = gate.Due(request, GetTickCount64(), requestContext);
        for (auto& e : entries) e.seen = false;
        int scanned = 0, pills = 0, stickies = 0, reflected = 0, armed = 0, details = 0;
        bool complete = true, trackingFull = false;
        for (auto entity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
        {
            if (++scanned > 512) { complete = false; break; }
            if (entity->GetClassID() != ETFClassID::CTFGrenadePipebombProjectile) continue;
            auto pipe = entity->As<CTFGrenadePipebombProjectile>();
            if (pipe->m_hThrower().Get() != local) continue;
            if (pipe->m_iDeflected()) { ++reflected; continue; }
            const int type = pipe->m_iType();
            const bool sticky = type == TF_GL_MODE_REMOTE_DETONATE;
            if (!sticky && type != TF_GL_MODE_REGULAR) continue;
            if (!(Vars::Aimbot::Projectile::AmmoEvidenceSources.Value & (sticky ? 2 : 1))) continue;
            sticky ? ++stickies : ++pills;
            const int handle = pipe->As<IHandleEntity>()->GetRefEHandle().ToInt();
            const float creation = pipe->m_flCreationTime();
            auto launcherEntity = pipe->m_hOriginalLauncher().Get();
            auto launcher = launcherEntity ? launcherEntity->As<CTFWeaponBase>() : nullptr;
            const bool owner = launcher && launcher->m_hOwnerEntity().Get() == local;
            const bool isArmed = sticky && owner && creation > 0 && std::isfinite(creation)
                && rawTime >= creation + SDK::AttribHookValue(.8f, "sticky_arm_time", launcher);
            if (isArmed) ++armed;
            Entry* entry = nullptr;
            for (auto& e : entries) if (e.used && e.handle == handle) { entry = &e; break; }
            bool fresh = !entry;
            if (!entry) for (auto& e : entries) if (!e.used) { entry = &e; break; }
            // Capacity pressure must not prevent retiring old entries after a
            // complete world scan, otherwise a full ledger can never recover.
            if (!entry) { trackingFull = true; continue; }
            if (fresh) *entry = {true, false, false, handle, type, 0, creation, now, now};
            const bool contact = pipe->m_bTouched() && !entry->touched;
            entry->seen = true; entry->last = now; entry->touched = pipe->m_bTouched();
            if ((fresh || contact || (recordRequest && sticky)) && details++ < 16)
            {
                const auto origin = pipe->GetAbsOrigin();
                SelfDamageDiagnostics::Write("ammo_lifetime", std::format(
                    "cmd={} projectile={} source={} transition={} creation={} age={} observed_for={} touched={} owner_verified={} armed={} request_cmd={} origin={},{},{} fuse_verified=0",
                    cmd->command_number, handle, sticky ? "sticky" : "pill", fresh ? "first_seen" : contact ? "contact" : "request_snapshot",
                    creation, now-creation, now-entry->first, entry->touched, owner, isArmed, recordRequest ? cmd->command_number : -1,
                    origin.x, origin.y, origin.z));
            }
        }
        int omitted = std::max(details-16, 0);
        for (auto& e : entries)
        {
            if (!e.used || !AmmoLifetimePolicy::Missing(e.seen, complete, e.misses)) continue;
            if (details++ < 16)
                SelfDamageDiagnostics::Write("ammo_lifetime", std::format(
                    "cmd={} projectile={} source={} transition=not_observed creation={} last_age={} observed_for={} missing_for={} touched={} cause=unknown explosion_confirmed=0 fuse_verified=0",
                    cmd->command_number, e.handle, e.type == TF_GL_MODE_REMOTE_DETONATE ? "sticky" : "pill", e.creation,
                    e.last-e.creation, e.last-e.first, now-e.last, e.touched));
            else ++omitted;
            e = {};
        }
        if (recordRequest)
        {
            SelfDamageDiagnostics::Write("ammo_detonation_request", std::format(
                "cmd={} auto_requested={} auto_sources={} original_attack2={} final_attack2={} weapon={} selected_target_required=0 autoshoot={} choked={} pills={} stickies={} armed={} reflected={} complete={} details_omitted={} server_detonation_confirmed=0 suppression=0 switching=0 tracking_full={} original_cmd={} request_cmd={} command_rewritten={}",
                cmd->command_number, autoRequest, autoRequest ? autoRequestSources : 0, bool(G::OriginalCmd.buttons & IN_ATTACK2), attack2,
                weapon ? weapon->GetWeaponID() : -1, Vars::Aimbot::General::AutoShoot.Value, G::Choking, pills, stickies, armed, reflected, complete && !trackingFull, omitted, trackingFull,
                observedRequest.originalCommand, observedRequest.requestCommand, observedRequest.originalCommand != cmd->command_number));
            DetonationExposureDiagnostics::Run(local,weapon,cmd,autoRequest,autoRequestSources,observedRequest.originalCommand);
        }
        else if (!complete || omitted || trackingFull)
            SelfDamageDiagnostics::Write("ammo_lifetime_limits", std::format("cmd={} complete={} details_omitted={} tracking_full={}", cmd->command_number, complete && !trackingFull, omitted, trackingFull), true);
    }
}
