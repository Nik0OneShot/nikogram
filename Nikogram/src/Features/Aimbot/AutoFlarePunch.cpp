#include "AutoFlarePunch.h"
#include "AimbotGlobal/AimbotGlobal.h"
#include "SelfDamageDiagnostics.h"
#include "AutoViewmodelSwitch.h"

static double ComboTime() {return double(GetTickCount64())*.001;}
static bool FlameWeapon(CTFWeaponBase* weapon)
{return weapon && (weapon->GetWeaponID()==TF_WEAPON_FLAMETHROWER || weapon->GetWeaponID()==TF_WEAPON_FLAME_BALL);}
static bool ComboFlare(CTFWeaponBase* weapon)
{
    if (!weapon || weapon->GetWeaponID()!=TF_WEAPON_FLAREGUN) return false;
    const auto type=weapon->As<CTFFlareGun>()->GetFlareGunType();
    return type==FLAREGUN_NORMAL || type==FLAREGUN_DETONATE || type==FLAREGUN_SCORCHSHOT;
}

void CAutoFlarePunch::Reset(const char* reason)
{
    if (m_targetIndex>=0 && SelfDamageDiagnostics::Enabled())
        SelfDamageDiagnostics::Write("flare_combo",std::format(
            "phase=cancel reason={} target={} switched={} returning={} waiting={} aim={} can_fire={}",
            reason,m_targetIndex,m_state.switched,m_state.returning,m_state.waiting,
            Vars::Aimbot::General::AimType.Value,G::CanPrimaryAttack));
    m_state.Reset();m_targetIndex=m_originalWeapon=m_flareWeapon=-1;m_nextFlareAttack=0;
    m_validatedCommand=m_validatedTarget=-1;
    m_lastDiagnostic=m_lastFrameDiagnostic=-10;m_lastDiagnosticReason.clear();
}

void CAutoFlarePunch::Diagnostic(const char* reason,CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd)
{
    if (!m_state.active || !SelfDamageDiagnostics::Enabled()) return;
    const double now=ComboTime();
    if (std::string_view(reason)=="frame")
    {
        if (now-m_lastFrameDiagnostic<.5) return;
        m_lastFrameDiagnostic=now;
    }
    else
    {
        if (now-m_lastDiagnostic<.1 || (m_lastDiagnosticReason==reason && now-m_lastDiagnostic<.5)) return;
        m_lastDiagnostic=now;m_lastDiagnosticReason=reason;
    }
    const double serverTime=local?TICKS_TO_TIME(local->m_nTickBase()):0;
    auto flare=local?local->GetWeaponFromSlot(SLOT_SECONDARY):nullptr;
    SelfDamageDiagnostics::Write("flare_combo",std::format(
        "phase=cycle_update reason={} cmd={} target={} age={} switched={} returning={} waiting={} active_weapon={} original_weapon={} flare_weapon={} aim={} option={} autoshoot={} buttons={} selection={} viewmodel_pending={} server_time={} owner_ready_at={} flare_ready_at={} retry_ready_at={}",
        reason,cmd?cmd->command_number:-1,m_targetIndex,now-m_state.started,m_state.switched,m_state.returning,m_state.waiting,
        weapon?weapon->GetRefEHandle().ToInt():-1,m_originalWeapon,m_flareWeapon,Vars::Aimbot::General::AimType.Value,
        Vars::Aimbot::Projectile::AutoFlarePunch.Value,Vars::Aimbot::General::AutoShoot.Value,cmd?cmd->buttons:0,
        cmd?cmd->weaponselect:0,AutoViewmodelSwitch::Pending(),serverTime,local?local->m_flNextAttack():0,
        flare?flare->m_flNextPrimaryAttack():0,m_nextFlareAttack));
}

void CAutoFlarePunch::Abort(const char* reason,double now)
{
    if (m_state.AbortToReturn(now))
    {
        if (SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("flare_combo",
            std::format("phase=abort_return reason={} target={}",reason,m_targetIndex));
        return;
    }
    Reset(reason);
}

void CAutoFlarePunch::Maintain(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd)
{
    if (!m_state.active) return;
    Diagnostic("frame",local,weapon,cmd);
    if (!local || !weapon || !cmd || !local->IsAlive() || local->m_iClass()!=TF_CLASS_PYRO
        || local->GetRefEHandle().ToInt()!=m_state.owner) {Reset("owner_invalid");return;}
    if (G::OriginalCmd.weaponselect) {Reset("manual_weapon_selection");return;}
    const double now=ComboTime();
    if (m_state.returning)
    {
        if (!m_state.IsValid(m_state.target,m_state.owner,now,true,true)) Reset("return_timeout");
        return;
    }
    auto client=I::ClientEntityList->GetClientEntity(m_targetIndex);
    auto entity=client?client->As<CBaseEntity>():nullptr;
    const bool enabled=FlareComboPolicy::Enabled(Vars::Aimbot::Projectile::AutoFlarePunch.Value,
        Vars::Aimbot::General::AimType.Value!=0,Vars::Aimbot::General::AutoShoot.Value,true);
    if (!entity || !entity->IsPlayer() || !m_state.IsValid(entity->GetRefEHandle().ToInt(),m_state.owner,now,
        entity->As<CTFPlayer>()->IsAlive(),enabled) || entity->IsDormant() || entity->m_iTeamNum()==local->m_iTeamNum())
    {Abort("target_disabled_invalid_or_expired",now);return;}
    auto target=entity->As<CTFPlayer>();
    if (!target->InCond(TF_COND_BURNING) && !target->InCond(TF_COND_BURNING_PYRO)
        && (m_state.switched || now-m_state.started>.35)) Abort("target_not_burning",now);
}

void CAutoFlarePunch::Event(IGameEvent* event,CTFPlayer* local)
{
    if (!event || !local || !Vars::Aimbot::Projectile::AutoFlarePunch.Value
        || !Vars::Aimbot::General::AimType.Value || !Vars::Aimbot::General::AutoShoot.Value
        || FNV1A::Hash32(event->GetName())!=FNV1A::Hash32Const("player_hurt")
        || local->m_iClass()!=TF_CLASS_PYRO || !local->IsAlive()
        || I::EngineClient->GetPlayerForUserID(event->GetInt("attacker"))!=local->entindex()
        || event->GetInt("damageamount")<=0 || event->GetInt("health")<=0
        || (event->GetInt("weaponid")!=TF_WEAPON_FLAMETHROWER && event->GetInt("weaponid")!=TF_WEAPON_FLAME_BALL)) return;
    auto active=local->m_hActiveWeapon().Get();
    if (!active || !FlameWeapon(active->As<CTFWeaponBase>()) || ComboTime()-m_lastAttempt<1.) return;
    const int index=I::EngineClient->GetPlayerForUserID(event->GetInt("userid"));
    auto client=I::ClientEntityList->GetClientEntity(index);
    auto entity=client?client->As<CBaseEntity>():nullptr;
    if (!entity || !entity->IsPlayer()) return;
    auto target=entity->As<CTFPlayer>();
    if (target==local || target->m_iTeamNum()==local->m_iTeamNum() || !target->IsAlive()) return;
    if (!m_state.Arm(target->GetRefEHandle().ToInt(),local->GetRefEHandle().ToInt(),ComboTime())) return;
    m_targetIndex=index;m_originalWeapon=active->GetRefEHandle().ToInt();m_flareWeapon=-1;
    if (SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("flare_combo","phase=own_flame_damage target="+std::to_string(index));
}

bool CAutoFlarePunch::Return(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd)
{
    const bool enabled=FlareComboPolicy::Enabled(Vars::Aimbot::Projectile::AutoFlarePunch.Value,
        Vars::Aimbot::General::AimType.Value!=0,Vars::Aimbot::General::AutoShoot.Value,local->m_iClass()==TF_CLASS_PYRO);
    const double now=ComboTime();
    const double serverTime=TICKS_TO_TIME(local->m_nTickBase());
    const int active=weapon->GetRefEHandle().ToInt();
    // Return independently of target survival: a successful flare may kill/extinguish
    // the target, but that must not strand the player on the secondary weapon.
    if (m_state.returning)
    {
        // Restoring our automatic switch is cleanup, not a new aim action.
        if (!m_state.IsValid(m_state.target,local->GetRefEHandle().ToInt(),now,local->IsAlive(),local->m_iClass()==TF_CLASS_PYRO)
            || G::OriginalCmd.weaponselect || cmd->weaponselect) {Reset("return_disabled_expired_or_manual");return false;}
        auto original=local->GetWeaponFromSlot(SLOT_PRIMARY);
        if (!FlameWeapon(original) || original->GetRefEHandle().ToInt()!=m_originalWeapon) {Reset("original_weapon_changed");return false;}
        if (active==m_originalWeapon)
        {
            m_state.Returned(now);
            if (SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("flare_combo","phase=returned_waiting_reset");
            if (!enabled) Reset("returned_feature_inactive");
            return false;
        }
        if (active!=m_flareWeapon) {Reset("unexpected_return_weapon");return false;}
        cmd->buttons&=~IN_ATTACK;
        if (!(cmd->buttons&IN_ATTACK2) && !AutoViewmodelSwitch::Pending()
            && FlareComboPolicy::Ready(serverTime,0,local->m_flNextAttack(),0))
        {
            cmd->weaponselect=original->entindex();
            if (SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("flare_combo","phase=return_requested");
        }
        else Diagnostic("return_wait",local,weapon,cmd);
        return true;
    }
    return false;
}

bool CAutoFlarePunch::Run(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd)
{
    // Also validate here for direct callers; maintenance never selects or fires.
    Maintain(local,weapon,cmd);
    if (!local || !weapon || !cmd || !m_state.active) return false;
    if (m_state.returning) return Return(local,weapon,cmd);
    auto client=I::ClientEntityList->GetClientEntity(m_targetIndex);
    auto entity=client?client->As<CBaseEntity>():nullptr;
    if (!entity || !entity->IsPlayer()) return false;
    const double now=ComboTime();
    const double serverTime=TICKS_TO_TIME(local->m_nTickBase());
    const int active=weapon->GetRefEHandle().ToInt();
    auto target=entity->As<CTFPlayer>();
    if (!target->InCond(TF_COND_BURNING) && !target->InCond(TF_COND_BURNING_PYRO))
    {
        Diagnostic("burn_condition_replication_wait",local,weapon,cmd);
        return false;
    }
    // Never switch away on a command already reserved for airblast/detonation.
    if (!m_state.switched && (cmd->buttons&IN_ATTACK2)) {Diagnostic("secondary_command_reserved",local,weapon,cmd);return false;}
    if (m_state.switched)
    {
        if (active==m_flareWeapon) {Diagnostic("await_flare_solution",local,weapon,cmd);return false;}
        if (active!=m_originalWeapon) {Reset("unexpected_switch_weapon");return false;}
        cmd->buttons&=~IN_ATTACK;
        Diagnostic("await_flare_equip",local,weapon,cmd);
        return true; // Await the requested switch; never fire another weapon as the combo.
    }
    if (cmd->weaponselect || AutoViewmodelSwitch::Pending()) {Diagnostic("command_or_viewmodel_wait",local,weapon,cmd);return false;}
    if (active!=m_originalWeapon || !FlameWeapon(weapon))
    {Reset("original_weapon_or_viewmodel_pending");return false;}
    auto flare=local->GetWeaponFromSlot(SLOT_SECONDARY);
    if (!ComboFlare(flare) || !flare->HasPrimaryAmmoForShot()
        || F::AimbotGlobal.ShouldIgnore(target,local,flare)
        || Math::CalcFov(I::EngineClient->GetViewAngles(),Math::CalcAngle(local->GetShootPos(),target->GetCenter()))>=Vars::Aimbot::Projectile::AimFOV.Value
        || !SDK::VisPos(local,target,local->GetShootPos(),target->GetCenter()))
    {Reset("flare_ammo_target_filter_fov_or_visibility");return false;}
    if (m_state.waiting && flare->GetRefEHandle().ToInt()!=m_flareWeapon) {Reset("flare_weapon_changed");return false;}
    // The holstered secondary's actual attack timer (plus this attempt's fire
    // interval) must be ready before leaving the flamethrower again.
    if (!FlareComboPolicy::Ready(serverTime,flare->m_flNextPrimaryAttack(),local->m_flNextAttack(),m_nextFlareAttack))
    {Diagnostic("flare_cooldown_wait",local,weapon,cmd);return false;}
    if (m_state.waiting)
    {
        m_state.Retry(now);
        if (SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("flare_combo","phase=reset_ready_retry");
    }
    m_flareWeapon=flare->GetRefEHandle().ToInt();m_state.switched=true;
    cmd->weaponselect=flare->entindex();cmd->buttons&=~IN_ATTACK;
    if (SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("flare_combo",std::format("phase=switch_requested target={} weapon={}",m_targetIndex,flare->entindex()));
    return true;
}

void CAutoFlarePunch::Validated(CTFWeaponBase* weapon,CUserCmd* cmd,int target)
{
    if (!weapon || !cmd || Target()<0 || target!=m_targetIndex || weapon->GetRefEHandle().ToInt()!=m_flareWeapon) return;
    m_validatedCommand=cmd->command_number;m_validatedTarget=target;
}

void CAutoFlarePunch::Shot(CTFWeaponBase* weapon,CUserCmd* cmd)
{
    if (!weapon || !cmd || Target()<0 || weapon->GetRefEHandle().ToInt()!=m_flareWeapon
        || G::Attacking!=1 || !(cmd->buttons&IN_ATTACK)
        || !FlareComboPolicy::ShotMatches(m_validatedCommand,cmd->command_number,m_validatedTarget,m_targetIndex)) return;
    if (SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("flare_combo",std::format("phase=validated_shot_attempt target={} cmd={} confirmed_hit=0",m_targetIndex,cmd->command_number));
    m_lastAttempt=ComboTime();
    auto owner=weapon->m_hOwnerEntity().Get();
    if (!owner || !owner->IsPlayer()) {Reset();return;}
    const double serverTime=TICKS_TO_TIME(owner->As<CTFPlayer>()->m_nTickBase());
    const float interval=weapon->GetFireRate();
    if (!std::isfinite(interval) || interval<=0 || !std::isfinite(serverTime)) {Reset();return;}
    m_nextFlareAttack=std::max(double(weapon->m_flNextPrimaryAttack()),serverTime+interval);
    m_state.Shot(m_lastAttempt);
}
