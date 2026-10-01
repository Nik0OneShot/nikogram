#include "Triggerbot.h"
#include "../Aimbot/Aimbot.h"
#include "../Aimbot/SelfDamageDiagnostics.h"
#include "../Backtrack/Backtrack.h"
#include "../ImGui/Menu/Menu.h"
#include <random>

void CTriggerbot::Run(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd)
{
    if (!cmd) {Reset();return;}
    if (cmd->command_number<=m_lastCommand) m_timer.Reset();
    m_lastCommand=cmd->command_number;
    if (!local || !weapon || !local->IsAlive() || !local->CanAttack()
        || I::EngineVGui->IsGameUIVisible() || F::Menu.m_bIsOpen
        || !(Vars::Triggerbot::Hitboxes.Value&31) || G::PrimaryWeaponType!=EWeaponType::HITSCAN
        || F::Aimbot.m_bHitscanAssisted || cmd->weaponselect || (cmd->buttons&(IN_ATTACK|IN_USE))
        || (G::OriginalCmd.buttons&(IN_ATTACK|IN_USE)))
    {m_timer.Reset();return;}
    const int id=weapon->GetWeaponID();
    if (id==TF_WEAPON_MEDIGUN || id==TF_WEAPON_LASER_POINTER || id==TF_WEAPON_SNIPERRIFLE_CLASSIC
        || SDK::AttribHookValue(1,"mult_dmg",weapon)<=0.f)
    {m_timer.Reset();return;}

    if (m_hitboxes!=Vars::Triggerbot::Hitboxes.Value || m_history!=Vars::Triggerbot::Backtrack.Value
        || m_fixed!=Vars::Triggerbot::Delay.Value || m_lower!=Vars::Triggerbot::DelayMin.Value
        || m_upper!=Vars::Triggerbot::DelayMax.Value || m_dynamic!=Vars::Triggerbot::DynamicDelay.Value)
        m_timer.Reset();
    m_hitboxes=Vars::Triggerbot::Hitboxes.Value;m_history=Vars::Triggerbot::Backtrack.Value;
    m_fixed=Vars::Triggerbot::Delay.Value;m_lower=Vars::Triggerbot::DelayMin.Value;
    m_upper=Vars::Triggerbot::DelayMax.Value;m_dynamic=Vars::Triggerbot::DynamicDelay.Value;
    const auto hit=F::Backtrack.FindCursorHit(local,weapon,cmd->viewangles,m_hitboxes,m_history,true,true);
    if (!hit.player) {m_timer.Reset();return;}
    const int target=hit.player->GetRefEHandle().ToInt(), weaponHandle=weapon->GetRefEHandle().ToInt();
    const double now=double(GetTickCount64())*.001;
    double chosen=m_timer.delay;
    if (!m_timer.active || m_timer.target!=target || m_timer.weapon!=weaponHandle || now<m_timer.last)
    {
        // Independent RNG: never disturb the game's spread/crit random stream.
        static std::mt19937 generator(unsigned(GetTickCount64())^GetCurrentProcessId());
        const auto delay=TriggerPolicy::Delay(m_dynamic,m_fixed,m_lower,m_upper,
            std::uniform_real_distribution<double>(0.,1.)(generator));
        if (!delay) {m_timer.Reset();return;}
        chosen=*delay;
        if (SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("trigger_arm",std::format(
            "cmd={} target={} hitbox={} delay={} dynamic={} historical={} history_mode={}",
            cmd->command_number,hit.player->entindex(),hit.hitbox,chosen,m_dynamic,hit.record!=nullptr,m_history),true);
    }
    bool ready=G::CanPrimaryAttack && weapon->HasPrimaryAmmoForShot();
    if (SDK::AttribHookValue(0,"sniper_only_fire_zoomed",weapon) && !local->InCond(TF_COND_ZOOMED)) ready=false;
    if (id==TF_WEAPON_MINIGUN)
    {
        const int state=weapon->As<CTFMinigun>()->m_iWeaponState();
        ready=ready && (state==AC_STATE_FIRING || state==AC_STATE_SPINNING);
    }
    if (!m_timer.Update(target,weaponHandle,now,chosen,ready)
        || m_lastShot==cmd->command_number) return;

    // A fresh ray/record check was made above on this exact firing command.
    cmd->buttons|=IN_ATTACK;
    G::Attacking=SDK::IsAttacking(local,weapon,cmd,true);
    if (hit.record)
    {
        cmd->tick_count=TIME_TO_TICKS(hit.record->m_flSimTime)+TIME_TO_TICKS(F::Backtrack.GetFakeInterp());
        F::Backtrack.ReportSelection(hit.record,cmd,hit.player->entindex());
    }
    m_lastShot=cmd->command_number;
    if (SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("trigger_shot",std::format(
        "cmd={} target={} hitbox={} delay={} elapsed={} historical={} history_mode={} command_tick={} angles_unchanged=1 shot_attempt_not_confirmation=1",
        cmd->command_number,hit.player->entindex(),hit.hitbox,m_timer.delay,now-m_timer.since,hit.record!=nullptr,m_history,cmd->tick_count));
    m_timer.Reset();
}
