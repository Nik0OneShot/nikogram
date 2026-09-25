#include "AntiAim.h"

#include "../../Ticks/Ticks.h"
#include "../../Players/PlayerUtils.h"
#include "../../Misc/Misc.h"
#include "../../Aimbot/AutoRocketJump/AutoRocketJump.h"
#include "../../AntiCheatCompatibility/AntiCheatCompatibility.h"

bool CAntiAim::AntiAimOn()
{
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
	if (!Vars::AntiAim::HidePitchOnShot.Value || G::Attacking != 1 || G::PrimaryWeaponType != EWeaponType::HITSCAN || pLocal->m_MoveType() != MOVETYPE_WALK)
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
	static std::unordered_map<uint32_t, bool> mJitter = {};

	if (!I::ClientState->chokedcommands)
		mJitter[uHash] = !mJitter[uHash];
	return mJitter[uHash] ? 1 : -1;
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
	case Vars::AntiAim::YawModeEnum::View: return pCmd->viewangles.y + flOffset;
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
		return (flSmallestFovTo == 360.f ? pCmd->viewangles.y + flOffset : flSmallestAngleTo + flOffset);
	}
	}
	return pCmd->viewangles.y;
}

float CAntiAim::GetYaw(CTFPlayer* pLocal, CUserCmd* pCmd, bool bFake)
{
	float flYaw = GetBaseYaw(pLocal, pCmd, bFake) + GetYawOffset(pLocal, bFake);
	return flYaw;
}

float CAntiAim::GetPitch(float flCurPitch)
{
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
	if (!Vars::AntiAim::MinWalk.Value || !YawOn() || !pLocal->m_hGroundEntity() || pLocal->InCond(TF_COND_HALLOWEEN_KART))
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



/*
	The server turns the body (feet yaw) once per usercmd in CMultiPlayerAnimState::ComputePoseParam_AimYaw
	(verified against the x64 server.dll):
	- moving (speed > 1): goal feet = eye yaw
	- standing: goal feet only moves, by 45 degrees, when the eye is more than 45 degrees away from it
	- then ConvergeYawAngles(goal, 720 deg/s, tick interval, feet), scaled down within 60 degrees of the goal
	With 2 real commands and 1 fake per packet, the fake pulls the body back each packet, so it never reaches
	the real yaw. Real compensation picks the yaw sent in each real (choked) command so the predicted body lands
	on the real yaw. Only the last command's angles are networked, so these steering angles aren't seen directly.
*/
static void SimulateFeet(float& flFeet, float& flGoal, float flEyeYaw, bool bMoving)
{
	flEyeYaw = Math::NormalizeAngle(flEyeYaw);
	if (bMoving)
		flGoal = flEyeYaw;
	else
	{
		float flDelta = Math::NormalizeAngle(flGoal - flEyeYaw);
		if (fabsf(flDelta) > 45.f)
			flGoal += flDelta > 0.f ? -45.f : 45.f;
	}
	flGoal = Math::NormalizeAngle(flGoal);
	if (flGoal == flFeet)
		return;

	// ConvergeYawAngles, including the game taking the absolute delta before normalizing it
	float flDelta = flGoal - flFeet;
	const float flDeltaAbs = fabsf(flDelta);
	flDelta = Math::NormalizeAngle(flDelta);
	const float flStep = 720.f * TICK_INTERVAL * std::clamp(flDeltaAbs / 60.f, 0.01f, 1.f);
	if (flDeltaAbs < flStep)
		flFeet = flGoal;
	else
		flFeet += flDelta < 0.f ? -flStep : flStep;
	flFeet = Math::NormalizeAngle(flFeet);
}

float CAntiAim::GetCompensatedYaw(float flTarget, float flFakeYaw, bool bMoving)
{
	// with the fake (nearly) opposite the real, a body sitting exactly on the real is about as close to the fake
	// going either way round, so the fake command pulls it left one packet and right the next (visible shake).
	// aim slightly to the side the body is already on so the fake always pulls it the same way.
	if (fabsf(Math::NormalizeAngle(flTarget - (flFakeYaw + 180.f))) < 20.f)
		flTarget += Math::NormalizeAngle(m_flSimFeetYaw - flTarget) < 0.f ? -2.5f : 2.5f;

	// stay within 45 degrees of the target so the standing rule never pushes the body past it
	float flBestYaw = flTarget, flBestError = FLT_MAX, flBestOffset = 0.f;
	for (float flOffset = -45.f; flOffset <= 45.f; flOffset += 0.5f)
	{
		float flFeet = m_flSimFeetYaw, flGoal = m_flSimGoalFeetYaw;
		SimulateFeet(flFeet, flGoal, flTarget + flOffset, bMoving);
		const float flError = fabsf(Math::NormalizeAngle(flFeet - flTarget));
		if (flError < flBestError - 0.01f || fabsf(flError - flBestError) <= 0.01f && fabsf(flOffset) < fabsf(flBestOffset))
			flBestYaw = flTarget + flOffset, flBestError = flError, flBestOffset = flOffset;
	}
	return Math::NormalizeAngle(flBestYaw);
}

void CAntiAim::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, bool bSendPacket)
{
	G::AntiAim = AntiAimOn() && ShouldRun(pLocal, pWeapon, pCmd);

	int iAntiBackstab = F::Misc.AntiBackstab(pLocal, pCmd, bSendPacket);
	if (!iAntiBackstab)
		FakeShotAngles(pLocal, pWeapon, pCmd);

	if (!G::AntiAim)
	{
		vRealAngles = { pCmd->viewangles.x, pCmd->viewangles.y };
		vFakeAngles = { pCmd->viewangles.x, pCmd->viewangles.y };
		return;
	}

	vEdgeTrace.clear();

	Vec2& vAngles = bSendPacket ? vFakeAngles : vRealAngles;
	vAngles.x = iAntiBackstab != 2 ? GetPitch(pCmd->viewangles.x) : pCmd->viewangles.x;
	vAngles.y = !iAntiBackstab ? GetYaw(pLocal, pCmd, bSendPacket) : pCmd->viewangles.y;

	if (F::AntiCheatCompatibility.Active())
		Math::ClampAngles(vAngles);

	// the yaw actually sent, vAngles keeps the intended real/fake for visuals
	Vec2 vSend = vAngles;
	auto pAnimState = pLocal->m_PlayerAnimState();
	const bool bCompensate = Vars::AntiAim::RealCompensation.Value && !F::AntiCheatCompatibility.Active() && !iAntiBackstab && pAnimState;
	if (bCompensate)
	{
		if (!I::ClientState->chokedcommands) // first command of a packet, local animations are up to date with the server here
			m_flSimFeetYaw = pAnimState->m_flCurrentFeetYaw, m_flSimGoalFeetYaw = pAnimState->m_flGoalFeetYaw;

		// minwalk keeps us counted as moving on the server when there's no other input
		const bool bMoving = pLocal->m_vecVelocity().Length() > 1.f || Vars::AntiAim::MinWalk.Value && pLocal->m_hGroundEntity();
		if (!bSendPacket)
			vSend.y = GetCompensatedYaw(vAngles.y, vFakeAngles.y, bMoving);
		SimulateFeet(m_flSimFeetYaw, m_flSimGoalFeetYaw, vSend.y, bMoving);
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
		}

		for (auto& vPair : vEdgeTrace)
		{
			if (SDK::W2S(vPair.first, vScreen1) && SDK::W2S(vPair.second, vScreen2))
				H::Draw.Line(vScreen1.x, vScreen1.y, vScreen2.x, vScreen2.y, { 255, 255, 255, 255 });
		}
	}
}