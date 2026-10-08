#include "AntiAim.h"

#include "../../Ticks/Ticks.h"
#include "../../Players/PlayerUtils.h"
#include "../../Misc/Misc.h"
#include "../../Aimbot/AutoRocketJump/AutoRocketJump.h"
#include "../../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include "../../Visuals/AnimInterp/AnimInterp.h"
#include "../../ImGui/MenuMode.h"

bool CAntiAim::UsingLegitAA() const
{
	return MenuMode::Active == MenuMode::Moonlit && Vars::AntiAim::LegitEnabled.Value;
}

LegitAAPolicy::Preset CAntiAim::LegitPreset() const
{
	static_assert(TF_CLASS_SCOUT == LegitAAPolicy::Scout && TF_CLASS_SNIPER == LegitAAPolicy::Sniper
		&& TF_CLASS_SOLDIER == LegitAAPolicy::Soldier && TF_CLASS_DEMOMAN == LegitAAPolicy::Demoman
		&& TF_CLASS_MEDIC == LegitAAPolicy::Medic && TF_CLASS_HEAVY == LegitAAPolicy::Heavy
		&& TF_CLASS_PYRO == LegitAAPolicy::Pyro && TF_CLASS_SPY == LegitAAPolicy::Spy
		&& TF_CLASS_ENGINEER == LegitAAPolicy::Engineer);
	auto pLocal = H::Entities.GetLocal();
	auto pWeapon = H::Entities.GetWeapon();
	if (!pLocal || !pWeapon)
		return {};
	using Weapon = LegitAAPolicy::Weapon;
	Weapon held = Weapon::Other;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_MEDIGUN: held = Weapon::Medigun; break;
	case TF_WEAPON_SNIPERRIFLE: case TF_WEAPON_SNIPERRIFLE_DECAP: case TF_WEAPON_SNIPERRIFLE_CLASSIC:
		held = Weapon::SniperRifle; break;
	case TF_WEAPON_SMG: case TF_WEAPON_CHARGED_SMG: held = Weapon::SMG; break;
	case TF_WEAPON_REVOLVER: held = Weapon::Revolver; break;
	case TF_WEAPON_BUILDER: case TF_WEAPON_PDA_SPY_BUILD: held = Weapon::Sapper; break;
	case TF_WEAPON_KNIFE: held = Weapon::Knife; break;
	case TF_WEAPON_PDA_SPY: held = Weapon::DisguiseKit; break;
	default: if (pWeapon->GetSlot() == 2) held = Weapon::Melee; break;
	}
	return LegitAAPolicy::Select(pLocal->m_iClass(), held);
}

bool CAntiAim::UseMinWalk() const
{
	return !UsingLegitAA() && Vars::AntiAim::MinWalk.Value;
}

bool CAntiAim::AntiAimOn()
{
	if (UsingLegitAA())
		return LegitPreset().enabled;
	return Vars::AntiAim::Enabled.Value
		&& (Vars::AntiAim::PitchReal.Value
		|| Vars::AntiAim::PitchFake.Value
		|| Vars::AntiAim::YawReal.Value
		|| Vars::AntiAim::YawFake.Value
		|| Vars::AntiAim::RealYawBase.Value
		|| Vars::AntiAim::FakeYawBase.Value
		|| Vars::AntiAim::RealYawOffset.Value
		|| Vars::AntiAim::FakeYawOffset.Value);
}

bool CAntiAim::YawOn()
{
	if (UsingLegitAA())
		return LegitPreset().enabled;
	return Vars::AntiAim::Enabled.Value
		&& (Vars::AntiAim::YawReal.Value
		|| Vars::AntiAim::YawFake.Value
		|| Vars::AntiAim::RealYawBase.Value
		|| Vars::AntiAim::FakeYawBase.Value
		|| Vars::AntiAim::RealYawOffset.Value
		|| Vars::AntiAim::FakeYawOffset.Value);
}

bool CAntiAim::ShouldRun(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!pLocal->IsAlive() || pLocal->IsAGhost() || pLocal->IsTaunting() || pLocal->m_MoveType() != MOVETYPE_WALK || pLocal->InCond(TF_COND_HALLOWEEN_KART)
		|| G::Attacking == 1 || F::AutoRocketJump.IsRunning() || F::Ticks.m_bDoubletap // this m_bDoubletap check can probably be removed if we fix tickbase correctly
		|| pWeapon && pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka && pCmd->buttons & IN_ATTACK && !(G::LastUserCmd->buttons & IN_ATTACK))
		return false;

	if (pLocal->InCond(TF_COND_SHIELD_CHARGE) || pCmd->buttons & IN_ATTACK2 && pLocal->m_bShieldEquipped() && pLocal->m_flChargeMeter() == 100.f)
		return false;

	return true;
}



void CAntiAim::FakeShotAngles(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (UsingLegitAA() || !Vars::AntiAim::HidePitchOnShot.Value || G::Attacking != 1 || G::PrimaryWeaponType != EWeaponType::HITSCAN || pLocal->m_MoveType() != MOVETYPE_WALK)
		return;

	switch (pWeapon ? pWeapon->GetWeaponID() : 0)
	{
	case TF_WEAPON_MEDIGUN:
	case TF_WEAPON_LASER_POINTER:
		return;
	}

	G::SilentAngles = true;
	if (!Vars::Aimbot::General::NoSpread.Value)
	{	// messes with nospread accuracy
		pCmd->viewangles.x = 180 - pCmd->viewangles.x;
		pCmd->viewangles.y += 180;
	}
	else
		pCmd->viewangles.x += 360 * (vFakeAngles.x < 0 ? -1 : 1);
}

static inline float EdgeDistance(CTFPlayer* pEntity, float flYaw, float flOffset)
{
	Vec3 vForward, vRight; Math::AngleVectors({ 0, flYaw, 0 }, &vForward, &vRight, nullptr);
	Vec3 vCenter = pEntity->GetCenter();

	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter = {};
	SDK::Trace(vCenter, vCenter + vRight * flOffset, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
	F::AntiAim.vEdgeTrace.emplace_back(trace.startpos, trace.endpos);
	SDK::Trace(trace.endpos, trace.endpos + vForward * 300.f, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
	F::AntiAim.vEdgeTrace.emplace_back(trace.startpos, trace.endpos);

	return trace.fraction;
}

static inline int GetEdge(CTFPlayer* pEntity, const float flYaw)
{
	float flSize = pEntity->GetSize().y;
	float flEdgeLeftDist = EdgeDistance(pEntity, flYaw, -flSize);
	float flEdgeRightDist = EdgeDistance(pEntity, flYaw, flSize);

	return flEdgeLeftDist > flEdgeRightDist ? -1 : 1;
}

static inline int GetJitter(uint32_t uHash)
{
	struct Entry { int command=-1;bool side=false; };
	static std::unordered_map<uint32_t, Entry> mJitter;
	auto& entry=mJitter[uHash];
	if(!I::ClientState->chokedcommands&&entry.command!=G::OriginalCmd.command_number)
		entry.side=!entry.side,entry.command=G::OriginalCmd.command_number;
	return entry.side?1:-1;
}

float CAntiAim::GetYawOffset(CTFPlayer* pEntity, bool bFake)
{
	const int iMode = bFake ? Vars::AntiAim::YawFake.Value : Vars::AntiAim::YawReal.Value;
	int iJitter = GetJitter(FNV1A::Hash32Const("Yaw"));

	switch (iMode)
	{
	case Vars::AntiAim::YawEnum::Forward: return 0.f;
	case Vars::AntiAim::YawEnum::Left: return 90.f;
	case Vars::AntiAim::YawEnum::Right: return -90.f;
	case Vars::AntiAim::YawEnum::Backwards: return 180.f;
	case Vars::AntiAim::YawEnum::Edge: return (bFake ? Vars::AntiAim::FakeYawValue.Value : Vars::AntiAim::RealYawValue.Value) * GetEdge(pEntity, I::EngineClient->GetViewAngles().y);
	case Vars::AntiAim::YawEnum::Jitter: return (bFake ? Vars::AntiAim::FakeYawValue.Value : Vars::AntiAim::RealYawValue.Value) * iJitter;
	case Vars::AntiAim::YawEnum::Spin: return Math::NormalizeAngle(fmod(I::GlobalVars->tickcount * Vars::AntiAim::SpinSpeed.Value, 360.f));
	}
	return 0.f;
}

float CAntiAim::GetBaseYaw(CTFPlayer* pLocal, CUserCmd* pCmd, bool bFake)
{
	const int iMode = bFake ? Vars::AntiAim::FakeYawBase.Value : Vars::AntiAim::RealYawBase.Value;
	const float flOffset = bFake ? Vars::AntiAim::FakeYawOffset.Value : Vars::AntiAim::RealYawOffset.Value;
	switch (iMode) // 0 offset, 1 at player
	{
	case Vars::AntiAim::YawModeEnum::View: return G::OriginalCmd.viewangles.y + flOffset;
	case Vars::AntiAim::YawModeEnum::Target:
	{
		float flSmallestAngleTo = 0.f; float flSmallestFovTo = 360.f;
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
		{
			auto pPlayer = pEntity->As<CTFPlayer>();
			if (pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->IsAGhost() || F::PlayerUtils.IsIgnored(pPlayer->entindex()))
				continue;
			
			const Vec3 vAngleTo = Math::CalcAngle(pLocal->m_vecOrigin(), pPlayer->m_vecOrigin());
			const float flFOVTo = Math::CalcFov(I::EngineClient->GetViewAngles(), vAngleTo);

			if (flFOVTo < flSmallestFovTo)
			{
				flSmallestAngleTo = vAngleTo.y;
				flSmallestFovTo = flFOVTo;
			}
		}
		return (flSmallestFovTo == 360.f ? G::OriginalCmd.viewangles.y + flOffset : flSmallestAngleTo + flOffset);
	}
	}
	return G::OriginalCmd.viewangles.y;
}

float CAntiAim::GetYaw(CTFPlayer* pLocal, CUserCmd* pCmd, bool bFake)
{
	if (UsingLegitAA())
	{
		const auto preset = LegitPreset();
		return BodyYawPolicy::Normalize(G::OriginalCmd.viewangles.y + (bFake ? preset.fake : preset.real));
	}
	float flYaw = GetBaseYaw(pLocal, pCmd, bFake) + GetYawOffset(pLocal, bFake);
	return BodyYawPolicy::Normalize(flYaw);
}

float CAntiAim::GetPitch(float flCurPitch)
{
	if (UsingLegitAA())
		return flCurPitch; // Neither real nor fake pitch is modified by this preset.
	float flRealPitch = 0.f, flFakePitch = 0.f;
	int iJitter = GetJitter(FNV1A::Hash32Const("Pitch"));

	switch (Vars::AntiAim::PitchReal.Value)
	{
	case Vars::AntiAim::PitchRealEnum::Up: flRealPitch = -89.f; break;
	case Vars::AntiAim::PitchRealEnum::Down: flRealPitch = 89.f; break;
	case Vars::AntiAim::PitchRealEnum::Zero: flRealPitch = 0.f; break;
	case Vars::AntiAim::PitchRealEnum::Jitter: flRealPitch = -89.f * iJitter; break;
	case Vars::AntiAim::PitchRealEnum::ReverseJitter: flRealPitch = 89.f * iJitter; break;
	}

	switch (Vars::AntiAim::PitchFake.Value)
	{
	case Vars::AntiAim::PitchFakeEnum::Up: flFakePitch = -89.f; break;
	case Vars::AntiAim::PitchFakeEnum::Down: flFakePitch = 89.f; break;
	case Vars::AntiAim::PitchFakeEnum::Jitter: flFakePitch = -89.f * iJitter; break;
	case Vars::AntiAim::PitchFakeEnum::ReverseJitter: flFakePitch = 89.f * iJitter; break;
	}

	if (Vars::AntiAim::PitchReal.Value && Vars::AntiAim::PitchFake.Value)
		return flRealPitch + (flFakePitch > 0.f ? 360 : -360);
	else if (Vars::AntiAim::PitchReal.Value)
		return flRealPitch;
	else if (Vars::AntiAim::PitchFake.Value)
		return flFakePitch;
	else
		return flCurPitch;
}

void CAntiAim::MinWalk(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!UseMinWalk() || !YawOn() || !pLocal->m_hGroundEntity() || pLocal->InCond(TF_COND_HALLOWEEN_KART))
		return;

	if (!pCmd->forwardmove && !pCmd->sidemove && pLocal->m_vecVelocity().Length2D() < 2.f)
	{
		static bool bVar = true;
		float flMove = (pLocal->IsDucking() ? 3 : 1) * ((bVar = !bVar) ? 1 : -1);
		Vec3 vDir = { flMove, flMove, 0 };

		Vec3 vMove = Math::RotatePoint(vDir, {}, { 0, -pCmd->viewangles.y, 0 });
		pCmd->forwardmove = vMove.x * (fmodf(fabsf(pCmd->viewangles.x), 180.f) > 90.f ? -1 : 1);
		pCmd->sidemove = -vMove.y;

		pLocal->m_vecVelocity() = { 1, 1 }; // a bit stupid but it's probably fine
	}
}



int CAntiAim::AntiAimTicks()
{
    auto local=H::Entities.GetLocal();
    if(!local||!local->m_PlayerAnimState()||F::AntiCheatCompatibility.Active())return 2;
    if(I::ClientState->chokedcommands&&m_bBodyValid&&m_pBodyLocal==local&&m_iBatchTicks)return m_iBatchTicks;
    const bool moving=local->m_vecVelocity().Length()>1.f
        || (UseMinWalk()&&local->m_hGroundEntity());
    return BodyYawPolicy::Budget(moving);
}

void CAntiAim::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, bool bSendPacket, bool bPacketControl)
{
	G::AntiAim = bPacketControl && AntiAimOn() && ShouldRun(pLocal, pWeapon, pCmd);

	int iAntiBackstab = F::Misc.AntiBackstab(pLocal, pCmd, bSendPacket);
	if (!iAntiBackstab)
		FakeShotAngles(pLocal, pWeapon, pCmd);

	if (!G::AntiAim)
	{
		m_bBodyValid = false;
		m_iBatchTicks = 0;
		m_tPreviousReal = m_tBatchReal = {};
		vRealAngles = { pCmd->viewangles.x, pCmd->viewangles.y };
		vFakeAngles = { pCmd->viewangles.x, pCmd->viewangles.y };
		return;
	}

	vEdgeTrace.clear();

	const float flPitch = iAntiBackstab != 2 ? GetPitch(pCmd->viewangles.x) : pCmd->viewangles.x;
	// Refresh both intents from the same unmodified view, including camera turns.
	vRealAngles = { flPitch, !iAntiBackstab ? GetYaw(pLocal, pCmd, false) : pCmd->viewangles.y };
	vFakeAngles = { flPitch, !iAntiBackstab ? GetYaw(pLocal, pCmd, true) : pCmd->viewangles.y };

	if (F::AntiCheatCompatibility.Active())
	{
		Math::ClampAngles(vRealAngles);
		Math::ClampAngles(vFakeAngles);
	}

	// Intents are desired directions; steering commands remain internal.
	Vec2 vSend = bSendPacket ? vFakeAngles : vRealAngles;
	auto pAnimState = pLocal->m_PlayerAnimState();
	const bool bControl = YawOn() && !F::AntiCheatCompatibility.Active() && !iAntiBackstab && pAnimState;
	if (bControl)
	{
		const bool bMoving = pLocal->m_vecVelocity().Length() > 1.f || UseMinWalk() && pLocal->m_hGroundEntity();
		const bool bRepeat = m_bBodyValid && m_pBodyLocal == pLocal && m_pBodyModel == pLocal->GetModel()
			&& pCmd->command_number == m_iBodyCommand && I::ClientState->chokedcommands == m_iBodyChoke;
		const bool bRestart = !m_bBodyValid || !bRepeat && (!I::ClientState->chokedcommands
			|| m_pBodyLocal != pLocal || m_pBodyModel != pLocal->GetModel()
			|| pCmd->command_number != m_iBodyCommand + 1);
		if (bRepeat)
		{
			m_tBody = m_tBodyBeforeCommand;
			m_tPreviousReal = m_tPreviousRealBeforeCommand;
			m_tBatchReal = m_tBatchRealBeforeCommand;
		}
		if (bRestart)
		{
			// A normal packet boundary preserves the previous real pose; a new
			// entity/model, disabled controller or command gap must not reuse it.
			if (!m_bBodyValid || m_pBodyLocal != pLocal || m_pBodyModel != pLocal->GetModel()
				|| pCmd->command_number != m_iBodyCommand + 1)
				m_tPreviousReal = {};
			m_tBatchReal = {};
			// AnimInterp restores the native state before CreateMove prediction.
			m_tBody = { pAnimState->m_flCurrentFeetYaw, pAnimState->m_flGoalFeetYaw };
			m_pBodyLocal = pLocal;
			m_pBodyModel = pLocal->GetModel();
			m_iBatchTicks = BodyYawPolicy::Budget(bMoving);
			m_bBodyValid = BodyYawPolicy::Valid(m_tBody);
		}
		if (m_bBodyValid)
		{
			m_tBodyBeforeCommand = m_tBody;
			m_tPreviousRealBeforeCommand = m_tPreviousReal;
			m_tBatchRealBeforeCommand = m_tBatchReal;
			if (!bSendPacket)
				vSend.y = BodyYawPolicy::Choose(m_tBody, vRealAngles.y, vFakeAngles.y, bMoving,
					std::max(1, m_iBatchTicks - I::ClientState->chokedcommands), TICK_INTERVAL, m_tPreviousReal).yaw;
			BodyYawPolicy::Step(m_tBody, vSend.y, bMoving, TICK_INTERVAL);
			if (!bSendPacket)
				m_tBatchReal = { m_tBody.feet, vRealAngles.y, true };
			else
				m_tPreviousReal = m_tBatchReal;
			m_iBodyCommand = pCmd->command_number;
			m_iBodyChoke = I::ClientState->chokedcommands;
		}
	}
	else
	{
		m_bBodyValid = false, m_iBatchTicks = 0;
		m_tPreviousReal = m_tBatchReal = {};
	}

	SDK::FixMovement(pCmd, vSend);
	pCmd->viewangles.x = vSend.x;
	pCmd->viewangles.y = vSend.y;

	MinWalk(pLocal, pCmd);
}

void CAntiAim::Draw(CTFPlayer* pLocal)
{
	if (!pLocal->IsAlive() || pLocal->IsAGhost() || !I::Input->CAM_IsThirdPerson() || !AntiAimOn())
		return;

	if (Vars::AntiAim::AntiAimLines.Value)
	{
		const auto& vOrigin = pLocal->GetAbsOrigin();

		Vec3 vScreen1, vScreen2;
		if (SDK::W2S(vOrigin, vScreen1))
		{
			if (SDK::W2S(vOrigin + Math::RotatePoint({ 50, 0, 0 }, {}, { 0, vRealAngles.y, 0 }), vScreen2))
				H::Draw.Line(vScreen1.x, vScreen1.y, vScreen2.x, vScreen2.y, { 0, 255, 0, 255 });
			if (SDK::W2S(vOrigin + Math::RotatePoint({ 50, 0, 0 }, {}, { 0, vFakeAngles.y, 0 }), vScreen2))
				H::Draw.Line(vScreen1.x, vScreen1.y, vScreen2.x, vScreen2.y, { 255, 0, 0, 255 });
			if (auto pAnimState = pLocal->m_PlayerAnimState())
			{
				const auto& font = H::Fonts.GetFont(FONT_INDICATORS);
				const Color_t bodyColour = { 100, 190, 255, 255 };
				if (SDK::W2S(vOrigin + Math::RotatePoint({ 65, 0, 0 }, {}, { 0, pAnimState->m_flCurrentFeetYaw, 0 }), vScreen2))
					H::Draw.Line(vScreen1.x, vScreen1.y, vScreen2.x, vScreen2.y, bodyColour);
				H::Draw.StringOutlined(font, vScreen1.x, vScreen1.y + font.m_nTall, bodyColour,
					Vars::Menu::Theme::Background.Value, ALIGN_TOPLEFT,
					std::format("AA target {:.1f} | completed body {:.1f} | error {:.1f}", vRealAngles.y,
						pAnimState->m_flCurrentFeetYaw, BodyYawPolicy::Distance(vRealAngles.y, pAnimState->m_flCurrentFeetYaw)).c_str());
				H::Draw.StringOutlined(font, vScreen1.x, vScreen1.y + font.m_nTall * 2, bodyColour,
					Vars::Menu::Theme::Background.Value, ALIGN_TOPLEFT,
					std::format("Fake {:.1f} | completed eye {:.1f} | render body {:.1f}", vFakeAngles.y,
						pAnimState->m_flEyeYaw, pAnimState->m_angRender.y).c_str());
				if (const auto* real = F::AnimInterp.LocalRealFrame(pLocal))
					H::Draw.StringOutlined(font, vScreen1.x, vScreen1.y + font.m_nTall * 3, bodyColour,
						Vars::Menu::Theme::Background.Value, ALIGN_TOPLEFT,
						std::format("Real command pose: eye {:.1f} | body {:.1f}", real->m_vRecordedEyeAngles.y,
							real->m_vRenderAngles.y).c_str());
			}
		}

		for (auto& vPair : vEdgeTrace)
		{
			if (SDK::W2S(vPair.first, vScreen1) && SDK::W2S(vPair.second, vScreen2))
				H::Draw.Line(vScreen1.x, vScreen1.y, vScreen2.x, vScreen2.y, { 255, 255, 255, 255 });
		}
	}
}
