#include "FakeAngle.h"
#include "PreviewFeedPolicy.h"

#include "../../PacketManip/AntiAim/AntiAim.h"
#include "../../Ticks/Ticks.h"
#include "../Groups/Groups.h"
#include "../AnimInterp/AnimInterp.h"

bool CFakeAngle::Run(CTFPlayer* pLocal, AnimFrame_t* frame)
{
	if (frame) *frame = {};
    if (bBonesSetup && !ValidSnapshot(pLocal)) Invalidate();
	if (!pLocal || !pLocal->IsAlive() || pLocal->IsAGhost()
		|| !F::AntiAim.AntiAimOn() && (!Vars::Fakelag::Fakelag.Value || F::Ticks.m_iShiftedTicks == F::Ticks.m_iMaxShift))
	{
		Invalidate();
		return false;
	}

	Group_t* pGroup = nullptr;
	if (!F::Groups.GetGroup(TargetsEnum::FakeAngle, pGroup) || !pGroup->m_tChams(true) && !pGroup->m_tGlow())
	{
		Invalidate();
		return false;
	}

	auto pAnimState = pLocal->m_PlayerAnimState();
	auto pStudioHdr = pLocal->m_pStudioHdr();
	if (!pAnimState || !pStudioHdr || !pAnimState->m_bPoseParameterInit
		|| pAnimState->m_aGestureSlots.Count() != GESTURE_SLOT_COUNT
		|| !pAnimState->m_aGestureSlots.Base())
	{
		Invalidate();
		return false;
	}
	const int boneCount = pLocal->m_CachedBoneData().Count();
	const Vec2 vAngle = { std::clamp(F::AntiAim.vFakeAngles.x, -89.f, 89.f), F::AntiAim.vFakeAngles.y };
	if (boneCount < 0 || boneCount > MAXSTUDIOBONES
		|| (boneCount && !pLocal->m_CachedBoneData().Base())
		|| !std::isfinite(vAngle.x) || !std::isfinite(vAngle.y))
	{
		Invalidate();
		return false;
	}

	// Bone construction can touch external layers as well as the animstate object.
	// Keep an owned snapshot, never a copied owning CUtlVector. Restore the live
	// engine allocations rather than resurrecting a pointer if native code moved one.
	struct StateScope
	{
		CTFPlayer* player;
		CTFPlayerAnimState* state;
		bool& complete;
		float frameTime, curTime, cycle, playback, tauntYaw, animTime;
		int sequence, sequenceParity, eventsParity, bones;
		bool loops, frameReset;
		std::array<float, 24> poses;
		std::array<unsigned char, sizeof(CTFPlayerAnimState)> animState{};
		std::array<GestureSlot_t, GESTURE_SLOT_COUNT> slots{};
		std::array<CAnimationLayer, GESTURE_SLOT_COUNT> layers{};
		matrix3x4 cachedBones[MAXSTUDIOBONES]{};
		StateScope(CTFPlayer* p, CTFPlayerAnimState* s, bool& restored) : player(p), state(s), complete(restored),
			frameTime(I::GlobalVars->frametime), curTime(I::GlobalVars->curtime),
			cycle(p->m_flCycle()), playback(p->m_flPlaybackRate()), tauntYaw(p->m_flTauntYaw()), animTime(p->m_flAnimTime()),
			sequence(p->m_nSequence()), sequenceParity(p->m_nNewSequenceParity()), eventsParity(p->m_nResetEventsParity()),
			bones(p->m_CachedBoneData().Count()), loops(p->m_bSequenceLoops()), frameReset(p->m_bClientSideFrameReset()), poses(p->m_flPoseParameter())
		{
			memcpy(animState.data(), state, animState.size());
			for (int i = 0; i < GESTURE_SLOT_COUNT; ++i)
			{
				slots[i] = state->m_aGestureSlots[i];
				if (slots[i].m_pAnimLayer) layers[i] = *slots[i].m_pAnimLayer;
			}
			if (bones) memcpy(cachedBones, p->m_CachedBoneData().Base(), sizeof(matrix3x4) * bones);
		}
		~StateScope()
		{
			I::GlobalVars->frametime = frameTime;
			I::GlobalVars->curtime = curTime;
			player->m_nSequence() = sequence;
			player->m_flCycle() = cycle;
			player->m_flPlaybackRate() = playback;
			player->m_bSequenceLoops() = loops;
			player->m_flTauntYaw() = tauntYaw;
			player->m_flAnimTime() = animTime;
			player->m_nNewSequenceParity() = sequenceParity;
			player->m_nResetEventsParity() = eventsParity;
			player->m_bClientSideFrameReset() = frameReset;
			player->m_flPoseParameter() = poses;

			std::array<unsigned char, sizeof(state->m_aGestureSlots)> liveVector{};
			memcpy(liveVector.data(), &state->m_aGestureSlots, liveVector.size());
			const int liveCount = state->m_aGestureSlots.Count();
			const bool validSlots = liveCount >= 0 && liveCount <= GESTURE_SLOT_COUNT
				&& (!liveCount || state->m_aGestureSlots.Base());
			std::array<CAnimationLayer*, GESTURE_SLOT_COUNT> liveLayers{};
			if (validSlots) for (int i = 0; i < liveCount; ++i) liveLayers[i] = state->m_aGestureSlots[i].m_pAnimLayer;
			memcpy(state, animState.data(), animState.size());
			memcpy(&state->m_aGestureSlots, liveVector.data(), liveVector.size());
			complete = validSlots && liveCount == GESTURE_SLOT_COUNT;
			if (validSlots) for (int i = 0; i < liveCount; ++i)
			{
				if (liveLayers[i] != slots[i].m_pAnimLayer) complete = false;
				state->m_aGestureSlots[i] = slots[i];
				// A changed layer allocation belongs to the engine. Do not dereference
				// the old pointer; restore its value into the current slot and reject the fake.
				state->m_aGestureSlots[i].m_pAnimLayer = liveLayers[i];
				if (liveLayers[i] && slots[i].m_pAnimLayer) *liveLayers[i] = layers[i];
			}

			auto& cache = player->m_CachedBoneData();
			const int currentBones = cache.Count();
			if (currentBones >= 0 && currentBones <= MAXSTUDIOBONES && (!currentBones || cache.Base()))
			{
				const int restoreCount = std::min(bones, currentBones);
				if (restoreCount) memcpy(cache.Base(), cachedBones, sizeof(matrix3x4) * restoreCount);
				// Do not expose newly generated fake entries to direct cache readers.
				// Shrinking does not reallocate or free the engine's backing allocation.
				if (currentBones > bones) cache.RemoveMultiple(bones, currentBones - bones);
				if (currentBones < bones) complete = false;
			}
			else complete = false;
			player->InvalidateBoneCache();
		}
	};
	AnimFrame_t captured = {};
	bool built = false, restored = false;
	{
		StateScope restore(pLocal, pAnimState, restored);
		CAnimInterp::FakeBuildScope fakeScope(F::AnimInterp);
		I::GlobalVars->frametime = 0.f;
		if (pLocal->IsTaunting() && pLocal->m_bAllowMoveDuringTaunt()) pLocal->m_flTauntYaw() = vAngle.y;
		// Orient the already evaluated real animation phase. A full Update also
		// advances external interpolation history and can dispatch gesture events.
		pAnimState->m_flEyeYaw = Math::NormalizeAngle(vAngle.y);
		pAnimState->m_flEyePitch = vAngle.x;
		pAnimState->m_flCurrentFeetYaw = pAnimState->m_flGoalFeetYaw = pAnimState->m_flEyeYaw;
		pAnimState->m_angRender = { 0.f, pAnimState->m_flEyeYaw, 0.f };
		pAnimState->ComputePoseParam_AimPitch(pStudioHdr);
		pAnimState->ComputePoseParam_AimYaw(pStudioHdr);
		if (!pLocal->IsTaunting()) pAnimState->ComputePoseParam_MoveYaw(pStudioHdr);
		const bool capturedFrame = !frame || F::AnimInterp.CaptureFrame(pLocal, captured);
		if (capturedFrame)
		{
			pLocal->InvalidateBoneCache();
			built = pLocal->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);
		}
	}
	if (!built || !restored)
	{
		Invalidate();
		return false;
	}
	bBonesSetup = true;
	if (frame) *frame = captured;
	vOrigin = pLocal->GetAbsOrigin();
	vAngles = vAngle;
	flCurTime = I::GlobalVars->curtime;
	m_flBuiltRealtime = I::GlobalVars->realtime;
	m_pBuiltModel = pLocal->GetModel();
	m_nBuiltClass = pLocal->m_iClass();
    m_uBuiltHandle = uint32_t(pLocal->GetRefEHandle().ToInt());
    m_uBuiltWeapon = uint32_t(pLocal->m_hActiveWeapon().ToInt());
    m_nBuiltSpawn = pLocal->m_iSpawnCounter();
    m_nBuiltServer = I::ClientState->m_nServerCount;
    m_nBuiltTeam = pLocal->m_iTeamNum();
	return true;
}

void CFakeAngle::Invalidate()
{
    if (bBonesSetup || m_bRenderReady) ++m_uGeneration;
    bBonesSetup = m_bRenderReady = false;
    m_nRenderFrame = -1; m_flBuiltRealtime = -1.f; m_pBuiltModel = nullptr;
}

bool CFakeAngle::ValidSnapshot(CTFPlayer* local) const
{
    const float now = I::GlobalVars->realtime;
    return !G::Unload && bDrawChams && bBonesSetup && local && local->IsAlive()
        && !local->IsDormant() && !local->IsAGhost() && std::isfinite(now)
        && now >= m_flBuiltRealtime && now - m_flBuiltRealtime <= .5f
        && local->GetModel() == m_pBuiltModel && local->m_iClass() == m_nBuiltClass
        && local->m_iTeamNum() == m_nBuiltTeam && local->m_iSpawnCounter() == m_nBuiltSpawn
        && uint32_t(local->GetRefEHandle().ToInt()) == m_uBuiltHandle
        && uint32_t(local->m_hActiveWeapon().ToInt()) == m_uBuiltWeapon
        && I::ClientState->m_nServerCount == m_nBuiltServer;
}

void CFakeAngle::PublishRender(CTFPlayer* local, const matrix3x4* bones, int count)
{
    m_bRenderReady = false;
    if (!ValidSnapshot(local) || !bones || count <= 0 || count > MAXSTUDIOBONES) return;
    const auto model = local->GetStudiomodel();
    if (!model || model->numbones <= 0 || model->numbones > MAXSTUDIOBONES || count < model->numbones) return;
    for (int i = 0; i < count; ++i)
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 4; ++col)
                if (!std::isfinite(bones[i][row][col])) return;
    memcpy(m_aRenderBones, bones, sizeof(matrix3x4) * count);
    m_nRenderFrame = I::GlobalVars->framecount;
    m_bRenderReady = true;
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
	auto renderBones = local ? RenderBones(local->GetModel()) : nullptr;
	bool valid = renderBones && ValidSnapshot(local) && local && local->IsAlive() && !local->IsAGhost() && !local->IsDormant()
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
			// Read the same current render-frame bones as Chams/Glow.
			// Read the hitbox centre; do not call SetupBones or apply another yaw/position correction.
			Math::VectorTransform((box->bbmin + box->bbmax) / 2.f, renderBones[box->bone], point);
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
