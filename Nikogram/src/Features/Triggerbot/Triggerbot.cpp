#include "../Backtrack/NamedPlayerTraceDiagnostics.h"
#include "Triggerbot.h"
#include "../Aimbot/Aimbot.h"
#include "../Aimbot/SelfDamageDiagnostics.h"
#include "../Backtrack/Backtrack.h"
#include "../Binds/Binds.h"
#include "../ImGui/Menu/Menu.h"
#include <random>

static bool AutomaticTriggerWeapon(CTFWeaponBase* weapon)
{
    if(!weapon)return false;
    switch(weapon->GetWeaponID())
    {
    case TF_WEAPON_MINIGUN:case TF_WEAPON_SMG:case TF_WEAPON_CHARGED_SMG:
    case TF_WEAPON_PISTOL:case TF_WEAPON_PISTOL_SCOUT:case TF_WEAPON_HANDGUN_SCOUT_SECONDARY:return true;
    default:return false;
    }
}
static TriggerAutomaticPolicy::Context AutomaticContext(CTFPlayer* local,CTFWeaponBase* weapon)
{
    return {local?uint32_t(local->GetRefEHandle().ToInt()):0,weapon?uint32_t(weapon->GetRefEHandle().ToInt()):0,
        local?local->m_iSpawnCounter():0,I::ClientState->m_nServerCount};
}

bool CTriggerbot::RunAutomatic(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd)
{
    if(!AutomaticTriggerWeapon(weapon)){m_automatic.Cancel();return false;}
    m_timer.Reset();
    const char* gate=!cmd?"no_command":cmd->command_number<=0?"invalid_command":!local?"no_local"
        :!local->IsAlive()?"local_dead":local->IsAGhost()?"local_ghost":!local->CanAttack()?"local_cannot_attack"
        :I::EngineVGui->IsGameUIVisible()?"game_ui":F::Menu.m_bIsOpen?"menu":!SDK::IsGameWindowInFocus()?"unfocused"
        :!Vars::Triggerbot::Enabled.Value?"disabled":!TriggerAutomaticPolicy::ObserverReady()?"automatic_observer_unavailable"
        :!(Vars::Triggerbot::Hitboxes.Value&31)?"hitboxes_off"
        :G::PrimaryWeaponType!=EWeaponType::HITSCAN?"not_hitscan"
        :cmd->weaponselect||G::OriginalCmd.weaponselect?"weapon_selection"
        :(G::OriginalCmd.buttons&IN_ATTACK)?"manual_primary":(cmd->buttons&IN_ATTACK)?"primary_already_requested"
        :((cmd->buttons|G::OriginalCmd.buttons)&IN_USE)?"using_action"
        :G::Reloading?"automatic_reload":!weapon->HasPrimaryAmmoForShot()?"primary_ammo"
        :SDK::AttribHookValue(1,"mult_dmg",weapon)<=0.f?"weapon_no_damage":nullptr;
    const bool active=Vars::Triggerbot::Activation.Value;
    int releaseCount=3;
    if(!gate && !active && m_automatic.phase==TriggerAutomaticPolicy::Phase::Held)
    {
        // Independent RNG, sampled exactly once per effective activation release.
        static std::mt19937 generator(unsigned(GetTickCount64())^GetCurrentProcessId());
        releaseCount=std::uniform_int_distribution<int>(3,6)(generator);
    }
    m_automatic.Begin(AutomaticContext(local,weapon),cmd?G::OriginalCmd.command_number:0,
        active,!gate,double(GetTickCount64())*.001,releaseCount);
    if(gate){Diagnostic(gate,local,weapon,cmd);return true;}
    if(!m_automatic.CanRequest())
    {Diagnostic(m_automatic.phase==TriggerAutomaticPolicy::Phase::Tail?"automatic_pending":"activation_inactive",local,weapon,cmd);return true;}
    m_owner=uint32_t(local->GetRefEHandle().ToInt());m_spawn=local->m_iSpawnCounter();m_server=I::ClientState->m_nServerCount;
    m_triggerAllowed=true;m_shotAutomatic=true;m_automaticInput=G::OriginalCmd.command_number;
    m_shotCommand=cmd->command_number;m_shotWeapon=weapon->GetRefEHandle().ToInt();m_shotTarget=-1;
    m_beforeShotTick=m_shotTick=cmd->tick_count;
    // Keep primary held through native cooldown and minigun windup. Those are
    // requests, never successful-shot counts. No cursor acquisition is required.
    cmd->buttons|=IN_ATTACK;G::Attacking=SDK::IsAttacking(local,weapon,cmd,true);
    Diagnostic(m_automatic.phase==TriggerAutomaticPolicy::Phase::Tail?"automatic_tail":"automatic_held",local,weapon,cmd);
    return true;
}

CTriggerbot::NativeObservation CTriggerbot::BeginNative(CTFPlayer* local,int sequence,CUserCmd* cmd)
{
    const auto previous=m_native;m_native={};
    if(local && cmd && local==H::Entities.GetLocal() && I::Prediction->m_bFirstTimePredicted)
    {
        auto* entity=local->m_hActiveWeapon().Get();
        auto* weapon=entity?entity->As<CTFWeaponBase>():nullptr;
        if(weapon && AutomaticTriggerWeapon(weapon))
            m_native={AutomaticContext(local,weapon),local,cmd,sequence,cmd->command_number,true};
    }
    return previous;
}
void CTriggerbot::EndNative(const NativeObservation& previous)
{
    // Refund a command which completed native prediction without a discharge.
    // This never decrements the release-tail count.
    if(m_native.valid)m_automatic.Settle(m_native.context,m_native.sequence,m_native.outgoing);
    m_native=previous;
}
void CTriggerbot::ObserveNativeShot(CTFWeaponBase* weapon,int player,int mode)
{
    if(!m_native.valid || !weapon || mode!=TF_WEAPON_PRIMARY_MODE || !I::Prediction->m_bFirstTimePredicted
        || player!=I::EngineClient->GetLocalPlayer() || H::Entities.GetLocal()!=m_native.local)return;
    // RunSimulation copies its incoming command into the player's simulation
    // context. RunCommand exposes that copy, not the incoming command pointer.
    const auto* nativeCommand=m_native.local->m_pCurrentCommand();
    if(!nativeCommand || !(nativeCommand->buttons&IN_ATTACK)
        || nativeCommand->command_number!=m_native.outgoing || m_native.command->command_number!=m_native.outgoing
        || AutomaticContext(m_native.local,weapon)!=m_native.context)return;
    if(m_automatic.Observe(m_native.context,m_native.sequence,m_native.outgoing) && SelfDamageDiagnostics::TriggerEnabled())
        SelfDamageDiagnostics::WriteTrigger("trigger_discharge",std::format(
            "sequence={} outgoing={} phase={} selected={} remaining={} native_local_discharge=1 server_ack=0",
            m_native.sequence,m_native.outgoing,int(m_automatic.phase),m_automatic.selected,m_automatic.remaining),true);
}

void CTriggerbot::Run(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd)
{
    NamedPlayerTraceDiagnostics::Tick(local,cmd);
    m_triggerAllowed=false;m_shotCommand=-1;m_shotCommitted=false;m_shotAutomatic=false;
    if(RunAutomatic(local,weapon,cmd))return;
    if (!cmd) {Reset();Diagnostic("no_command",local,weapon,cmd);return;}
    if (cmd->command_number<=0 || int64_t(cmd->command_number)!=int64_t(m_lastCommand)+1) m_timer.Reset();
    m_lastCommand=cmd->command_number;
    const char* gate=cmd->command_number<=0?"invalid_command":!local?"no_local":!weapon?"no_weapon"
        :!local->IsAlive()?"local_dead":local->IsAGhost()?"local_ghost":!local->CanAttack()?"local_cannot_attack"
        :I::EngineVGui->IsGameUIVisible()?"game_ui":F::Menu.m_bIsOpen?"menu":!SDK::IsGameWindowInFocus()?"unfocused"
        :!Vars::Triggerbot::Enabled.Value?"disabled":!Vars::Triggerbot::Activation.Value?"activation_inactive"
        :!(Vars::Triggerbot::Hitboxes.Value&31)?"hitboxes_off":G::PrimaryWeaponType!=EWeaponType::HITSCAN?"not_hitscan"
        :cmd->weaponselect?"weapon_selection":(cmd->buttons&IN_ATTACK)?"primary_already_requested"
        :(cmd->buttons&IN_USE)?"using_action":(G::OriginalCmd.buttons&IN_ATTACK)?"manual_primary"
        :(G::OriginalCmd.buttons&IN_USE)?"manual_use":nullptr;
    if(gate){m_timer.Reset();Diagnostic(gate,local,weapon,cmd);return;}
    const uint32_t owner=uint32_t(local->GetRefEHandle().ToInt());
    const int spawn=local->m_iSpawnCounter(),server=I::ClientState->m_nServerCount;
    if (m_owner!=owner || m_spawn!=spawn || m_server!=server) {m_timer.Reset();m_shotCommand=-1;}
    m_owner=owner;m_spawn=spawn;m_server=server;
    const int id=weapon->GetWeaponID();
    if (id==TF_WEAPON_MEDIGUN || id==TF_WEAPON_LASER_POINTER || id==TF_WEAPON_SNIPERRIFLE_CLASSIC)
    {m_timer.Reset();Diagnostic("weapon_excluded",local,weapon,cmd);return;}
    if(SDK::AttribHookValue(1,"mult_dmg",weapon)<=0.f)
    {m_timer.Reset();Diagnostic("weapon_no_damage",local,weapon,cmd);return;}

    if (m_hitboxes!=Vars::Triggerbot::Hitboxes.Value || m_history!=Vars::Triggerbot::Backtrack.Value
        || m_fixed!=Vars::Triggerbot::Delay.Value || m_lower!=Vars::Triggerbot::DelayMin.Value
        || m_upper!=Vars::Triggerbot::DelayMax.Value || m_dynamic!=Vars::Triggerbot::DynamicDelay.Value
        || m_disableDelay!=Vars::Triggerbot::DisableDelay.Value)
        m_timer.Reset();
    m_hitboxes=Vars::Triggerbot::Hitboxes.Value;m_history=Vars::Triggerbot::Backtrack.Value;
    m_fixed=Vars::Triggerbot::Delay.Value;m_lower=Vars::Triggerbot::DelayMin.Value;
    m_upper=Vars::Triggerbot::DelayMax.Value;m_dynamic=Vars::Triggerbot::DynamicDelay.Value;
    m_disableDelay=Vars::Triggerbot::DisableDelay.Value;
    const double now=double(GetTickCount64())*.001;
    bool ready=G::CanPrimaryAttack && weapon->HasPrimaryAmmoForShot();
    const char* readiness=G::CanPrimaryAttack?"primary_ammo":"primary_cooldown";
    if (SDK::AttribHookValue(0,"sniper_only_fire_zoomed",weapon) && !local->InCond(TF_COND_ZOOMED))
        ready=false,readiness="scope_required";
    if (id==TF_WEAPON_MINIGUN)
    {
        const int state=weapon->As<CTFMinigun>()->m_iWeaponState();
        if(state!=AC_STATE_FIRING && state!=AC_STATE_SPINNING) ready=false,readiness="minigun_not_spinning";
    }
    if (!ready) {m_timer.Reset();Diagnostic(readiness,local,weapon,cmd);return;}
    m_triggerAllowed=true;
    // Use the sampled command view, not a render-frame cursor or swept angle.
    // FindCursorHit validates actual hitboxes and world/actor occlusion afresh.
    const auto hit=F::Backtrack.FindCursorHit(local,weapon,cmd->viewangles,m_hitboxes,m_history,true,true,cmd);
    const int target=hit.player?hit.player->GetRefEHandle().ToInt():-1, weaponHandle=weapon->GetRefEHandle().ToInt();
    if (m_timer.active && (m_timer.weapon!=weaponHandle
        || now<m_timer.last || now-m_timer.last>.25)) m_timer.Reset();
    double chosen=m_timer.delay;
    if (!m_timer.active)
    {
        if (!hit.player) {Diagnostic(hit.rejection,local,weapon,cmd,false,nullptr,hit.rejectedEntity,hit.rejectedHitbox);return;}
        // Independent RNG: never disturb the game's spread/crit random stream.
        static std::mt19937 generator(unsigned(GetTickCount64())^GetCurrentProcessId());
        const auto delay=TriggerPolicy::Delay(m_dynamic,m_fixed,m_lower,m_upper,
            m_disableDelay?0.:std::uniform_real_distribution<double>(0.,1.)(generator),m_disableDelay);
        if (!delay) {m_timer.Reset();Diagnostic("invalid_delay",local,weapon,cmd);return;}
        chosen=*delay;
        if (SelfDamageDiagnostics::TriggerEnabled()) SelfDamageDiagnostics::WriteTrigger("trigger_arm",std::format(
            "cmd={} target={} hitbox={} delay={} dynamic={} historical={} history_mode={}",
            cmd->command_number,hit.player->entindex(),hit.hitbox,chosen,m_dynamic,hit.record!=nullptr,m_history),true);
        m_timer.Update(target,weaponHandle,now,chosen,ready);
    }
    if (!m_timer.Poll(weaponHandle,now,ready))
    {Diagnostic("delay_wait",local,weapon,cmd,false,nullptr,hit.player?hit.player->entindex():-1,hit.hitbox,hit.record!=nullptr);return;}
    if(m_lastShot==cmd->command_number) {Diagnostic("duplicate_command",local,weapon,cmd);return;}

    // A positive delay is an intentional commitment: fire along the current
    // view at expiry even after leaving/changing targets. Immediate shots still
    // require this command's fresh acquisition; swept-only hits never arm it.
    m_shotCommand=cmd->command_number;m_shotTarget=target;m_shotWeapon=weaponHandle;
    m_shotCommitted=m_timer.delay>0.;
    m_beforeShotTick=cmd->tick_count;
    cmd->buttons|=IN_ATTACK;
    G::Attacking=SDK::IsAttacking(local,weapon,cmd,true);
    if (hit.player && hit.record)
    {
        cmd->tick_count=TIME_TO_TICKS(hit.record->m_flSimTime)+TIME_TO_TICKS(F::Backtrack.GetFakeInterp());
        F::Backtrack.ReportSelection(hit.record,cmd,hit.player->entindex());
    }
    m_shotTick=cmd->tick_count;
    m_lastShot=cmd->command_number;
    Diagnostic(m_shotCommitted?"shot_committed":"shot_immediate",local,weapon,cmd,false,nullptr,
        hit.player?hit.player->entindex():-1,hit.hitbox,hit.record!=nullptr);
    if (SelfDamageDiagnostics::TriggerEnabled()) SelfDamageDiagnostics::WriteTrigger("trigger_shot",std::format(
        "cmd={} target={} hitbox={} delay={} elapsed={} historical={} history_mode={} command_tick={} angles_unchanged=1 shot_attempt_not_confirmation=1",
        cmd->command_number,hit.player?hit.player->entindex():-1,hit.hitbox,m_timer.delay,now-m_timer.since,hit.record!=nullptr,m_history,cmd->tick_count));
    m_timer.Reset();
}

void CTriggerbot::Diagnostic(const char* reason,CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd,
    bool finalStage,const Vec3* ray,int target,int hitbox,bool historical)
{
    if(!SelfDamageDiagnostics::TriggerEnabled()) return;
    const auto now=GetTickCount64();
    auto& last=m_diagnosticAt[finalStage?1:0];
    // At most four reports per second per stage. Do no formatting or additional
    // tracing unless a report is due; reuse existing bounded/rotating logging.
    if(last && now>=last && now-last<250) return;
    last=now;
    const Vec3 angles=ray?*ray:cmd?cmd->viewangles:Vec3{};
    const Vec3 punch=local?local->m_vecPunchAngle():Vec3{};
    const auto& activationMap=Vars::Triggerbot::Activation.Map;
    const auto base=activationMap.find(DEFAULT_BIND);
    std::string activationBinds;
    size_t bindingCount=0;
    // Observe existing bind storage and the Windows high bit without updating
    // either. Bound the payload, and only sample at the diagnostic rate above.
    for(const auto& [id,value]:activationMap)
    {
        if(id==DEFAULT_BIND) continue;
        if(++bindingCount>4) continue;
        if(id<0 || id>=int(F::Binds.m_vBinds.size()))
        {activationBinds+=std::format("[id={} invalid=1 value={}]",id,value);continue;}
        const auto& bind=F::Binds.m_vBinds[id];
        const bool rawDown=bind.m_iType==BindEnum::Key && bind.m_iKey>0 && bind.m_iKey<256
            && (GetAsyncKeyState(bind.m_iKey)&0x8000)!=0;
        activationBinds+=std::format("[id={} value={} type={} mode={} key={} enabled={} invert={} active={} stored_down={} raw_down={} parent={}]",
            id,value,bind.m_iType,bind.m_iInfo,bind.m_iKey,bind.m_bEnabled,bind.m_bNot,bind.m_bActive,
            bind.m_tKeyStorage.m_bIsDown,rawDown,bind.m_iParent);
    }
    SelfDamageDiagnostics::WriteTrigger("trigger_state",std::format(
        "stage={} reason={} cmd={} enabled={} activation={} hitboxes={} history={} dynamic={} fixed={} lower={} upper={} disable_delay={} pending={} sampled_delay={} committed={} aim_type={} autoshoot={} assisted={} weapon={} ready={} buttons={} raw={} target={} hitbox={} historical={} ray={},{},{} punch={},{},{} activation_base={} activation_bind_count={} activation_binds={} freelook={} automatic_phase={} automatic_selected={} automatic_remaining={} automatic_pending={} diagnostics_global={}",
        finalStage?"final":"run",reason,cmd?cmd->command_number:-1,Vars::Triggerbot::Enabled.Value,
        Vars::Triggerbot::Activation.Value,Vars::Triggerbot::Hitboxes.Value,Vars::Triggerbot::Backtrack.Value,
        Vars::Triggerbot::DynamicDelay.Value,Vars::Triggerbot::Delay.Value,Vars::Triggerbot::DelayMin.Value,
        Vars::Triggerbot::DelayMax.Value,Vars::Triggerbot::DisableDelay.Value,m_timer.active,m_timer.delay,
        m_shotCommitted,Vars::Aimbot::General::AimType.Value,Vars::Aimbot::General::AutoShoot.Value,
        F::Aimbot.m_bHitscanAssisted,weapon?weapon->GetWeaponID():-1,G::CanPrimaryAttack,
        cmd?cmd->buttons:0,G::OriginalCmd.buttons,target,hitbox,historical,
        angles.x,angles.y,angles.z,punch.x,punch.y,punch.z,base==activationMap.end()?"missing":base->second?"true":"false",
        bindingCount,activationBinds,Vars::Misc::Freelook::Enabled.Value,int(m_automatic.phase),m_automatic.selected,m_automatic.remaining,m_automatic.Pending(),Vars::Triggerbot::Diagnostics.Value));
}

void CTriggerbot::Finalize(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd,const Vec3& shotAngles,bool compatibilityAddedAttack,bool attackBeforeCompatibility)
{
    if (!cmd) {m_priorCompatibilityTrigger=false;return;}
    const bool owned=cmd->command_number==m_shotCommand
        || (m_shotAutomatic && m_automaticInput==G::OriginalCmd.command_number);
    // Compatibility stores consecutive calls, including calls across command or
    // entity transitions. Keep provenance for one call, never firing authority.
    const bool retained=compatibilityAddedAttack && m_priorCompatibilityTrigger;
    m_priorCompatibilityTrigger=owned && attackBeforeCompatibility;
    if ((!owned && !retained) || !(cmd->buttons&IN_ATTACK)
        || (G::OriginalCmd.buttons&IN_ATTACK)) return;
    if(m_shotAutomatic && owned && compatibilityAddedAttack && !attackBeforeCompatibility)
    {
        // A later feature vetoed this request; compatibility cannot authorize it anew.
        cmd->buttons&=~IN_ATTACK;m_automatic.Cancel();m_shotAutomatic=false;
        G::Attacking=local&&weapon?SDK::IsAttacking(local,weapon,cmd,true):0;
        Diagnostic("automatic_late_veto",local,weapon,cmd,true,&shotAngles);
        return;
    }
    if (!owned)
    {
        // A previous trigger attempt cannot authorize a new command or skip a
        // newly armed delay, even if compatibility retained its attack button.
        cmd->buttons&=~IN_ATTACK;
        G::Attacking=local && weapon?SDK::IsAttacking(local,weapon,cmd,true):0;
        Diagnostic("carryover_rejected",local,weapon,cmd,true,&shotAngles);
        return;
    }
    if (!local || !weapon || uint32_t(local->GetRefEHandle().ToInt())!=m_owner
        || weapon->GetRefEHandle().ToInt()!=m_shotWeapon || local->m_iSpawnCounter()!=m_spawn
        || I::ClientState->m_nServerCount!=m_server)
    {Diagnostic("final_identity_changed",local,weapon,cmd,true,&shotAngles);return;}
    if(m_shotAutomatic)
    {
        const bool valid=m_triggerAllowed && local->IsAlive() && !local->IsAGhost() && local->CanAttack()
            && Vars::Triggerbot::Enabled.Value && !cmd->weaponselect && !(cmd->buttons&IN_USE)
            && m_automatic.CanRequest();
        if(!valid){cmd->buttons&=~IN_ATTACK;G::Attacking=SDK::IsAttacking(local,weapon,cmd,true);m_automatic.Cancel();}
        Diagnostic(valid?"final_automatic":"final_automatic_rejected",local,weapon,cmd,true,&shotAngles);
        return;
    }
    const auto hit=F::Backtrack.FindCursorHit(local,weapon,shotAngles,m_hitboxes,m_history,true,true,cmd);
    if (!m_triggerAllowed || (!m_shotCommitted && (!hit.player || hit.player->GetRefEHandle().ToInt()!=m_shotTarget))
        || !local->IsAlive() || local->IsAGhost() || !local->CanAttack()
        || cmd->weaponselect || (cmd->buttons&IN_USE))
    {
        cmd->buttons&=~IN_ATTACK;
        if (owned && cmd->tick_count==m_shotTick) cmd->tick_count=m_beforeShotTick;
        G::Attacking=SDK::IsAttacking(local,weapon,cmd,true);
        m_timer.Reset();
        Diagnostic(!m_triggerAllowed?"final_not_allowed":!hit.player?hit.rejection
            :hit.player->GetRefEHandle().ToInt()!=m_shotTarget?"final_target_changed":"final_lifecycle",
            local,weapon,cmd,true,&shotAngles,hit.player?hit.player->entindex():-1,hit.hitbox,hit.record!=nullptr);
        return;
    }
    // Publish only the final ray's record. A committed delay may intentionally
    // miss or hit a different target; its stale historical tick must not survive.
    if (hit.record)
    {
        cmd->tick_count=TIME_TO_TICKS(hit.record->m_flSimTime)+TIME_TO_TICKS(F::Backtrack.GetFakeInterp());
        F::Backtrack.ReportSelection(hit.record,cmd,hit.player->entindex());
    }
    else if (owned && cmd->tick_count==m_shotTick) cmd->tick_count=m_beforeShotTick;
    Diagnostic(m_shotCommitted?"final_committed":"final_immediate",local,weapon,cmd,true,&shotAngles,
        hit.player?hit.player->entindex():-1,hit.hitbox,hit.record!=nullptr);
}

void CTriggerbot::CommitAutomatic(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd,int inputSequence)
{
    if(!m_shotAutomatic || !cmd || m_automaticInput!=G::OriginalCmd.command_number
        || !(cmd->buttons&IN_ATTACK) || (G::OriginalCmd.buttons&IN_ATTACK))return;
    if(!local || !weapon || AutomaticContext(local,weapon)!=m_automatic.context
        || !m_triggerAllowed || !m_automatic.Authorize(inputSequence,cmd->command_number))
    {
        cmd->buttons&=~IN_ATTACK;m_automatic.Cancel();
        G::Attacking=local&&weapon?SDK::IsAttacking(local,weapon,cmd,true):0;
    }
}
