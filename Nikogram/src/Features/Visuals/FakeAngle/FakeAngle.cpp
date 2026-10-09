#include "FakeAngle.h"
#include "PreviewFeedPolicy.h"

#include "../../PacketManip/AntiAim/AntiAim.h"
#include "../../Ticks/Ticks.h"
#include "../Groups/Groups.h"
#include "../AnimInterp/AnimInterp.h"

void CFakeAngle::Run(CTFPlayer* pLocal)
{
	if (!pLocal || !pLocal->IsAlive() || pLocal->IsAGhost()
		|| !F::AntiAim.AntiAimOn() && (!Vars::Fakelag::Fakelag.Value || F::Ticks.m_iShiftedTicks == F::Ticks.m_iMaxShift))
	{
		bBonesSetup = false;
		return;
	}

	Group_t* pGroup = nullptr;
	if (!F::Groups.GetGroup(TargetsEnum::FakeAngle, pGroup) || !pGroup->m_tChams(true) && !pGroup->m_tGlow())
	{
		bBonesSetup = false;
		return;
	}

	auto pAnimState = pLocal->m_PlayerAnimState();
	if (!pAnimState)
	{
		bBonesSetup = false;
		return;
	}

	float flOldFrameTime = I::GlobalVars->frametime;
	int nOldSequence = pLocal->m_nSequence();
	float flOldCycle = pLocal->m_flCycle();
	float flOldPlaybackRate = pLocal->m_flPlaybackRate();
	bool bOldSequenceLoops = pLocal->m_bSequenceLoops();
	float flOldTauntYaw = pLocal->m_flTauntYaw();
	auto pOldPoseParams = pLocal->m_flPoseParameter();
	char pOldAnimState[sizeof(CTFPlayerAnimState)];
	memcpy(pOldAnimState, pAnimState, sizeof(CTFPlayerAnimState));
	const int nSlots = std::min(pAnimState->m_aGestureSlots.Count(), int(GESTURE_SLOT_COUNT));
	std::array<CAnimationLayer, GESTURE_SLOT_COUNT> aOldLayers = {};
	for (int i = 0; i < nSlots; i++)
	{
		if (auto pLayer = pAnimState->m_aGestureSlots[i].m_pAnimLayer)
			aOldLayers[i] = *pLayer;
	}
	CAnimInterp::FakeBuildScope fakeScope(F::AnimInterp);

	I::GlobalVars->frametime = 0.f;
	Vec2 vAngle = { std::clamp(F::AntiAim.vFakeAngles.x, -89.f, 89.f), F::AntiAim.vFakeAngles.y };
	if (pLocal->IsTaunting() && pLocal->m_bAllowMoveDuringTaunt())
		pLocal->m_flTauntYaw() = vAngle.y;
	pAnimState->Update(pAnimState->m_flCurrentFeetYaw = /*pAnimState->m_flEyeYaw =*/ vAngle.y, vAngle.x);
	pLocal->InvalidateBoneCache();
	bBonesSetup = pLocal->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);
	vOrigin = pLocal->GetAbsOrigin();
	vAngles = vAngle;
	flCurTime = I::GlobalVars->curtime;
	m_flBuiltRealtime = I::GlobalVars->realtime;
	m_pBuiltModel = pLocal->GetModel();
	m_nBuiltClass = pLocal->m_iClass();

	I::GlobalVars->frametime = flOldFrameTime;
	pLocal->m_nSequence() = nOldSequence;
	pLocal->m_flCycle() = flOldCycle;
	pLocal->m_flPlaybackRate() = flOldPlaybackRate;
	pLocal->m_bSequenceLoops() = bOldSequenceLoops;
	pLocal->m_flTauntYaw() = flOldTauntYaw;
	pLocal->m_flPoseParameter() = pOldPoseParams;
	memcpy(pAnimState, pOldAnimState, sizeof(CTFPlayerAnimState));
	for (int i = 0; i < nSlots; i++)
	{
		if (auto pLayer = pAnimState->m_aGestureSlots[i].m_pAnimLayer)
			*pLayer = aOldLayers[i];
	}
	pLocal->InvalidateBoneCache();
}

namespace
{
	bool PreviewLabConnection()
	{
		if (G::Unload || !I::EngineClient->IsInGame() || I::EngineClient->IsPlayingDemo()) return false;
		auto channel = I::EngineClient->GetNetChannelInfo();
		const char* address = channel ? channel->GetAddress() : nullptr;
		const char* level = I::EngineClient->GetLevelName();
		// The addon checks its authoritative sv_lan/enable flags. Do not use the
		// client engine's own sv_lan value as a dedicated-server handshake.
		return address && level && PreviewFeedPolicy::LocalLabAddress(address)
			&& PreviewFeedPolicy::Itemtest(level);
	}
}

void CFakeAngle::PreviewFeedCommand(const char* text)
{
	if (!strcmp(text, "off") || !strcmp(text, "0"))
	{
		if (m_nFeedToken && m_nFeedSequence < INT_MAX && PreviewLabConnection()
			&& m_pFeedChannel == I::EngineClient->GetNetChannelInfo() && m_nFeedServerCount == I::ClientState->m_nServerCount)
		{
			const auto stop = std::format("sm_aapreview 1 {} {} {} {} 0 0 0 0 0", m_nFeedToken, m_nFeedUserId,
				++m_nFeedSequence, I::ClientState->m_ClockDriftMgr.m_nServerTick);
			I::EngineClient->ServerCmd(stop.c_str(), false);
		}
		m_nFeedToken = 0;
		SDK::Output("AA Lab", "Preview feed OFF. Stop requested; the previous sample also expires within 0.20s if the message is lost.");
		return;
	}
	int token = 0;
	player_info_t info = {};
	if (!PreviewFeedPolicy::ParseToken(text, token) || !PreviewLabConnection()
		|| !I::EngineClient->GetPlayerInfo(I::EngineClient->GetLocalPlayer(), &info))
	{
		SDK::Output("AA Lab", "Not enabled. On the local itemtest lab, use !aamode preview, then copy its aa_preview command here.");
		return;
	}
	// Keep sequence monotonic even when the same token is enabled again. Resetting
	// it would make the receiver reject new samples until the old count is reached.
	m_nFeedToken = token; m_nFeedUserId = info.userID;
	m_pFeedChannel = I::EngineClient->GetNetChannelInfo();
	m_nFeedServerCount = I::ClientState->m_nServerCount;
	m_flNextFeed = 0.f;
	SDK::Output("AA Lab", "Preview-head feed armed for THIS local connection only. No AA settings changed. aa_preview off stops it.");
}

void CFakeAngle::SendPreviewHead()
{
	if (!m_nFeedToken) return;
	player_info_t info = {};
	if (!PreviewLabConnection() || m_pFeedChannel != I::EngineClient->GetNetChannelInfo()
		|| m_nFeedServerCount != I::ClientState->m_nServerCount
		|| !I::EngineClient->GetPlayerInfo(I::EngineClient->GetLocalPlayer(), &info) || info.userID != m_nFeedUserId)
	{
		m_nFeedToken = 0; // never carry consent into a different connection/map
		return;
	}
	const float now = I::GlobalVars->realtime;
	if (now < m_flNextFeed) return;
	m_flNextFeed = now + PreviewFeedPolicy::SendInterval;
	auto local = H::Entities.GetLocal();
	Vec3 point = {};
	bool valid = local && local->IsAlive() && !local->IsAGhost() && !local->IsDormant()
		&& bDrawChams && bBonesSetup && local->GetModel() == m_pBuiltModel
		&& local->m_iClass() == m_nBuiltClass && PreviewFeedPolicy::Fresh(now, m_flBuiltRealtime);
	if (valid)
	{
		auto hdr = local->GetStudiomodel();
		auto set = local->GetHitboxSet();
		valid = false;
		for (int i = 0; hdr && set && i < set->numhitboxes; ++i)
		{
			auto box = set->pHitbox(i);
			if (!box || box->group != 1 || box->bone < 0 || box->bone >= hdr->numbones || box->bone >= MAXSTUDIOBONES) continue;
			// aBones is exactly what Chams/Glow draw, including AnimInterp's render-only replacement.
			// Read the hitbox centre; do not call SetupBones or apply another yaw/position correction.
			Math::VectorTransform((box->bbmin + box->bbmax) / 2.f, aBones[box->bone], point);
			const Vec3 origin = local->GetAbsOrigin();
			const float coordinates[3] = { point.x, point.y, point.z }, anchor[3] = { origin.x, origin.y, origin.z };
			valid = PreviewFeedPolicy::NearOrigin(coordinates, anchor);
			break;
		}
	}
	const int sourceTick = I::ClientState->m_ClockDriftMgr.m_nServerTick;
	if (m_nFeedSequence == INT_MAX) { m_nFeedToken = 0; return; }
	const auto command = std::format("sm_aapreview 1 {} {} {} {} {} {} {:.3f} {:.3f} {:.3f}",
		m_nFeedToken, m_nFeedUserId, ++m_nFeedSequence, sourceTick, local ? local->m_iClass() : 0,
		valid ? 1 : 0, valid ? point.x : 0.f, valid ? point.y : 0.f, valid ? point.z : 0.f);
	// Unreliable diagnostic message: no retransmission queue and no forced SendDatagram/SendPacket.
	I::EngineClient->ServerCmd(command.c_str(), false);
}
