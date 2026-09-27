#include "Ticks.h"
#include "TickIndicatorStyle.h"
#include "../ImGui/Workspace.h"
#include "../ImGui/Menu/Menu.h"
#include "../ImGui/Menu/BindLayout.h"
#include "../ImGui/Notifications/NotificationStyle.h"

#include "../PacketManip/AntiAim/AntiAim.h"
#include "../EnginePrediction/EnginePrediction.h"
#include "../Aimbot/AutoRocketJump/AutoRocketJump.h"
#include "../Backtrack/Backtrack.h"
#include "../AntiCheatCompatibility/AntiCheatCompatibility.h"

void CTicks::Reset()
{
	m_bSpeedhack = m_bDoubletap = m_bRecharge = m_bWarp = false;
	m_iShiftedTicks = m_iShiftedGoal = 0;
}

void CTicks::Recharge(CTFPlayer* pLocal)
{
	if (!m_bGoalReached)
		return;

	bool bPassive = m_bRecharge = false;

	static float flPassiveTime = 0.f;
	flPassiveTime = std::max(flPassiveTime - TICK_INTERVAL, -TICK_INTERVAL);
	if (Vars::Doubletap::PassiveRecharge.Value && 0.f >= flPassiveTime)
	{
		bPassive = true;
		flPassiveTime += 1.f / Vars::Doubletap::PassiveRecharge.Value;
	}

	if (m_iDeficit)
	{
		bPassive = true;
		m_iDeficit--, m_iShiftedTicks--;
	}

	if (!Vars::Doubletap::RechargeTicks.Value && !bPassive
		|| m_bDoubletap || m_bWarp || m_iShiftedTicks == m_iMaxShift || m_bSpeedhack)
		return;

	m_bRecharge = true;
	m_iShiftedGoal = m_iShiftedTicks + 1;
}

void CTicks::Warp()
{
	if (!m_bGoalReached)
		return;

	m_bWarp = false;
	if (!Vars::Doubletap::Warp.Value
		|| !m_iShiftedTicks || m_bDoubletap || m_bRecharge || m_bSpeedhack)
		return;

	m_bWarp = true;
	m_iShiftedGoal = std::max(m_iShiftedTicks - Vars::Doubletap::WarpRate.Value + 1, 0);
}

void CTicks::Doubletap(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!m_bGoalReached)
		return;

	if (!Vars::Doubletap::Doubletap.Value
		|| m_iWait || m_bWarp || m_bRecharge || m_bSpeedhack)
		return;

	int iTicks = std::min(m_iShiftedTicks + 1, 22);
	auto pWeapon = H::Entities.GetWeapon();
	if (!(iTicks >= Vars::Doubletap::TickLimit.Value || pWeapon && GetShotsWithinPacket(pWeapon, iTicks) > 1))
		return;

	bool bAttacking = G::PrimaryWeaponType == EWeaponType::MELEE ? pCmd->buttons & IN_ATTACK : G::Attacking;
	if (!G::CanPrimaryAttack && !G::Reloading || !bAttacking && !m_bDoubletap || F::AutoRocketJump.IsRunning())
		return;

	m_bDoubletap = true;
	m_iShiftedGoal = std::max(m_iShiftedTicks - Vars::Doubletap::TickLimit.Value + 1, 0);
	if (Vars::Doubletap::AntiWarp.Value)
		m_bAntiWarp = pLocal->m_hGroundEntity();
}

void CTicks::Speedhack()
{
	m_bSpeedhack = Vars::Speedhack::Scale.Value != 1;
	if (!m_bSpeedhack)
		return;

	m_bDoubletap = m_bWarp = m_bRecharge = false;
}

static Vec3 s_vVelocity = {};
static int s_iMaxTicks = 0;
void CTicks::AntiWarp(CTFPlayer* pLocal, float flYaw, float& flForwardMove, float& flSideMove, int iTicks)
{
	s_iMaxTicks = std::max(iTicks + 1, s_iMaxTicks);

	Vec3 vAngles; Math::VectorAngles(s_vVelocity, vAngles);
	vAngles.y = flYaw - vAngles.y;
	Vec3 vForward; Math::AngleVectors(vAngles, &vForward);
	vForward *= s_vVelocity.Length2D();

	if (iTicks > std::max(s_iMaxTicks - 8, 3))
		flForwardMove = -vForward.x, flSideMove = -vForward.y;
	else if (iTicks > 3)
		flForwardMove = flSideMove = 0.f;
	else
		flForwardMove = vForward.x, flSideMove = vForward.y;
}
void CTicks::AntiWarp(CTFPlayer* pLocal, float flYaw, float& flForwardMove, float& flSideMove)
{
	AntiWarp(pLocal, flYaw, flForwardMove, flSideMove, GetTicks());
}
void CTicks::AntiWarp(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (m_bAntiWarp)
		AntiWarp(pLocal, pCmd->viewangles.y, pCmd->forwardmove, pCmd->sidemove);
	else
	{
		s_vVelocity = pLocal->m_vecVelocity();
		s_iMaxTicks = 0;
	}
}

bool CTicks::ValidWeapon(CTFWeaponBase* pWeapon)
{
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_PDA:
	case TF_WEAPON_PDA_ENGINEER_BUILD:
	case TF_WEAPON_PDA_ENGINEER_DESTROY:
	case TF_WEAPON_PDA_SPY:
	case TF_WEAPON_PDA_SPY_BUILD:
	case TF_WEAPON_BUILDER:
	case TF_WEAPON_INVIS:
	case TF_WEAPON_GRAPPLINGHOOK:
	case TF_WEAPON_JAR_MILK:
	case TF_WEAPON_LUNCHBOX:
	case TF_WEAPON_BUFF_ITEM:
	case TF_WEAPON_ROCKETPACK:
	case TF_WEAPON_JAR_GAS:
	case TF_WEAPON_LASER_POINTER:
	case TF_WEAPON_MEDIGUN:
	case TF_WEAPON_SNIPERRIFLE:
	case TF_WEAPON_SNIPERRIFLE_DECAP:
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
	case TF_WEAPON_COMPOUND_BOW:
	case TF_WEAPON_JAR:
		return false;
	}

	return true;
}

void CTicks::MoveFunc(float accumulated_extra_samples, bool bFinalTick)
{
	m_iShiftedTicks--;
	if (m_iWait > 0)
		m_iWait--;

	int iTicks = std::min(m_iShiftedTicks + 1, 22);
	auto pWeapon = H::Entities.GetWeapon();
	if (!(iTicks >= Vars::Doubletap::TickLimit.Value || pWeapon && GetShotsWithinPacket(pWeapon, iTicks) > 1))
		m_iWait = -1;

	m_bGoalReached = bFinalTick && m_iShiftedTicks == m_iShiftedGoal;

	static auto CL_Move = U::Hooks.m_mHooks["CL_Move"];
	CL_Move->Call<void>(accumulated_extra_samples, bFinalTick);
}

void CTicks::Move(float accumulated_extra_samples, bool bFinalTick)
{
	MoveManage();

	while (m_iShiftedTicks > m_iMaxShift)
		MoveFunc(accumulated_extra_samples, false);
	m_iShiftedTicks = std::max(m_iShiftedTicks, 0) + 1;

	if (m_bSpeedhack)
	{
		m_iShiftedTicks = Vars::Speedhack::Scale.Value;
		m_iShiftedGoal = 0;
	}

	m_iShiftedGoal = std::clamp(m_iShiftedGoal, 0, m_iMaxShift);
	if (m_iShiftedTicks > m_iShiftedGoal) // normal use/doubletap/teleport
	{
		m_iShiftStart = m_iShiftedTicks - 1;
		m_bShifted = false;

		while (m_iShiftedTicks > m_iShiftedGoal)
		{
			m_bShifting = m_bShifted |= m_iShiftedTicks - 1 != m_iShiftedGoal;
			MoveFunc(accumulated_extra_samples, m_iShiftedTicks - 1 == m_iShiftedGoal);
		}

		m_bShifting = m_bAntiWarp = m_bTimingUnsure = false;
		if (m_bWarp)
			m_iDeficit = 0;

		m_bDoubletap = m_bWarp = false;
	}
	else // else recharge, run once if we have any choked ticks
	{
		if (I::ClientState->chokedcommands)
			MoveFunc(accumulated_extra_samples, bFinalTick);
	}
}

void CTicks::MoveManage()
{
	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return;

	Recharge(pLocal);
	Warp();
	Speedhack();

	if (!m_bRecharge)
		m_iWait = std::max(m_iWait, 0);
	if (auto pWeapon = H::Entities.GetWeapon())
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_PIPEBOMBLAUNCHER:
		case TF_WEAPON_CANNON:
			if (!G::CanSecondaryAttack)
				m_iWait = Vars::Doubletap::TickLimit.Value;
			break;
		default:
			if (!ValidWeapon(pWeapon))
				m_iWait = -1;
			else if (G::Attacking || !G::CanPrimaryAttack && !G::Reloading)
				m_iWait = Vars::Doubletap::TickLimit.Value;
		}
	}
	else
		m_iWait = -1;

	static auto sv_maxusrcmdprocessticks = H::ConVars.FindVar("sv_maxusrcmdprocessticks");
	m_iMaxUsrCmdProcessTicks = sv_maxusrcmdprocessticks->GetInt();
	if (F::AntiCheatCompatibility.Active())
		m_iMaxUsrCmdProcessTicks = std::min(m_iMaxUsrCmdProcessTicks, 8);
	m_iMaxShift = m_iMaxUsrCmdProcessTicks - std::max(m_iMaxUsrCmdProcessTicks - Vars::Doubletap::RechargeLimit.Value, 0) - (F::AntiAim.YawOn() ? F::AntiAim.AntiAimTicks() : 0);
	m_iMaxShift = std::max(m_iMaxShift, 1);
}

void CTicks::CreateMove(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, bool* pSendPacket)
{
	Doubletap(pLocal, pCmd);
	AntiWarp(pLocal, pCmd);
	ManagePacket(pCmd, pSendPacket);

	SaveShootPos(pLocal);
	SaveShootAngle(pCmd, *pSendPacket);

	if (m_bDoubletap && m_iShiftedTicks == m_iShiftStart && pWeapon && pWeapon->IsInReload())
		m_bTimingUnsure = true;
}

void CTicks::ManagePacket(CUserCmd* pCmd, bool* pSendPacket)
{
	if (!m_bDoubletap && !m_bWarp && !m_bSpeedhack)
	{
		static bool bWasSet = false;
		bool bCanChoke = CanChoke(true); // failsafe
		if (G::PSilentAngles && bCanChoke)
			*pSendPacket = false, bWasSet = true;
		else if (bWasSet || !bCanChoke)
			*pSendPacket = true, bWasSet = false;

		bool bShouldShift = m_iShiftedTicks && m_iShiftedTicks + I::ClientState->chokedcommands >= m_iMaxUsrCmdProcessTicks;
		if (!*pSendPacket && bShouldShift)
			m_iShiftedGoal = std::max(m_iShiftedGoal - 1, 0);
	}
	else
	{
		if ((m_bSpeedhack || m_bWarp) && G::Attacking == 1)
		{
			*pSendPacket = true;
			return;
		}

		*pSendPacket = m_iShiftedGoal == m_iShiftedTicks;
		if (I::ClientState->chokedcommands >= 21) // prevent overchoking
			*pSendPacket = true;
	}
}

void CTicks::Start(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	Vec2 vOriginalMove; int iOriginalButtons;
	if (m_bPredictAntiwarp = m_bAntiWarp || GetTicks(H::Entities.GetWeapon()) && Vars::Doubletap::AntiWarp.Value && pLocal->m_hGroundEntity())
	{
		vOriginalMove = { pCmd->forwardmove, pCmd->sidemove };
		iOriginalButtons = pCmd->buttons;

		AntiWarp(pLocal, pCmd->viewangles.y, pCmd->forwardmove, pCmd->sidemove);
	}

	F::EnginePrediction.Start(pLocal, pCmd);

	if (m_bPredictAntiwarp)
	{
		pCmd->forwardmove = vOriginalMove.x, pCmd->sidemove = vOriginalMove.y;
		pCmd->buttons = iOriginalButtons;
	}
}

void CTicks::End(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (m_bPredictAntiwarp && !m_bAntiWarp && !G::Attacking)
	{
		F::EnginePrediction.End(pLocal, pCmd);
		F::EnginePrediction.Start(pLocal, pCmd);
	}
}

bool CTicks::CanChoke(bool bCanShift, int iMaxTicks)
{
	bool bCanChoke = I::ClientState->chokedcommands < 21;
	if (bCanChoke && !bCanShift)
		bCanChoke = m_iShiftedTicks + I::ClientState->chokedcommands < iMaxTicks;
	return bCanChoke;
}
bool CTicks::CanChoke(bool bCanShift)
{
	return CanChoke(bCanShift, m_iMaxUsrCmdProcessTicks);
}

int CTicks::GetTicks(CTFWeaponBase* pWeapon)
{
	if (m_bDoubletap && m_iShiftedGoal < m_iShiftedTicks)
		return m_iShiftedTicks - m_iShiftedGoal;

	if (!Vars::Doubletap::Doubletap.Value
		|| m_iWait || m_bWarp || m_bRecharge || m_bSpeedhack || F::AutoRocketJump.IsRunning())
		return 0;

	int iTicks = std::min(m_iShiftedTicks + 1, 22);
	if (!(iTicks >= Vars::Doubletap::TickLimit.Value || pWeapon && GetShotsWithinPacket(pWeapon, iTicks) > 1))
		return 0;
	
	return std::min(Vars::Doubletap::TickLimit.Value - 1, m_iMaxShift);
}

int CTicks::GetShotsWithinPacket(CTFWeaponBase* pWeapon, int iTicks)
{
	iTicks = std::min(m_iMaxShift + 1, iTicks);

	int iDelay = 1;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_MINIGUN:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_CANNON:
		iDelay = 2;
	}

	return 1 + (iTicks - iDelay) / std::max(std::ceilf(pWeapon->GetFireRate() / TICK_INTERVAL), 1.f);
}

int CTicks::GetMinimumTicksNeeded(CTFWeaponBase* pWeapon)
{
	int iDelay = 1;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_MINIGUN:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_CANNON:
		iDelay = 2;
	}

	return (GetShotsWithinPacket(pWeapon) - 1) * std::ceilf(pWeapon->GetFireRate() / TICK_INTERVAL) + iDelay;
}

void CTicks::SaveShootPos(CTFPlayer* pLocal)
{
	if (m_iShiftedTicks == m_iShiftStart)
		m_vShootPos = pLocal->GetShootPos();
}
Vec3 CTicks::GetShootPos()
{
	return m_vShootPos;
}

void CTicks::SaveShootAngle(CUserCmd* pCmd, bool bSendPacket)
{
	static auto sv_maxusrcmdprocessticks_holdaim = H::ConVars.FindVar("sv_maxusrcmdprocessticks_holdaim");

	if (bSendPacket)
		m_vShootAngle = std::nullopt;
	else if (!m_vShootAngle && G::Attacking == 1 && sv_maxusrcmdprocessticks_holdaim->GetBool())
		m_vShootAngle = pCmd->viewangles;
}
Vec3* CTicks::GetShootAngle()
{
	if (m_vShootAngle && I::ClientState->chokedcommands)
		return &m_vShootAngle.value();
	return nullptr;
}

bool CTicks::IsTimingUnsure()
{	// actually knowing when we'll shoot would be better than this, but this is fine for now
	return m_bTimingUnsure || m_bSpeedhack /*|| m_bWarp*/;
}

void CTicks::Draw(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Ticks) || !pLocal || !pLocal->IsAlive())
		return;

	const int antiAimTicks = F::AntiAim.YawOn() ? F::AntiAim.AntiAimTicks() : 0;
	const int maximum = std::max(m_iMaxUsrCmdProcessTicks - antiAimTicks, 0);
	const int ticks = std::clamp(m_iShiftedTicks + std::max(I::ClientState->chokedcommands - antiAimTicks, 0), 0, maximum);
	const char* status = TickIndicatorStyle::Status(ticks, maximum, m_bSpeedhack, m_bWarp);
	const std::string count = m_bSpeedhack ? std::format("x{}", Vars::Speedhack::Scale.Value) : std::format("{:02} / {:02}", ticks, maximum);
	const char* countLabel = m_bSpeedhack ? "SCALE" : "STORED";
	// Readiness if attack were pressed now; do not require attack to already be held.
	auto weapon = H::Entities.GetWeapon();
	const bool dtReady = weapon && ValidWeapon(weapon) && Vars::Doubletap::Doubletap.Value
		&& !m_iWait && !m_bWarp && !m_bRecharge && !m_bSpeedhack
		&& !F::AutoRocketJump.IsRunning() && GetTicks(weapon) > 0
		&& (G::CanPrimaryAttack || G::Reloading);
	const char* dtStatus = dtReady ? "DT READY" : "DT NOT READY";
	const auto& labelFont = H::Fonts.GetFont(FONT_CRIT_LABEL);
	const auto& detailFont = labelFont;
	const auto& countFont = H::Fonts.GetFont(FONT_CRIT_COUNT);
	const float scale = std::max(.1f, H::Draw.Scale());
	const int padding = std::max(3, int(H::Draw.Scale(6, Scale_Round)));
	const int gap = std::max(2, int(H::Draw.Scale(3, Scale_Round)));
	const int barHeight = std::max(5, int(H::Draw.Scale(10, Scale_Round)));
	int width = int(H::Draw.Scale(196, Scale_Round));
	width = std::max(width, int(H::Draw.GetTextSize("TICKS", labelFont).x + H::Draw.GetTextSize(status, labelFont).x) + padding * 2 + gap * 3);
	width = std::max(width, int(H::Draw.GetTextSize(count.c_str(), countFont).x + H::Draw.GetTextSize(countLabel, labelFont).x) + padding * 2 + gap * 3);
	width = std::max(width, int(H::Draw.GetTextSize(count.c_str(), countFont).x + H::Draw.GetTextSize(dtStatus, detailFont).x) + padding * 2 + gap * 3);
	const int bodyHeight = std::max(countFont.m_nTall, detailFont.m_nTall + gap + labelFont.m_nTall);
	const int height = padding * 2 + labelFont.m_nTall + bodyHeight + gap * 2 + barHeight;
	m_vIndicatorSize = { float(width), float(height) };
	const auto position = Vars::Menu::TicksDisplay.Value;
	const int left = int(BindLayout::ClampAxis(float(position.x - width / 2), float(width), 0.f, float(H::Draw.m_nScreenW)));
	const float taskbar = F::Menu.m_bIsOpen ? H::Draw.Scale(26) : 0.f;
	const int top = int(BindLayout::ClampAxis(float(position.y), float(height), Workspace::TopTaskbar ? taskbar : 0.f,
		H::Draw.m_nScreenH - (Workspace::TopTaskbar ? 0.f : taskbar)));
	const Color_t accent(int(Workspace::Accent[0] * 255), int(Workspace::Accent[1] * 255), int(Workspace::Accent[2] * 255), 255);
	const float ratio = TickIndicatorStyle::Charge(ticks, maximum, m_bSpeedhack);
	const bool full = TickIndicatorStyle::Full(ticks, maximum, m_bSpeedhack);
	const Color_t textColour(int(Workspace::TextChannel(0) * 255), int(Workspace::TextChannel(1) * 255), int(Workspace::TextChannel(2) * 255), 255);
	const Color_t rightColour = textColour.Lerp({ 0, 0, 0, 255 }, 1.f - TickIndicatorStyle::LabelBrightness(ratio));
	// Preserve intentional charge tinting while respecting the user's base text colour.
	Workspace::ScopedTextColour preserveTextColour;
	auto drawText = [&](const Font_t& font, int textX, int textY, EAlign alignment, const char* text, bool right)
	{
		Color_t colour = right ? rightColour : textColour;
		if (full)
		{
			colour = textColour.Lerp({ 255, 255, 255, 255 }, .05f);
			Color_t halo = colour; halo.a = 10;
			const int offset = std::max(1, int(std::round(scale)));
			H::Draw.String(font, textX - offset, textY, halo, alignment, text);
			H::Draw.String(font, textX + offset, textY, halo, alignment, text);
			H::Draw.String(font, textX, textY - offset, halo, alignment, text);
			H::Draw.String(font, textX, textY + offset, halo, alignment, text);
		}
		H::Draw.String(font, textX, textY, colour, alignment, text);
	};
	H::Draw.FillRect(left, top, width, height, { 0, 0, 0, 255 });
	H::Draw.LineRect(left, top, width, height, Color_t(int(Workspace::BorderChannel(0) * 255), int(Workspace::BorderChannel(1) * 255), int(Workspace::BorderChannel(2) * 255), 255));
	int y = top + padding;
	drawText(labelFont, left + padding, y, ALIGN_TOPLEFT, "TICKS", false);
	drawText(labelFont, left + width - padding, y, ALIGN_TOPRIGHT, status, true);
	y += labelFont.m_nTall + gap;
	drawText(countFont, left + padding, y + (bodyHeight - countFont.m_nTall) / 2, ALIGN_TOPLEFT, count.c_str(), false);
	// Keep this independent of reserve glow: a full reserve need not mean usable DT.
	const int storedY = y + bodyHeight - labelFont.m_nTall;
	const int detailY = (top + padding + storedY) / 2;
	H::Draw.String(detailFont, left + width - padding, detailY,
		dtReady ? textColour : textColour.Lerp({ 0, 0, 0, 255 }, .35f), ALIGN_TOPRIGHT, dtStatus);
	drawText(labelFont, left + width - padding, storedY, ALIGN_TOPRIGHT, countLabel, true);
	y += bodyHeight + gap;
	const auto layout = NotificationStyle::Layout(float(width - padding * 2), scale);
	if (full)
	{
		// Keep the halo inside the panel's padding, including at screen/taskbar boundaries.
		for (int ring = 2; ring >= 1; --ring)
		{
			const int spread = std::max(1, int(std::round(ring * scale)));
			Color_t halo = accent; halo.a = ring == 2 ? 10 : 20;
			H::Draw.LineRect(left + padding - spread, y - spread, width - padding * 2 + spread * 2, barHeight + spread * 2, halo);
		}
	}
	const int filled = NotificationStyle::Filled(ratio, layout.count);
	for (int i = 0; i < layout.count; ++i)
	{
		const int cellX = left + padding + int(layout.Left(i));
		const int cellWidth = std::max(1, int(layout.Right(i)) - int(layout.Left(i)));
		if (i < filled)
			H::Draw.GradientRect(cellX, y, cellWidth, barHeight, accent.Lerp({ 255, 255, 255, 255 }, full ? .15f : .10f), accent.Lerp({ 0, 0, 0, 255 }, full ? .35f : .45f), false);
		else
			H::Draw.FillRect(cellX, y, cellWidth, barHeight, { 35, 35, 35, 255 });
	}
}
