#include "NoSpreadHitscan.h"
#include "../../ImGui/HudTextStyle.h"
#include "../../ImGui/MoonlitHud.h"

#include "../../Ticks/Ticks.h"
#include "../../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include <regex>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <cstring>

MAKE_SIGNATURE(CTFWeaponBaseGun_GetWeaponSpread, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 4C 63 91", 0x0);

namespace
{
	// How long the prediction survives without a new acknowledgement (paused, alt-tabbed).
	constexpr double AckTimeout = 1.0;
	// A reply's lines arrive in several TextMsg chunks; this long after the last one it is complete.
	constexpr double ReplySettle = 0.25;
	constexpr double EchoDisplayWindow = 15.0;
	constexpr size_t SentWindow = 512;
	constexpr size_t PendingWindow = 64;

	// FX_FireBullets: fixed pellet offsets for 15+ pellet buckshot patterns.
	constexpr std::pair<float, float> aWideLarge[15] = {
		{ 0.f, 0.f }, { -0.5f, 0.f }, { -1.f, 0.f }, { 0.5f, 0.f }, { 1.f, 0.f },
		{ 0.f, 0.5f }, { -0.5f, 0.5f }, { -1.f, 0.5f }, { 0.5f, 0.5f }, { 1.f, 0.5f },
		{ 0.f, -0.5f }, { -0.5f, -0.5f }, { -1.f, -0.5f }, { 0.5f, -0.5f }, { 1.f, -0.5f } };

	Vec3 AngleDelta(const Vec3& a, const Vec3& b)
	{
		Vec3 d = a - b;
		d.y = std::remainderf(d.y, 360.f);
		d.z = 0.f;
		return d;
	}
}

void CNoSpreadHitscan::Reset()
{
	SeedClockPolicy::Options tOptions{};
	if (I::GlobalVars && I::GlobalVars->interval_per_tick > 0.001f && I::GlobalVars->interval_per_tick < 0.1f)
		tOptions.tickMs = double(I::GlobalVars->interval_per_tick) * 1000.0;
	m_Clock = SeedClockPolicy::FrameClock(tOptions);
	m_dSent.clear();
	m_vReplyLines.clear();
	m_bRequestPending = m_bWaitingForReply = false;
	m_iRequestCmd = -1;
	m_dRequestTime = m_dLastChunkTime = m_dLastReplyTime = m_dLastAckTime = m_dLastEchoTime = 0.0;
	m_iSnapshotsThisFrame = 0;
	m_iLastServerTick = -1;
	m_iLastOffset = std::numeric_limits<int>::min();
	m_iEchoesSeen = m_iEchoesHuman = m_iRepliesSeen = m_iRepliesAligned = m_iRejectedReplies = 0;
	m_dLoopbackDeltas.clear();
	m_dLoopbackDeltaMs = 0.2;
	m_tTarget = {};
	m_dPending.clear();
	m_iFinalizedCmd = -1;
	m_iHoldEngineTick = -1;
	m_iTailTick = -1;
	m_dFinalizedMs = 0.0;
	m_iPacketsFinalized = m_iShotsResolved = m_iSeedsChanged = 0;

	m_iSeed = 0;
	m_flMantissaStep = 0.f;
	m_iSeedCommand = -1;
}

bool CNoSpreadHitscan::ShouldRun(CTFWeaponBase* pWeapon)
{
	if (pWeapon
		? G::PrimaryWeaponType != EWeaponType::HITSCAN || pWeapon->GetWeaponSpread() <= 0.f
		: !Vars::Aimbot::General::NoSpread.Value || !I::EngineClient->IsInGame())
		return false;

	if (F::AntiCheatCompatibility.Active())
		return false;

	return true;
}

CNoSpreadHitscan::SeedMode CNoSpreadHitscan::GetSeedMode()
{
	// sv_usercmd_custom_random_seed exists only in server.dll (FCVAR_CHEAT, default 1,
	// not replicated). A remote server runs the default; only a listen server's value
	// is ours to read.
	if (!SDK::IsLoopback()) return SeedMode::Time;
	static auto seed = H::ConVars.FindVar("sv_usercmd_custom_random_seed");
	return !seed ? SeedMode::Unknown : seed->GetBool() ? SeedMode::Time : SeedMode::Command;
}

int CNoSpreadHitscan::RemainingThisTick()
{
	return std::max(F::Ticks.m_iShiftedTicks - F::Ticks.m_iShiftedGoal, 0);
}

int CNoSpreadHitscan::HoldAimTicks()
{
	// sv_maxusrcmdprocessticks_holdaim lives in server.dll (FCVAR_CHEAT, default 1): a
	// remote server runs the default, only a listen server's value is ours to read.
	if (!SDK::IsLoopback()) return 1;
	static auto holdaim = H::ConVars.FindVar("sv_maxusrcmdprocessticks_holdaim");
	return holdaim ? holdaim->GetInt() : 1;
}

bool CNoSpreadHitscan::AcksFresh(double dNow) const
{
	return m_dLastAckTime > 0.0 && dNow - m_dLastAckTime <= AckTimeout;
}

bool CNoSpreadHitscan::Loopback() const
{
	return SDK::IsLoopback();
}

double CNoSpreadHitscan::LoopbackMs() const
{
	return SDK::PlatFloatTime() * 1000.0 + m_dLoopbackDeltaMs;
}

bool CNoSpreadHitscan::StepUsable() const
{
	// On a listen server the shared clock is exact to about 0.1 ms, so steps down to
	// 1/32 ms (the game up for nine minutes) are predictable; the slider is for remote
	// servers, whose frame jitter is what makes small steps hopeless there.
	if (Loopback())
		return SeedClockPolicy::StepMs(LoopbackMs()) >= 1.0 / 32.0;
	const double dStep = m_Clock.HaveBase() ? m_Clock.StepNowMs() : 0.0;
	return dStep > 0.0 && dStep >= double(Vars::Aimbot::General::NoSpreadMinimumStep.Value);
}

// IsFixedWeaponSpreadEnabled: tf_use_fixed_weaponspreads (replicated) or the weapon's
// fixed_shot_pattern attribute, for buckshot weapons firing more than one pellet. The
// competitive match-group rule is not read; casual and community servers are covered.
bool CNoSpreadHitscan::FixedPattern(CTFWeaponBase* pWeapon, int iBulletsPerShot)
{
	if (!pWeapon || iBulletsPerShot <= 1)
		return false;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_SHOTGUN_PRIMARY: case TF_WEAPON_SHOTGUN_SOLDIER: case TF_WEAPON_SHOTGUN_HWG:
	case TF_WEAPON_SHOTGUN_PYRO: case TF_WEAPON_SCATTERGUN: case TF_WEAPON_SENTRY_REVENGE:
	case TF_WEAPON_HANDGUN_SCOUT_PRIMARY: case TF_WEAPON_SODA_POPPER: case TF_WEAPON_PEP_BRAWLER_BLASTER:
	case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
		break;
	default:
		return false;
	}
	static auto tf_use_fixed_weaponspreads = H::ConVars.FindVar("tf_use_fixed_weaponspreads");
	if (tf_use_fixed_weaponspreads && tf_use_fixed_weaponspreads->GetBool())
		return true;
	return SDK::AttribHookValue(0, "fixed_shot_pattern", pWeapon) != 0;
}

void CNoSpreadHitscan::SetTarget(CBaseEntity* pEntity, const matrix3x4* aBones, const Vec3& vAimPoint, int iCommand)
{
	if (!pEntity || !aBones)
		return;
	m_tTarget = { pEntity, pEntity->entindex(), aBones, vAimPoint, iCommand };
}

Vec3 CNoSpreadHitscan::SolvePellet(const Vec3& vWantedBase, float x, float y, float flSpread)
{
	// The pellet is built in the basis of the angles the server fires with, so solve
	// by fixed point; three passes are far below the angle resolution the server keeps.
	Vec3 vBase = vWantedBase;
	for (int iPass = 0; iPass < 3; iPass++)
	{
		Vec3 vForward, vRight, vUp; Math::AngleVectors(vBase, &vForward, &vRight, &vUp);
		Vec3 vPellet = vForward + (vRight * x * flSpread) + (vUp * y * flSpread);
		vPellet.Normalize();
		Vec3 vError = vWantedBase - Math::VectorAngles(vPellet);
		vError.y = std::remainderf(vError.y, 360.f);
		vError.z = 0.f;
		vBase += vError;
		vBase.x = std::clamp(vBase.x, -89.f, 89.f);
		vBase.y = std::remainderf(vBase.y, 360.f);
	}
	return vBase;
}

int CNoSpreadHitscan::CountHits(CTFPlayer* pLocal, CBaseEntity* pTarget, const matrix3x4* aBones, const Vec3& vEye, const Vec3& vBase,
	const std::vector<std::pair<float, float>>& vPellets, float flSpread, float flRange, bool bTrace, std::vector<bool>* pHitMask) const
{
	auto pSet = pTarget->As<CBaseAnimating>()->GetHitboxSet();
	if (!pSet)
		return 0;
	const float flModelScale = pTarget->As<CBaseAnimating>()->m_flModelScale();
	Vec3 vForward, vRight, vUp; Math::AngleVectors(vBase, &vForward, &vRight, &vUp);
	int iHits = 0;
	if (pHitMask) pHitMask->assign(vPellets.size(), false);
	for (size_t i = 0; i < vPellets.size(); i++)
	{
		Vec3 vDir = vForward + (vRight * vPellets[i].first * flSpread) + (vUp * vPellets[i].second * flSpread);
		vDir.Normalize();
		bool bHit = false;
		for (int nHitbox = 0; nHitbox < pSet->numhitboxes && !bHit; nHitbox++)
		{
			auto pBox = pSet->pHitbox(nHitbox);
			if (!pBox || pBox->bone < 0 || pBox->bone >= MAXSTUDIOBONES)
				continue;
			bHit = Math::RayToOBB(vEye, vDir, pBox->bbmin, pBox->bbmax, aBones[pBox->bone], flModelScale);
		}
		// A pellet the world stops before reaching the target does not count.
		if (bHit && bTrace && !SDK::VisPosWorld(pLocal, pTarget, vEye, vEye + vDir * flRange))
			bHit = false;
		if (bHit) { iHits++; if (pHitMask) (*pHitMask)[i] = true; }
	}
	return iHits;
}

std::string CNoSpreadHitscan::GetFormat(int iServerTime)
{
	int iDays = iServerTime / 86400;
	int iHours = iServerTime / 3600 % 24;
	int iMinutes = iServerTime / 60 % 60;
	int iSeconds = iServerTime % 60;

	if (iDays)
		return std::format("{}d {}h", iDays, iHours);
	else if (iHours)
		return std::format("{}h {}m", iHours, iMinutes);
	else
		return std::format("{}m {}s", iMinutes, iSeconds);
}

void CNoSpreadHitscan::Log(const char* sEvent, const std::string& sMessage)
{
	if (!Vars::Aimbot::General::NoSpreadDiagnostics.Value)
		return;
	try
	{
		static std::mutex mutex; std::lock_guard lock(mutex);
		static std::ofstream file; static std::filesystem::path path; static std::uintmax_t bytes = 0; static bool started = false;
		if (path.empty())
		{
			wchar_t executable[MAX_PATH * 4] = {};
			const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH * 4);
			if (!length || length >= MAX_PATH * 4) return;
			path = std::filesystem::path(executable).parent_path() / L"Nikogram" / L"Logs" / L"nospread-seed.log";
			std::error_code ec; std::filesystem::create_directories(path.parent_path(), ec);
			if (ec) return;
		}
		if (!file.is_open())
		{
			file.open(path, std::ios::app);
			std::error_code ec; bytes = std::filesystem::file_size(path, ec); if (ec) bytes = 0;
		}
		if (!file) return;
		if (bytes >= 4 * 1024 * 1024)
		{
			file.close(); std::error_code ec;
			auto old = path; old += L".1";
			std::filesystem::remove(old, ec); ec.clear();
			std::filesystem::rename(path, old, ec); if (ec) return;
			file.open(path, std::ios::app); started = false; bytes = 0;
		}
		if (!started)
		{
			const auto header = std::format("SESSION nospread-seed-v1 build={} {} pid={}\n", __DATE__, __TIME__, GetCurrentProcessId());
			file << header; bytes += header.size() + 1; started = true;
		}
		const double dNow = SDK::PlatFloatTime();
		const auto line = std::format("t={:.3f} tick={} {} {}\n", dNow, I::GlobalVars ? I::GlobalVars->tickcount : 0, sEvent, sMessage);
		file << line; bytes += line.size() + 1;
		// Batch flushes: this runs on the command path.
		static double dLastFlush = 0.0; static int iPending = 0;
		if (++iPending >= 64 || dNow - dLastFlush >= 1.0) { file.flush(); dLastFlush = dNow; iPending = 0; }
	}
	catch (...) {}
}

void CNoSpreadHitscan::FrameStart()
{
	m_iSnapshotsThisFrame = 0;
}

void CNoSpreadHitscan::ObserveNetUpdate()
{
	if (!ShouldRun() || I::EngineClient->IsPlayingDemo())
		return;

	const int iTick = I::ClientState->m_ClockDriftMgr.m_nServerTick;
	const int iAck = I::ClientState->command_ack;
	m_iSnapshotsThisFrame++;

	// A new map or server runs a new tick count: start over.
	if (m_iLastServerTick >= 0 && iTick < m_iLastServerTick - 66)
		Reset();
	m_iLastServerTick = iTick;

	if (iAck <= 0 || iTick <= 0)
		return;

	m_Clock.ObserveAck(iAck, iTick);
	m_dLastAckTime = SDK::PlatFloatTime();
	// The same acknowledgement against the engine tick the packet left in.
	const bool bSendOffsetKnown = m_Clock.SendOffsetKnown();
	const int iLastSendOffset = bSendOffsetKnown ? m_Clock.SendOffset() : 0;
	for (auto it = m_dSent.rbegin(); it != m_dSent.rend(); ++it)
	{
		if (it->cmd > iAck) continue;
		if (it->cmd == iAck && it->sendTick >= 0)
			m_Clock.ObserveSendAck(it->sendTick, iTick);
		break;
	}
	if (m_Clock.SendOffsetKnown() && (!bSendOffsetKnown || m_Clock.SendOffset() != iLastSendOffset))
		Log("send_offset", std::format("ack={} tick={} send_offset={} cmd_offset={}", iAck, iTick, m_Clock.SendOffset(), m_Clock.Offset()));

	const int iOffset = m_Clock.Offset();
	if (iOffset != m_iLastOffset)
	{
		Log("offset", std::format("ack={} tick={} offset={} stable={}", iAck, iTick, iOffset, m_Clock.OffsetStable()));
		m_iLastOffset = iOffset;
	}
}

void CNoSpreadHitscan::ObserveEcho(int iPlayer, int iSeed)
{
	if (!ShouldRun() || I::EngineClient->IsPlayingDemo() || GetSeedMode() != SeedMode::Time)
		return;
	if (iPlayer == I::EngineClient->GetLocalPlayer())
		return;

	m_iEchoesSeen++;
	player_info_t tInfo{};
	if (!I::EngineClient->GetPlayerInfo(iPlayer, &tInfo))
		return;
	// A bot's command is stamped when the bot thinks, later in the frame than the
	// packet read that stamps ours and every human's.
	if (tInfo.fakeplayer || tInfo.ishltv || tInfo.isreplay)
		return;
	// Two snapshots parsed in one client frame: the event could belong to either tick.
	if (m_iSnapshotsThisFrame > 1)
		return;

	const int iTick = I::ClientState->m_ClockDriftMgr.m_nServerTick;
	if (iTick <= 0)
		return;

	m_iEchoesHuman++;
	const auto tBefore = m_Clock.PredictTick(iTick);
	const bool bMatched = m_Clock.ObserveEcho(iTick, iSeed);
	m_dLastEchoTime = SDK::PlatFloatTime();
	Log("echo", std::format("player={} tick={} seed={} predicted={} matched={} votes={}/{} refinement={:.3f}",
		iPlayer, iTick, iSeed & 255, tBefore.valid ? tBefore.seed : -1, bMatched, tBefore.votes, tBefore.voters, m_Clock.RefinementMs()));
}

void CNoSpreadHitscan::NotePacketSent(int iCommand, int iNewCommands)
{
	if (!Vars::Aimbot::General::NoSpread.Value)
		return;

	// The reliable "playerperf" string leaves in the same datagram as this move.
	if (m_bRequestPending)
	{
		m_bRequestPending = false;
		m_bWaitingForReply = true;
		m_iRequestCmd = iCommand;
		m_dRequestTime = SDK::PlatFloatTime();
		m_vReplyLines.clear();
	}

	// This packet's shots are settled.
	while (!m_dPending.empty() && m_dPending.front().cmd <= iCommand)
		m_dPending.pop_front();

	if (!m_dSent.empty() && iCommand <= m_dSent.back().cmd)
		return;
	// The send time is FinalizePacket's, the instant the seed was predicted from; the
	// listen server's read delay is measured from the same point.
	const double dSentMs = m_iFinalizedCmd == iCommand ? m_dFinalizedMs : SDK::PlatFloatTime() * 1000.0;
	m_dSent.push_back({ iCommand, std::max(iNewCommands, 1), dSentMs, m_iEngineTick });

	// How long the rest of a doubletap takes to leave, per command, from the packets of
	// one engine tick (0.7.0.52 log: ~0.16 ms per command after a 22-command packet).
	const int iRemaining = RemainingThisTick();
	if (m_iTailTick == m_iEngineTick && m_iTailFirstRemaining > iRemaining)
	{
		const double dPer = (dSentMs - m_dTailFirstMs) / double(m_iTailFirstRemaining - iRemaining);
		if (dPer > 0.0 && dPer < 5.0)
			m_dTailMsPerCmd = 0.8 * m_dTailMsPerCmd + 0.2 * dPer;
	}
	if (m_iTailTick != m_iEngineTick)
		m_iTailTick = m_iEngineTick, m_dTailFirstMs = dSentMs, m_iTailFirstRemaining = iRemaining;
	while (m_dSent.size() > SentWindow)
		m_dSent.pop_front();
}

void CNoSpreadHitscan::FinalizeReply(double dNow, bool bForce)
{
	if (m_vReplyLines.empty())
		return;
	if (!bForce && dNow - m_dLastChunkTime < ReplySettle)
		return;

	m_iRepliesSeen++;
	const std::vector<SeedClockPolicy::SentPacket> vSent(m_dSent.begin(), m_dSent.end());
	// The newest line is normally the packet before the request. If the request's
	// datagram was lost and resent, or the packet before it was lost, the lines sit one
	// or more packets away: once a line exists, accept the shift that agrees with it.
	std::vector<std::pair<int, double>> vAligned = {};
	int iShift = 0; double dDistance = 0.0;
	const bool bHaveLine = m_Clock.HaveBase() && m_Clock.StampCount() >= 40;
	const std::optional<double> dLine = bHaveLine ? std::optional<double>(m_Clock.InterceptMs()) : std::nullopt;
	double dBest = std::numeric_limits<double>::infinity();
	for (int iCandidate : { 0, 1, -1, 2, -2, 3, -3 })
	{
		auto vCandidate = SeedClockPolicy::AlignReply(vSent, m_iRequestCmd + iCandidate, m_vReplyLines);
		if (vCandidate.empty())
			continue;
		if (!dLine)
		{
			vAligned = std::move(vCandidate); iShift = iCandidate;
			break;
		}
		const auto dIntercept = m_Clock.InterceptOf(vCandidate);
		if (!dIntercept)
			continue;
		const double dError = std::fabs(*dIntercept - *dLine);
		if (dError < dBest)
		{
			dBest = dError; vAligned = std::move(vCandidate); iShift = iCandidate; dDistance = dError;
		}
		if (dError <= 7.5)
			break; // the natural shift is tried first
	}
	if (dLine && dBest > 7.5)
	{
		// Three replies in a row disagreeing with the line means the line is what moved
		// (a server hitch the echoes did not catch): take the natural alignment.
		if (++m_iRejectedReplies >= 3)
		{
			vAligned = SeedClockPolicy::AlignReply(vSent, m_iRequestCmd, m_vReplyLines); iShift = 0; dDistance = dBest;
			Log("reply", std::format("request={} lines={} accepted after {} rejections ({:.1f} ms from the line)", m_iRequestCmd, m_vReplyLines.size(), m_iRejectedReplies, dBest));
			m_iRejectedReplies = 0;
		}
		else
		{
			Log("reply", std::format("request={} lines={} rejected: nearest shift {} is {:.1f} ms from the line", m_iRequestCmd, m_vReplyLines.size(), iShift, dBest));
			vAligned.clear();
		}
	}
	else
		m_iRejectedReplies = 0;
	if (!vAligned.empty())
		m_iRepliesAligned++;
	int iPlaced = 0;
	for (const auto& [iCmd, dMs] : vAligned)
	{
		if (m_Clock.TickForCommand(iCmd))
			iPlaced++;
		m_Clock.ObserveStamp(iCmd, dMs);
		if (Loopback())
		{
			// Printed to 1 ms; the mean over many packets recovers the sub-ms delay.
			for (const auto& tSent : vSent)
				if (tSent.cmd == iCmd && tSent.sentMs > 0.0)
				{
					const double dDelta = dMs - tSent.sentMs;
					if (dDelta > -2.0 && dDelta < 20.0)
					{
						m_dLoopbackDeltas.push_back(dDelta);
						while (m_dLoopbackDeltas.size() > 120) m_dLoopbackDeltas.pop_front();
					}
					break;
				}
		}
	}
	if (Loopback() && m_dLoopbackDeltas.size() >= 10)
	{
		double dSum = 0.0; for (double d : m_dLoopbackDeltas) dSum += d;
		m_dLoopbackDeltaMs = std::clamp(dSum / double(m_dLoopbackDeltas.size()), 0.0, 15.0);
	}
	m_dLastReplyTime = dNow;
	Log("reply", std::format("request={} lines={} aligned={} shift={} distance={:.2f} placed={} stamps={} agree={} intercept={:.3f} uptime={:.3f} step={:.3f}",
		m_iRequestCmd, m_vReplyLines.size(), vAligned.size(), iShift, dDistance, iPlaced, m_Clock.StampCount(), m_Clock.StampAgreement(),
		m_Clock.InterceptMs(), m_Clock.UptimeMs(), m_Clock.StepNowMs()));
	m_vReplyLines.clear();
	m_bWaitingForReply = false;
}

void CNoSpreadHitscan::AskForPlayerPerf()
{
	if (!ShouldRun())
		return Reset();

	if (GetSeedMode() != SeedMode::Time)
	{
		// Command seeds need no clock; keep the seed and the shots waiting for their packet.
		const int iSeed = m_iSeed; auto dPending = std::move(m_dPending);
		Reset(); m_iSeed = iSeed; m_dPending = std::move(dPending);
		return;
	}

	const double dNow = SDK::PlatFloatTime();
	FinalizeReply(dNow, false);
	if (m_bWaitingForReply && dNow - m_dRequestTime > std::max(Vars::Aimbot::General::NoSpreadBackupInterval.Value, 1.f))
	{
		FinalizeReply(dNow, true);
		m_bWaitingForReply = false; // lost or refused: ask again
	}

	if (m_bRequestPending && dNow - m_dRequestTime > 1.0)
		m_bRequestPending = false; // the move it should have left with was never sent
	if (m_bRequestPending || m_bWaitingForReply)
		return;

	static Timer tTimer = {};
	if (tTimer.Run(Vars::Aimbot::General::NoSpreadInterval.Value))
	{
		I::ClientState->SendStringCmd("playerperf");
		m_bRequestPending = true; // bound to its packet in NotePacketSent
		m_dRequestTime = dNow;
	}
}

bool CNoSpreadHitscan::ParsePlayerPerf(const std::string& sMsg)
{
	if (!ShouldRun())
		return false;

	// Second block: ProcessUsercmds stamp, new commands, dropped packets. One line per packet of ours.
	static const std::regex tCmdInfo(R"(^(\d+\.\d+)\s(\d+)\s(\d+)\s*$)");
	// First block: PhysicsSimulate stamp and timings. Only its uptime is used.
	static const std::regex tSimInfo(R"(^(\d+\.\d+)\s\d+\s\d+\s\d+\.\d+\s\d+\.\d+\svel\s\d+\.\d+\s*$)");

	bool bPlayerPerf = false, bFirst = true;
	size_t iStart = 0;
	while (iStart <= sMsg.size())
	{
		size_t iEnd = sMsg.find('\n', iStart);
		if (iEnd == std::string::npos) iEnd = sMsg.size();
		std::string sLine = sMsg.substr(iStart, iEnd - iStart);
		iStart = iEnd + 1;
		while (!sLine.empty() && (sLine.back() == '\r' || sLine.back() == ' ')) sLine.pop_back();
		if (sLine.empty()) continue;

		std::smatch tMatch;
		if (std::regex_match(sLine, tMatch, tCmdInfo))
		{
			if (bFirst) bPlayerPerf = true;
			try
			{
				const double dMs = std::stod(tMatch[1].str()) * 1000.0;
				const int iNumCmds = std::stoi(tMatch[2].str());
				const int iDropped = std::clamp(std::stoi(tMatch[3].str()), 0, 1024);
				if (std::isfinite(dMs) && dMs > 0.0 && m_bWaitingForReply && m_vReplyLines.size() < 256)
				{
					m_vReplyLines.push_back({ dMs, iNumCmds, iDropped });
					m_dLastChunkTime = SDK::PlatFloatTime();
				}
			}
			catch (...) {}
		}
		else if (std::regex_match(sLine, tMatch, tSimInfo))
		{
			if (bFirst) bPlayerPerf = true;
			try
			{
				const double dMs = std::stod(tMatch[1].str()) * 1000.0;
				m_Clock.ObserveUptime(dMs);
			}
			catch (...) {}
		}
		else if (bFirst)
			return false; // not a playerperf chunk
		bFirst = false;
	}
	return bPlayerPerf;
}

void CNoSpreadHitscan::BuildPattern(const PendingShot& tShot, int iSeed, std::vector<BurstPlanPolicy::Pellet>& vOut)
{
	// FX_FireBullets: RandomSeed(seed + pellet) for each pellet.
	vOut.clear();
	for (int iBullet = 0; iBullet < tShot.bullets; iBullet++)
	{
		SDK::RandomSeed(iSeed + iBullet);
		float x = 0.f, y = 0.f;
		if (tShot.fixedPattern)
		{
			const auto& tFixed = aWideLarge[iBullet % 15];
			x = tFixed.first + SDK::RandomFloat(-0.07f, 0.07f);
			y = tFixed.second + SDK::RandomFloat(-0.07f, 0.07f);
		}
		else
		{
			const float flVariance = iBullet == 0 ? tShot.firstVariance : 0.5f;
			if (flVariance != 0.f)
			{
				x = SDK::RandomFloat(-flVariance, flVariance) + SDK::RandomFloat(-flVariance, flVariance);
				y = SDK::RandomFloat(-flVariance, flVariance) + SDK::RandomFloat(-flVariance, flVariance);
			}
		}
		vOut.push_back({ x, y });
	}
}

BurstPlanPolicy::Plan CNoSpreadHitscan::PlanShots(CTFPlayer* pLocal, const std::vector<const PendingShot*>& vShots, const std::vector<int>& vSeeds, Vec3& vView) const
{
	std::vector<BurstPlanPolicy::Shot> vPlan(vShots.size());
	std::vector<std::vector<std::pair<float, float>>> vPairs(vShots.size());
	std::vector<CBaseEntity*> vTargets(vShots.size(), nullptr);
	bool bAnyTarget = false;
	for (size_t k = 0; k < vShots.size(); k++)
	{
		const auto& tShot = *vShots[k];
		BuildPattern(tShot, vSeeds[k], vPlan[k].pellets);
		vPlan[k].exactFirst = tShot.perfectFirst && tShot.firstVariance == 0.f;
		vPlan[k].defaultPellet = BurstPlanPolicy::DefaultPellet(vPlan[k].pellets, vPlan[k].exactFirst);
		for (const auto& p : vPlan[k].pellets) vPairs[k].emplace_back(p.x, p.y);
		// The record's bones were copied; the entity must still be the one they belong to.
		if (tShot.pTarget && !tShot.bones.empty()
			&& static_cast<void*>(I::ClientEntityList->GetClientEntity(tShot.targetIndex)) == static_cast<void*>(tShot.pTarget)
			&& tShot.pTarget->IsPlayer())
		{
			vTargets[k] = tShot.pTarget;
			bAnyTarget = true;
		}
	}

	struct Geometry
	{
		using Angles = Vec3;
		const CNoSpreadHitscan* self;
		CTFPlayer* local;
		const std::vector<const PendingShot*>& shots;
		const std::vector<std::vector<std::pair<float, float>>>& pairs;
		const std::vector<CBaseEntity*>& targets;
		bool anyTarget;

		Vec3 Solve(int s, const BurstPlanPolicy::Pellet& p) const
		{
			const auto& t = *shots[s];
			// The server fires along view + punch; solve in that basis, send the view.
			Vec3 vOut = SolvePellet(t.wantedView + t.punch, p.x, p.y, t.spread) - t.punch;
			Math::ClampAngles(vOut);
			return vOut;
		}
		int Hits(const Vec3& view, int k, bool trace) const
		{
			if (!targets[k]) return 0;
			const auto& t = *shots[k];
			return self->CountHits(local, targets[k], t.Bones(), t.eye, view + t.punch, pairs[k], t.spread, t.range, trace, nullptr);
		}
		int Exact(const Vec3& view, int k) const
		{
			const auto& t = *shots[k];
			Vec3 vWanted; Math::AngleVectors(t.wantedView + t.punch, &vWanted);
			Vec3 vForward, vRight, vUp; Math::AngleVectors(view + t.punch, &vForward, &vRight, &vUp);
			// A tenth of the spread: the same aim point, not merely a near one.
			const float flCos = std::cos(std::max(t.spread * 0.1f, 1e-4f));
			int n = 0;
			for (const auto& [x, y] : pairs[k])
			{
				Vec3 vDir = vForward + (vRight * x * t.spread) + (vUp * y * t.spread);
				vDir.Normalize();
				n += vDir.Dot(vWanted) >= flCos;
			}
			return n;
		}
		bool HasTarget() const { return anyTarget; }
	} tGeometry{ this, pLocal, vShots, vPairs, vTargets, bAnyTarget };

	return BurstPlanPolicy::Choose(vPlan, tGeometry, vView);
}

void CNoSpreadHitscan::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!ShouldRun(pWeapon))
		return;

	const auto eMode = GetSeedMode();
	if (eMode == SeedMode::Unknown)
		return;

	// Provisional seed for the command being built. The packet it leaves in, and so the
	// seed the server stamps, is only known when the packet is written: FinalizePacket
	// settles it then. This one draws our own predicted shot and is the fallback.
	m_iSeedCommand = -1;
	if (eMode == SeedMode::Command)
	{
		m_iSeed = pCmd->random_seed & 255;
		m_flMantissaStep = 0.f;
		m_iSeedCommand = pCmd->command_number;
	}
	else
	{
		const double dNow = SDK::PlatFloatTime();
		FinalizeReply(dNow, false);
		if (Loopback())
		{
			// The listen server reads this packet later in this same host frame and stamps
			// it with the clock we share; the frame model below assumes a dedicated server's
			// steady 15 ms grid, which a server inside our own render loop does not have.
			if (!StepUsable())
				return;
			const double dMs = LoopbackMs();
			m_iSeed = SeedClockPolicy::SeedFromMs(dMs);
			m_flMantissaStep = float(SeedClockPolicy::StepMs(dMs));
			m_iSeedCommand = pCmd->command_number;
			if (G::Attacking == 1)
				Log("shot", std::format("cmd={} loopback ms={:.3f} seed={} step={:.2f} delta={:.3f} samples={} provisional",
					pCmd->command_number, dMs, m_iSeed, m_flMantissaStep, m_dLoopbackDeltaMs, m_dLoopbackDeltas.size()));
		}
		else
		{
			if (!m_Clock.Ready() || !StepUsable() || !AcksFresh(dNow))
				return;

			// The packet is counted in engine ticks (doubletap bursts leave in the tick that
			// makes them; a command the engine chokes for cl_cmdrate leaves in the next one).
			// Before the first send acknowledgement, the older command-number model.
			const int iSendTick = m_iEngineTick + (!G::EngineSendPacket && !F::Ticks.m_bShifting ? 1 : 0);
			auto tPrediction = m_Clock.PredictSend(iSendTick);
			int iPacketCmd = -1;
			if (!tPrediction.valid)
			{
				int iRemaining = std::max(F::Ticks.m_iShiftedTicks - F::Ticks.m_iShiftedGoal, 0);
				if ((F::Ticks.m_bSpeedhack || F::Ticks.m_bWarp) && G::Attacking == 1)
					iRemaining = 0; // ManagePacket sends every attacking command on its own
				iPacketCmd = pCmd->command_number + iRemaining;
				if (!iRemaining && !G::EngineSendPacket)
					iPacketCmd++;
				tPrediction = m_Clock.Predict(iPacketCmd);
			}
			if (!tPrediction.valid)
				return;
			m_iSeed = tPrediction.seed;
			m_flMantissaStep = float(tPrediction.step);
			m_iSeedCommand = pCmd->command_number;
			if (G::Attacking == 1)
				Log("shot", std::format("cmd={} send_tick={} packet={} tick={} ms={:.3f} seed={} step={:.2f} votes={}/{} offset={} send_offset={} provisional",
					pCmd->command_number, iSendTick, iPacketCmd, tPrediction.tick, tPrediction.ms, m_iSeed, tPrediction.step,
					tPrediction.votes, tPrediction.voters, m_Clock.Offset(), m_Clock.SendOffsetKnown() ? m_Clock.SendOffset() : 0));
		}
	}
#ifdef SEEDPRED_DEBUG
	SDK::Output("CNoSpreadHitscan::Run", std::format("{}: {}", SDK::PlatFloatTime(), m_iSeed).c_str(), { 255, 0, 0 });
#endif

	// credits to cathook for average spread stuff
	const float flSpread = pWeapon->GetWeaponSpread();
	const int iBulletsPerShot = pWeapon->GetBulletsPerShot();
	const float flFireRate = std::ceilf(pWeapon->GetFireRate() / TICK_INTERVAL) * TICK_INTERVAL;
	if (iBulletsPerShot < 1)
		return;

	// Fixed pellet patterns (FX_FireBullets): buckshot weapons on servers with
	// tf_use_fixed_weaponspreads, or any weapon with fixed_shot_pattern (Panic Attack).
	// Under 15 pellets the pattern has no random part and its first pellet is dead
	// centre, so there is nothing to correct; 15 and more keep a seeded +-0.07 jitter.
	const bool bFixedPattern = FixedPattern(pWeapon, iBulletsPerShot);
	if (bFixedPattern && iBulletsPerShot < 15)
		return;

	// First-shot accuracy bonus (FX_FireBullets): more than 0.25 s (1.25 s for single
	// bullets) since the last shot and pellet 0 flies exactly where we aim, scaled by
	// mult_spread_scale_first_shot where a weapon has it. Every shotgun shot at its normal
	// fire rate qualifies. This is what the server will do with this command.
	const float flTimeSinceLastShot = I::GlobalVars->curtime - pWeapon->m_flLastFireTime();
	// (A fixed pattern ignores it for its offsets; it only keeps pellet 0 as the intent, as in .36.)
	const bool bPerfectFirst = flTimeSinceLastShot > (iBulletsPerShot > 1 ? 0.25f : 1.25f);

	PendingShot tShot{};
	tShot.cmd = pCmd->command_number;
	tShot.seedRun = m_iSeed;
	tShot.commandSeed = pCmd->random_seed & 255;
	tShot.wantedView = pCmd->viewangles;
	tShot.punch = pLocal->m_vecPunchAngle();
	tShot.eye = pLocal->GetShootPos();
	tShot.spread = flSpread;
	tShot.bullets = iBulletsPerShot;
	tShot.perfectFirst = bPerfectFirst;
	tShot.firstVariance = bPerfectFirst ? SDK::AttribHookValue(0.f, "mult_spread_scale_first_shot", pWeapon) : 0.5f;
	tShot.fixedPattern = bFixedPattern;
	tShot.range = pWeapon->GetRange();
	tShot.fireTicks = std::max(TIME_TO_TICKS(flFireRate), 1);
	// Pattern placement target: the hitscan aimbot's record for this command. The bones
	// are copied, the packet may be written after the record has moved on.
	if (m_tTarget.iCommand == pCmd->command_number && m_tTarget.pEntity && m_tTarget.aBones
		&& static_cast<void*>(I::ClientEntityList->GetClientEntity(m_tTarget.iEntityIndex)) == static_cast<void*>(m_tTarget.pEntity)
		&& m_tTarget.pEntity->IsPlayer())
	{
		tShot.pTarget = m_tTarget.pEntity;
		tShot.targetIndex = m_tTarget.iEntityIndex;
		tShot.bones.resize(MAXSTUDIOBONES);
		std::memcpy(tShot.bones.data(), m_tTarget.aBones, sizeof(matrix3x4) * MAXSTUDIOBONES);
		// World check stops just short of the aim point. It used to run 64 u past it, so a
		// target standing in front of a wall had every pellet "blocked" (0.7.0.50 log:
		// hits=0 against default_hits=1 for shots that landed).
		tShot.range = std::clamp(tShot.eye.DistTo(m_tTarget.vAimPoint) - 12.f, 1.f, tShot.range);
	}

	// Provisional placement, this shot alone. A doubletap about to fire several shots in
	// one packet keeps the old rule (prioritize the later bullets: pellet 0 treated as
	// random); FinalizePacket plans the whole packet with the true first-shot flags.
	PendingShot tProvisional = tShot;
	const int iTicks = F::Ticks.GetTicks();
	if (iTicks && iTicks >= TIME_TO_TICKS(flFireRate) * 2)
		tProvisional.perfectFirst = false, tProvisional.firstVariance = 0.5f;

	auto fRecord = [&](const Vec3& vView)
	{
		tShot.viewRun = vView;
		if (!m_dPending.empty() && m_dPending.back().cmd >= tShot.cmd)
			m_dPending.clear(); // command numbers went back (reconnect): nothing pending is valid
		m_dPending.push_back(std::move(tShot));
		while (m_dPending.size() > PendingWindow)
			m_dPending.pop_front();
	};

	if (tProvisional.perfectFirst && iBulletsPerShot == 1 && tProvisional.firstVariance == 0.f)
	{
		fRecord(pCmd->viewangles); // nothing to correct for this shot on its own
		return;
	}

	Vec3 vView = pCmd->viewangles;
	const auto tPlan = PlanShots(pLocal, { &tProvisional }, { m_iSeed }, vView);
	if (!tPlan.valid)
	{
		fRecord(pCmd->viewangles);
		return;
	}
	const Vec3 vWanted = pCmd->viewangles;
	pCmd->viewangles = vView;

	if (G::Attacking == 1 && Vars::Aimbot::General::NoSpreadDiagnostics.Value)
	{
		// Numerical check of the solve: the chosen pellet, fired with view + punch,
		// should leave along the direction the aimbot wanted.
		std::vector<BurstPlanPolicy::Pellet> vPellets; BuildPattern(tProvisional, m_iSeed, vPellets);
		const auto& tPellet = vPellets[tPlan.anchorPellet];
		Vec3 vForward, vRight, vUp; Math::AngleVectors(vView + tShot.punch, &vForward, &vRight, &vUp);
		Vec3 vPellet = vForward + (vRight * tPellet.x * flSpread) + (vUp * tPellet.y * flSpread);
		vPellet.Normalize();
		Vec3 vWantedForward; Math::AngleVectors(vWanted + tShot.punch, &vWantedForward);
		const float flDot = std::clamp(vPellet.Dot(vWantedForward), -1.f, 1.f);
		Log("solve", std::format("pellet={} x={:.3f} y={:.3f} spread={:.4f} residual_deg={:.4f} correction_deg=({:.3f}, {:.3f}) pellets={} perfect_first={} target={} hits={} default_hits={} candidates={} provisional",
			tPlan.anchorPellet, tPellet.x, tPellet.y, flSpread, Math::Rad2Deg(std::acos(flDot)),
			vView.x - vWanted.x, std::remainderf(vView.y - vWanted.y, 360.f), iBulletsPerShot, tProvisional.perfectFirst, tShot.pTarget ? tShot.targetIndex : 0, tPlan.hits, tPlan.defaultHits, tPlan.candidates));
	}

	fRecord(vView);
	G::SilentAngles = true;
}

void CNoSpreadHitscan::FinalizePacket(int iCommand, int iNewCommands)
{
	m_iFinalizedCmd = iCommand;
	m_dFinalizedMs = SDK::PlatFloatTime() * 1000.0;
	iNewCommands = std::max(iNewCommands, 1);

	// Shots of earlier packets that were never settled (not sent) are stale.
	while (!m_dPending.empty() && m_dPending.front().cmd <= iCommand - iNewCommands)
		m_dPending.pop_front();
	if (m_dPending.empty() || !ShouldRun())
		return;

	// Only commands that still attack: the triggerbot, auto fire and compatibility run
	// after No spread and may have taken the attack back.
	std::vector<PendingShot*> vShots;
	for (auto& tShot : m_dPending)
		if (tShot.cmd > iCommand - iNewCommands && tShot.cmd <= iCommand)
		{
			const CUserCmd* pSlot = I::Input->CommandSlot(tShot.cmd);
			if (pSlot && pSlot->command_number == tShot.cmd && (pSlot->buttons & IN_ATTACK))
				vShots.push_back(&tShot);
		}
	if (vShots.empty())
		return;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal || !pLocal->IsAlive())
		return;
	const auto eMode = GetSeedMode();
	if (eMode == SeedMode::Unknown)
		return;

	// The seed every command of this packet will be stamped with.
	std::vector<int> vSeeds(vShots.size(), 0);
	double dMs = 0.0, dStep = 0.0; int iTick = -1;
	const char* sSource = "command";
	if (eMode == SeedMode::Command)
	{
		for (size_t k = 0; k < vShots.size(); k++)
			vSeeds[k] = vShots[k]->commandSeed;
	}
	else if (Loopback())
	{
		if (!StepUsable())
			return;
		// Read after the last packet of this engine tick: add the rest of a doubletap.
		dMs = m_dFinalizedMs + m_dLoopbackDeltaMs + m_dTailMsPerCmd * double(RemainingThisTick());
		dStep = SeedClockPolicy::StepMs(dMs);
		std::fill(vSeeds.begin(), vSeeds.end(), SeedClockPolicy::SeedFromMs(dMs));
		sSource = "loopback";
	}
	else
	{
		const double dNow = SDK::PlatFloatTime();
		if (!m_Clock.Ready() || !StepUsable() || !AcksFresh(dNow))
			return;
		auto tPrediction = m_Clock.PredictSend(m_iEngineTick);
		sSource = "send_tick";
		if (!tPrediction.valid)
		{
			tPrediction = m_Clock.Predict(iCommand);
			sSource = "command_offset";
		}
		if (!tPrediction.valid)
			return;
		dMs = tPrediction.ms; dStep = tPrediction.step; iTick = tPrediction.tick;
		std::fill(vSeeds.begin(), vSeeds.end(), tPrediction.seed);
	}

	// Write a planned view into a command that has not been serialized yet.
	auto fWrite = [&](PendingShot& tShot, const Vec3& vPlanned) -> bool
	{
		CUserCmd* pCmd = I::Input->CommandSlot(tShot.cmd);
		if (!pCmd || pCmd->command_number != tShot.cmd)
			return false;
		// Keep anything that rotated the command after No spread ran, shifted by our change.
		Vec3 vTarget = vPlanned;
		if (AngleDelta(pCmd->viewangles, tShot.viewRun).Length() > 0.001f)
			vTarget = pCmd->viewangles + AngleDelta(vPlanned, tShot.viewRun);
		Math::ClampAngles(vTarget);
		tShot.viewRun = vTarget;
		if (AngleDelta(vTarget, pCmd->viewangles).Length() < 1e-4f)
			return false;
		SDK::FixMovement(pCmd, vTarget);
		pCmd->viewangles = vTarget;
		I::Input->CommitUserCmd(tShot.cmd);
		return true;
	};

	const bool bHold = HoldAimTicks() >= 1;
	int iWritten = 0, iChanged = 0;
	std::string sPlans;

	// A doubletap longer than one packet (22 commands at most) leaves as two packets in
	// one engine tick, run in one server tick: the first packet's first shot holds the
	// view for the second packet's shots too (0.7.0.51 log: "new=22 shots=1" then
	// "new=2 shots=1" in the same engine tick). Those later shots cannot be steered.
	const bool bHeld = bHold && m_iHoldEngineTick == m_iEngineTick;
	if (bHeld)
		sPlans = "[held by an earlier packet of this tick]";

	// And the other way round: shots the rest of this tick's burst will still fire are
	// planned now, as copies of this packet's last shot (no first-shot accuracy, same
	// seed: they are read with this packet). Single bullets only need the seed.
	std::vector<PendingShot> vFollow;
	if (bHold && !bHeld)
	{
		const PendingShot& tLast = *vShots.back();
		const int iRemaining = RemainingThisTick();
		const int iNeed = std::max(tLast.fireTicks - (iCommand - tLast.cmd), 1);
		if (iRemaining >= iNeed)
		{
			const int iCount = std::min(1 + (iRemaining - iNeed) / tLast.fireTicks, 8);
			for (int i = 0; i < iCount; i++)
			{
				PendingShot tCopy = tLast;
				tCopy.cmd = -1; // never written
				tCopy.perfectFirst = false;
				tCopy.firstVariance = 0.5f;
				vFollow.push_back(std::move(tCopy));
			}
		}
	}

	// The server holds the first shot's view for every later shot of the tick: plan them
	// together. Without the hold, each on its own.
	const bool bLocked = bHold && !bHeld && vShots.size() + vFollow.size() > 1;
	auto fPlan = [&](const std::vector<PendingShot*>& vGroup, const std::vector<int>& vGroupSeeds)
	{
		// A lone single bullet with exact first-shot accuracy flies where the aimbot aimed:
		// send that view untouched (a re-solve could only add rounding, or the pitch clamp).
		if (vGroup.size() == 1)
		{
			const auto& tShot = *vGroup.front();
			if (tShot.bullets == 1 && tShot.perfectFirst && tShot.firstVariance == 0.f && !tShot.fixedPattern)
			{
				iWritten += fWrite(*vGroup.front(), tShot.wantedView);
				if (!sPlans.empty()) sPlans += ' ';
				sPlans += "[exact first shot]";
				return;
			}
		}
		std::vector<const PendingShot*> vConst(vGroup.begin(), vGroup.end());
		Vec3 vView = vGroup.front()->viewRun;
		const auto tPlan = PlanShots(pLocal, vConst, vGroupSeeds, vView);
		if (!tPlan.valid)
			return;
		for (auto pShot : vGroup)
			iWritten += fWrite(*pShot, vView);
		if (!sPlans.empty()) sPlans += ' ';
		sPlans += std::format("[aim={} pellet={}/{} hits={} default_hits={} exact={} candidates={}]",
			tPlan.anchorAim, tPlan.anchorShot, tPlan.anchorPellet, tPlan.hits, tPlan.defaultHits, tPlan.exact, tPlan.candidates);
	};
	// Held: nothing this packet sends changes where its shots go.
	if (!bHeld && bLocked)
	{
		std::vector<PendingShot*> vGroup = vShots;
		std::vector<int> vGroupSeeds = vSeeds;
		for (auto& tCopy : vFollow) { vGroup.push_back(&tCopy); vGroupSeeds.push_back(vSeeds.back()); }
		fPlan(vGroup, vGroupSeeds);
	}
	else if (!bHeld)
		for (size_t k = 0; k < vShots.size(); k++)
			fPlan({ vShots[k] }, { vSeeds[k] });
	if (bHold && !bHeld)
		m_iHoldEngineTick = m_iEngineTick;

	std::string sRunSeeds;
	for (size_t k = 0; k < vShots.size(); k++)
	{
		if (!sRunSeeds.empty()) sRunSeeds += ',';
		sRunSeeds += std::to_string(vShots[k]->seedRun);
		iChanged += vShots[k]->seedRun != vSeeds[k];
		vShots[k]->seedRun = vSeeds[k]; // a serialization retry plans from the settled state
	}
	m_iPacketsFinalized++;
	m_iShotsResolved += int(vShots.size());
	m_iSeedsChanged += iChanged;

	Log("final", std::format("packet={} new={} shots={} following={} remaining={} tail_ms_per_cmd={:.3f} source={} ms={:.3f} step={:.3f} tick={} engine_tick={} seed_run={} seed={} changed={} locked={} held={} written={} {}",
		iCommand, iNewCommands, vShots.size(), vFollow.size(), RemainingThisTick(), m_dTailMsPerCmd, sSource, dMs, dStep, iTick, m_iEngineTick, sRunSeeds, vSeeds.front(), iChanged, bLocked, bHeld, iWritten, sPlans));
}

void CNoSpreadHitscan::Draw(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::SeedPrediction) || !ShouldRun() || !pLocal->IsAlive())
		return;

	const double dNow = SDK::PlatFloatTime();
	const auto eMode = GetSeedMode();
	const bool bCommandSeed = eMode == SeedMode::Command;
	const bool bLoopback = eMode == SeedMode::Time && Loopback();
	const bool bHaveBase = bLoopback || m_Clock.HaveBase();
	const bool bStepLow = bHaveBase && !StepUsable();
	const bool bReady = bCommandSeed || (bLoopback && StepUsable())
		|| (eMode == SeedMode::Time && !bLoopback && m_Clock.Ready() && StepUsable() && AcksFresh(dNow));
	const bool bEchoes = dNow - m_dLastEchoTime <= EchoDisplayWindow && m_Clock.EchoScored() > 0;
	const int iScored = m_Clock.EchoScored(), iMatched = m_Clock.EchoMatched();

	std::string sState;
	Color_t tAccent = MoonlitHud::Muted;
	if (bCommandSeed) { sState = "command seed"; tAccent = MoonlitHud::Green; }
	else if (eMode == SeedMode::Unknown) sState = "seed mode unavailable";
	else if (!bHaveBase) sState = "waiting for clock";
	else if (bStepLow) sState = "uptime too low";
	else if (bLoopback) { sState = "shared clock"; tAccent = MoonlitHud::Green; }
	else if (!m_Clock.OffsetKnown() || !AcksFresh(dNow)) sState = "waiting for packets";
	else if (bEchoes)
	{
		sState = std::format("echo {}/{}", iMatched, iScored);
		tAccent = iScored >= 5 && iMatched * 4 >= iScored * 3 ? MoonlitHud::Green : iMatched * 2 >= iScored ? MoonlitHud::Gold : MoonlitHud::Muted;
	}
	else { sState = "clock only"; tAccent = MoonlitHud::Gold; }

	const int iUptime = int((bLoopback ? LoopbackMs() : m_Clock.UptimeMs()) / 1000.0);
	const double dStep = bLoopback ? SeedClockPolicy::StepMs(LoopbackMs()) : m_Clock.StepNowMs();
	const std::string sStep = dStep >= 1.0 ? std::format("{:g} ms", dStep) : std::format("{:.2f} ms", dStep);
	const std::string sOffset = m_Clock.OffsetKnown() ? std::format("{}{} {}", m_Clock.Offset() >= 0 ? "+" : "", m_Clock.Offset(), m_Clock.OffsetStable() ? "steady" : "moving") : "-";

	if (MoonlitHud::Enabled())
	{
		const auto pos = Vars::Menu::SeedPredictionDisplay.Value;
		MoonlitHud::Rows rows{};
		if (eMode == SeedMode::Time)
		{
			rows.emplace_back("uptime", GetFormat(iUptime));
			rows.emplace_back("seed step", sStep);
			if (bLoopback)
				rows.emplace_back("read delay", std::format("{:.2f} ms ({})", m_dLoopbackDeltaMs, m_dLoopbackDeltas.size()));
			else if (Vars::Debug::Info.Value)
			{
				rows.emplace_back("frame offset", sOffset);
				rows.emplace_back("stamps", std::format("{} ({} agree)", m_Clock.StampCount(), m_Clock.StampAgreement()));
				rows.emplace_back("echo age", m_dLastEchoTime > 0.0 ? std::format("{:.0f} s", dNow - m_dLastEchoTime) : "-");
				rows.emplace_back("refinement", std::format("{:+.2f} ms", m_Clock.RefinementMs()));
				rows.emplace_back("send offset", m_Clock.SendOffsetKnown() ? std::format("{:+}", m_Clock.SendOffset()) : "-");
			}
			// Shots whose seed was settled when their packet was written, and how many the
			// command-time prediction had wrong (doubletap bursts, mostly).
			if (Vars::Debug::Info.Value)
				rows.emplace_back("packet seeds", std::format("{} fixed / {} shots", m_iSeedsChanged, m_iShotsResolved));
		}
		else
			rows = {{"source", bCommandSeed ? "command random seed" : "unknown"}};
		m_vIndicatorSize = MoonlitHud::Info(pos.x, pos.y, "seed prediction", sState, rows, tAccent);
		return;
	}

	int x = Vars::Menu::SeedPredictionDisplay.Value.x;
	int y = Vars::Menu::SeedPredictionDisplay.Value.y + 8;
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = fFont.m_nTall + H::Draw.Scale(1);

	EAlign align = ALIGN_TOP;
	if (x <= 100 + H::Draw.Scale(50, Scale_Round))
	{
		x -= H::Draw.Scale(42, Scale_Round);
		align = ALIGN_TOPLEFT;
	}
	else if (x >= H::Draw.m_nScreenW - 100 - H::Draw.Scale(50, Scale_Round))
	{
		x += H::Draw.Scale(42, Scale_Round);
		align = ALIGN_TOPRIGHT;
	}

	const auto& cColor = bReady ? Workspace::HudTextColour() : Workspace::HudTextColour(false);

	H::Draw.StringOutlined(fFont, x, y, cColor, Vars::Menu::Theme::Background.Value, align, sState.c_str());
	if (eMode != SeedMode::Time)
		return;
	y += nTall;
	H::Draw.StringOutlined(fFont, x, y, cColor, Vars::Menu::Theme::Background.Value, align, std::format("Uptime {}", GetFormat(iUptime)).c_str());
	H::Draw.StringOutlined(fFont, x, y += nTall, cColor, Vars::Menu::Theme::Background.Value, align, std::format("Seed step {}", sStep).c_str());
	if (Vars::Debug::Info.Value)
	{
		H::Draw.StringOutlined(fFont, x, y += nTall, cColor, Vars::Menu::Theme::Background.Value, align, std::format("Offset {}", sOffset).c_str());
		H::Draw.StringOutlined(fFont, x, y += nTall, cColor, Vars::Menu::Theme::Background.Value, align, std::format("Stamps {} ({} agree)", m_Clock.StampCount(), m_Clock.StampAgreement()).c_str());
	}
}
