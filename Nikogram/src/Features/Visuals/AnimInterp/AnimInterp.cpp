#include "AnimInterp.h"

#include "../FakeAngle/FakeAngle.h"
#include "../../Backtrack/Backtrack.h"
#include "../../Resolver/Resolver.h"
#include <utility>

// most ticks playback may trail the newest simulated tick by (sv_maxusrcmdprocessticks is 24)
static constexpr float MAX_DELAY_TICKS = 24.f;
// extra delay on top of the largest recent burst, so frame timing jitter never starves playback
static constexpr float DELAY_MARGIN_TICKS = 1.f;
// how long a burst size is remembered when picking the delay
static constexpr float BURST_WINDOW = 1.f;
// a burst this far from the playback clock is treated as instant ticks (doubletap, warp, speedhack, hitches) and skipped over
static constexpr float RESYNC_TICKS = 6.f;
// a timeline that hasn't received ticks for this long is not drawn and restarts on the next tick
static constexpr float STALE_TIME = 0.5f;
static constexpr size_t MAX_FRAMES = 96;

static inline int& ModelBoneCounter()
{
	// CBaseEntity_InvalidateBoneCache starts with "mov eax, [g_iModelBoneCounter]" (8B 05 rel32).
	// Bumping the counter is what the engine itself does every frame to invalidate every bone cache at once.
	static auto pCounter = reinterpret_cast<int*>(U::Memory.RelToAbs(S::CBaseEntity_InvalidateBoneCache(), 2));
	return *pCounter;
}

static inline float LerpCycle(float flFrom, float flTo, float t)
{
	if (flTo >= flFrom)
		return Math::Lerp(flFrom, flTo, t);

	if (flFrom - flTo > 0.5f)
	{	// looped around
		float flCycle = Math::Lerp(flFrom, flTo + 1.f, t);
		return flCycle >= 1.f ? flCycle - 1.f : flCycle;
	}

	return flTo; // restarted, don't play it backwards
}

static bool Capture(CTFPlayer* pPlayer, AnimFrame_t& tFrame)
{
	auto pAnimState = pPlayer->m_PlayerAnimState();
	if (!pAnimState)
		return false;

	tFrame.m_pModel = pPlayer->GetModel();
	tFrame.m_nSequence = pPlayer->m_nSequence();
	tFrame.m_flCycle = pPlayer->m_flCycle();
	tFrame.m_flPlaybackRate = pPlayer->m_flPlaybackRate();
	tFrame.m_bSequenceLoops = pPlayer->m_bSequenceLoops();
	tFrame.m_aPoseParameters = pPlayer->m_flPoseParameter();
	tFrame.m_vRenderAngles = pAnimState->m_angRender;

	const int nSlots = std::min(pAnimState->m_aGestureSlots.Count(), int(GESTURE_SLOT_COUNT));
	for (int i = 0; i < GESTURE_SLOT_COUNT; i++)
	{
		auto& tLayer = tFrame.m_aLayers[i];
		auto pLayer = i < nSlots ? pAnimState->m_aGestureSlots[i].m_pAnimLayer : nullptr;
		tLayer.m_bValid = pLayer != nullptr;
		if (!pLayer)
			continue;

		tLayer.m_nSequence = pLayer->m_nSequence;
		tLayer.m_flPrevCycle = pLayer->m_flPrevCycle;
		tLayer.m_flWeight = pLayer->m_flWeight;
		tLayer.m_nOrder = pLayer->m_nOrder;
		tLayer.m_flPlaybackRate = pLayer->m_flPlaybackRate;
		tLayer.m_flCycle = pLayer->m_flCycle;
	}

	return true;
}

static void Apply(CTFPlayer* pPlayer, const AnimFrame_t& tFrame)
{
	auto pAnimState = pPlayer->m_PlayerAnimState();
	if (!pAnimState)
		return;

	pPlayer->m_nSequence() = tFrame.m_nSequence;
	pPlayer->m_flCycle() = tFrame.m_flCycle;
	pPlayer->m_flPlaybackRate() = tFrame.m_flPlaybackRate;
	pPlayer->m_bSequenceLoops() = tFrame.m_bSequenceLoops;
	pPlayer->m_flPoseParameter() = tFrame.m_aPoseParameters;
	pAnimState->m_angRender = tFrame.m_vRenderAngles;

	const int nSlots = std::min(pAnimState->m_aGestureSlots.Count(), int(GESTURE_SLOT_COUNT));
	for (int i = 0; i < nSlots; i++)
	{
		auto& tLayer = tFrame.m_aLayers[i];
		auto pLayer = pAnimState->m_aGestureSlots[i].m_pAnimLayer;
		if (!tLayer.m_bValid || !pLayer)
			continue;

		pLayer->m_nSequence = tLayer.m_nSequence;
		pLayer->m_flPrevCycle = tLayer.m_flPrevCycle;
		pLayer->m_flWeight = tLayer.m_flWeight;
		pLayer->m_nOrder = tLayer.m_nOrder;
		pLayer->m_flPlaybackRate = tLayer.m_flPlaybackRate;
		pLayer->m_flCycle = tLayer.m_flCycle;
	}
}

static void Blend(const AnimFrame_t& tFrom, const AnimFrame_t& tTo, float t, AnimFrame_t& tOut)
{
	tOut = tTo;

	// on a sequence change take the new one as is, MaintainSequenceTransitions blends it while drawing
	if (tFrom.m_nSequence == tTo.m_nSequence)
	{
		tOut.m_flCycle = LerpCycle(tFrom.m_flCycle, tTo.m_flCycle, t);
		tOut.m_flPlaybackRate = Math::Lerp(tFrom.m_flPlaybackRate, tTo.m_flPlaybackRate, t);
	}

	for (size_t i = 0; i < tOut.m_aPoseParameters.size(); i++)
	{
		const float flFrom = tFrom.m_aPoseParameters[i], flTo = tTo.m_aPoseParameters[i];
		if (fabsf(flTo - flFrom) <= 0.5f) // larger jumps are wraps or snaps, don't sweep through the middle
			tOut.m_aPoseParameters[i] = Math::Lerp(flFrom, flTo, t);
	}

	tOut.m_vRenderAngles.x = Math::Lerp(tFrom.m_vRenderAngles.x, tTo.m_vRenderAngles.x, t);
	tOut.m_vRenderAngles.y = Math::NormalizeAngle(tFrom.m_vRenderAngles.y + Math::NormalizeAngle(tTo.m_vRenderAngles.y - tFrom.m_vRenderAngles.y) * t);
	tOut.m_vRenderAngles.z = Math::Lerp(tFrom.m_vRenderAngles.z, tTo.m_vRenderAngles.z, t);

	for (size_t i = 0; i < tOut.m_aLayers.size(); i++)
	{
		auto& tLayerFrom = tFrom.m_aLayers[i], &tLayerTo = tTo.m_aLayers[i];
		auto& tLayerOut = tOut.m_aLayers[i];
		if (!tLayerFrom.m_bValid || !tLayerTo.m_bValid || tLayerFrom.m_nSequence != tLayerTo.m_nSequence)
			continue; // new gesture, take it as is

		tLayerOut.m_flCycle = LerpCycle(tLayerFrom.m_flCycle, tLayerTo.m_flCycle, t);
		tLayerOut.m_flPrevCycle = LerpCycle(tLayerFrom.m_flPrevCycle, tLayerTo.m_flPrevCycle, t);
		tLayerOut.m_flWeight = Math::Lerp(tLayerFrom.m_flWeight, tLayerTo.m_flWeight, t);
		tLayerOut.m_flPlaybackRate = Math::Lerp(tLayerFrom.m_flPlaybackRate, tLayerTo.m_flPlaybackRate, t);
	}
}



bool CAnimInterp::Enabled()
{
	return Vars::Visuals::Animations::Interpolation.Value && !G::Unload;
}

void CAnimInterp::Forget(CTFPlayer* pPlayer)
{
	for (auto it = m_mTimelines.begin(); it != m_mTimelines.end();)
	{
		if (it->second.m_pPlayer == pPlayer)
			it = m_mTimelines.erase(it);
		else
			++it;
	}
}

bool CAnimInterp::ShouldRebuildBones(CBaseEntity* pEntity)
{
	if (m_bRendering)
		return true; // sequence transitions also need fresh bones for normally animated players
	if (!pEntity)
		return false;
	auto it = m_mDirtyPlayers.find(pEntity->entindex());
	return it != m_mDirtyPlayers.end() && it->second == pEntity;
}

void CAnimInterp::BonesRebuilt(CBaseEntity* pEntity)
{
	if (!m_bRendering && pEntity)
		m_mDirtyPlayers.erase(pEntity->entindex());
}

void CAnimInterp::Record(CTFPlayer* pPlayer, int iElapsedTicks, bool bBatchEndpoint)
{
	if (!pPlayer || !Enabled() || iElapsedTicks <= 0)
		return;

	auto& tTimeline = m_mTimelines[pPlayer->entindex()];
	if (tTimeline.m_pPlayer != pPlayer || !pPlayer->IsAlive() || tTimeline.m_bBatchEndpoints != bBatchEndpoint
		|| tTimeline.m_bStarted && I::GlobalVars->realtime - tTimeline.m_flLastArrival > STALE_TIME
		|| !tTimeline.m_vFrames.empty() && tTimeline.m_vFrames.back().m_pModel != pPlayer->GetModel())
	{	// new entity, death, model change (class, disguise) or a long gap: start over rather than blend across it
		tTimeline = {};
		tTimeline.m_pPlayer = pPlayer;
		tTimeline.m_bBatchEndpoints = bBatchEndpoint;
		if (!pPlayer->IsAlive())
			return;
	}

	AnimFrame_t tFrame = {};
	if (!Capture(pPlayer, tFrame))
		return;

	tTimeline.m_iNextTick += iElapsedTicks;
	tFrame.m_iTick = tTimeline.m_iNextTick - 1;
	tTimeline.m_vFrames.push_back(tFrame);
	tTimeline.m_iBurst += iElapsedTicks;
	while (tTimeline.m_vFrames.size() > MAX_FRAMES)
		tTimeline.m_vFrames.pop_front();
}

void CAnimInterp::EndBurst(CTFPlayer* pPlayer)
{
	if (!pPlayer || !Enabled())
		return;

	auto it = m_mTimelines.find(pPlayer->entindex());
	if (it == m_mTimelines.end())
		return;

	auto& tTimeline = it->second;
	const int iBurst = std::exchange(tTimeline.m_iBurst, 0);
	if (tTimeline.m_pPlayer != pPlayer || !iBurst || tTimeline.m_vFrames.empty())
		return;

	const float flNow = I::GlobalVars->realtime;
	const float flNewest = float(tTimeline.m_vFrames.back().m_iTick);

	{	// CEntities::Store only advances the main sequence cycle on the first tick of a burst (FrameAdvance(0) is curtime based),
		// spread that advance over the whole burst so it plays back evenly
		auto& vFrames = tTimeline.m_vFrames;
		const size_t nFrames = vFrames.size();
		if (!tTimeline.m_bBatchEndpoints && iBurst >= 2 && nFrames > size_t(iBurst))
		{
			const size_t nFirst = nFrames - size_t(iBurst);
			const auto& tAnchor = vFrames[nFirst - 1];
			const auto& tLast = vFrames.back();
			bool bSpread = tAnchor.m_nSequence == tLast.m_nSequence;
			for (size_t i = nFirst; bSpread && i < nFrames; i++)
				bSpread = vFrames[i].m_nSequence == tLast.m_nSequence && vFrames[i].m_flCycle == tLast.m_flCycle;
			for (size_t i = nFirst; bSpread && i + 1 < nFrames; i++)
				vFrames[i].m_flCycle = LerpCycle(tAnchor.m_flCycle, tLast.m_flCycle, float(i - nFirst + 1) / float(iBurst));
		}
	}

	const bool bFirst = !tTimeline.m_bStarted;
	bool bResync = bFirst;
	if (!bFirst)
	{
		const float flError = flNewest - tTimeline.m_flClock;
		if (fabsf(flError) > RESYNC_TICKS)
			bResync = true;
		else
			tTimeline.m_flClock += flError * 0.2f; // gently pull the clock towards real tick arrival
	}

	// resync bursts (doubletap etc) are instant, they shouldn't make playback lag behind for the next second
	if (!bResync || bFirst)
		tTimeline.m_vBursts.emplace_back(flNow, iBurst);
	while (!tTimeline.m_vBursts.empty() && flNow - tTimeline.m_vBursts.front().first > BURST_WINDOW)
		tTimeline.m_vBursts.pop_front();

	// delay by the largest recent burst (choke) so the next burst always arrives before playback runs out
	int iLargest = 1;
	for (auto& [flTime, iSize] : tTimeline.m_vBursts)
		iLargest = std::max(iLargest, iSize);
	tTimeline.m_flTargetDelay = std::min(float(iLargest), MAX_DELAY_TICKS) + DELAY_MARGIN_TICKS;

	if (bResync)
	{
		tTimeline.m_flClock = flNewest;
		if (bFirst)
		{
			tTimeline.m_flDelay = tTimeline.m_flTargetDelay;
			tTimeline.m_flCursor = flNewest - tTimeline.m_flDelay;
		}
	}

	tTimeline.m_bStarted = true;
	tTimeline.m_flLastArrival = flNow;
}

void CAnimInterp::Advance(AnimTimeline_t& tTimeline, float flDeltaTicks)
{
	const float flOldest = float(tTimeline.m_vFrames.front().m_iTick);
	const float flNewest = float(tTimeline.m_vFrames.back().m_iTick);

	// don't let the clock run away if ticks stop arriving
	tTimeline.m_flClock = std::min(tTimeline.m_flClock + flDeltaTicks, flNewest + tTimeline.m_flTargetDelay);

	// ease into delay changes (playback runs at 0.5x - 1.5x speed meanwhile) instead of jumping
	const float flSlew = flDeltaTicks * 0.5f;
	tTimeline.m_flDelay += std::clamp(tTimeline.m_flTargetDelay - tTimeline.m_flDelay, -flSlew, flSlew);

	// never rewind, never extrapolate past the newest simulated tick
	tTimeline.m_flCursor = std::clamp(std::max(tTimeline.m_flCursor, tTimeline.m_flClock - tTimeline.m_flDelay), flOldest, flNewest);

	// drop frames playback has passed, keeping the one we interpolate from
	while (tTimeline.m_vFrames.size() > 2 && float(tTimeline.m_vFrames[1].m_iTick) <= tTimeline.m_flCursor)
		tTimeline.m_vFrames.pop_front();
}

bool CAnimInterp::Sample(const AnimTimeline_t& tTimeline, AnimFrame_t& tOut)
{
	const auto& vFrames = tTimeline.m_vFrames;
	if (vFrames.empty())
		return false;

	const float flCursor = tTimeline.m_flCursor;
	if (vFrames.size() == 1 || flCursor >= float(vFrames.back().m_iTick))
	{
		tOut = vFrames.back();
		return true;
	}
	if (flCursor <= float(vFrames.front().m_iTick))
	{
		tOut = vFrames.front();
		return true;
	}

	// Local endpoints are sparse (and batch sizes can change). Bracket by tick time,
	// not deque index; the interpolation fraction below uses the real elapsed span.
	size_t i = 0;
	while (i + 1 < vFrames.size() - 1 && float(vFrames[i + 1].m_iTick) <= flCursor)
		++i;
	const auto& tFrom = vFrames[i];
	const auto& tTo = vFrames[i + 1];
	const float flSpan = float(tTo.m_iTick - tFrom.m_iTick);
	const float t = flSpan > 0.f ? std::clamp((flCursor - float(tFrom.m_iTick)) / flSpan, 0.f, 1.f) : 1.f;
	Blend(tFrom, tTo, t, tOut);
	return true;
}

void CAnimInterp::RebuildFakeAngle(CTFPlayer* pLocal, const AnimFrame_t& tFrame)
{
	if (!F::FakeAngle.bBonesSetup || !F::FakeAngle.bDrawChams)
		return;

	auto pAnimState = pLocal->m_PlayerAnimState();
	if (!pAnimState)
		return;

	// same steps as CFakeAngle::Run, but on top of the smoothed pose instead of the last simulated one
	const float flOldFrameTime = I::GlobalVars->frametime;
	const float flOldCurTime = I::GlobalVars->curtime;
	const float flOldTauntYaw = pLocal->m_flTauntYaw();
	char pOldAnimState[sizeof(CTFPlayerAnimState)];
	memcpy(pOldAnimState, pAnimState, sizeof(CTFPlayerAnimState));
	const int nSlots = std::min(pAnimState->m_aGestureSlots.Count(), int(GESTURE_SLOT_COUNT));
	std::array<CAnimationLayer, GESTURE_SLOT_COUNT> aOldLayers = {};
	for (int i = 0; i < nSlots; i++)
	{
		if (auto pLayer = pAnimState->m_aGestureSlots[i].m_pAnimLayer)
			aOldLayers[i] = *pLayer;
	}

	I::GlobalVars->frametime = 0.f;
	I::GlobalVars->curtime = F::FakeAngle.flCurTime;
	const Vec2 vAngle = F::FakeAngle.vAngles;
	if (pLocal->IsTaunting() && pLocal->m_bAllowMoveDuringTaunt())
		pLocal->m_flTauntYaw() = vAngle.y;
	pAnimState->Update(pAnimState->m_flCurrentFeetYaw = vAngle.y, vAngle.x);

	FakeBuildScope fakeScope(*this);
	pLocal->InvalidateBoneCache();
	const bool bSetup = pLocal->SetupBones(m_aFakeBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);

	I::GlobalVars->frametime = flOldFrameTime;
	I::GlobalVars->curtime = flOldCurTime;
	pLocal->m_flTauntYaw() = flOldTauntYaw;
	memcpy(pAnimState, pOldAnimState, sizeof(CTFPlayerAnimState));
	for (int i = 0; i < nSlots; i++)
	{	// gesture layers aren't part of the anim state memory, put them back as a whole
		if (auto pLayer = pAnimState->m_aGestureSlots[i].m_pAnimLayer)
			*pLayer = aOldLayers[i];
	}
	Apply(pLocal, tFrame); // Update() rewrote sequence, cycle and pose parameters
	pLocal->InvalidateBoneCache();

	if (!bSetup)
		return;

	// keep the fake where CFakeAngle::Run put it (the last sent position), only its pose is smoothed
	const Vec3 vDelta = F::FakeAngle.vOrigin - pLocal->GetAbsOrigin();
	const int nBones = std::min(pLocal->m_CachedBoneData().Count(), int(MAXSTUDIOBONES));
	for (int i = 0; i < nBones; i++)
	{
		m_aFakeBones[i][0][3] += vDelta.x;
		m_aFakeBones[i][1][3] += vDelta.y;
		m_aFakeBones[i][2][3] += vDelta.z;
	}
	memcpy(m_aOriginalFakeBones, F::FakeAngle.aBones, sizeof(m_aOriginalFakeBones));
	m_bFakeApplied = true;
	memcpy(F::FakeAngle.aBones, m_aFakeBones, sizeof(matrix3x4) * nBones);
}

void CAnimInterp::InvalidateAllBones()
{
	ModelBoneCounter()++;
}

void CAnimInterp::RenderStart()
{
	Restore(); // normally empty, RenderEnd already restored

	const float flNow = I::GlobalVars->realtime;
	const float flDeltaTicks = m_flLastRender > 0.f ? std::clamp((flNow - m_flLastRender) / TICK_INTERVAL, 0.f, 8.f) : 0.f;
	m_flLastRender = flNow;

	if (!Enabled())
	{
		m_mTimelines.clear();
		return;
	}

	const int iLocal = I::EngineClient->GetLocalPlayer();
	const bool bDemo = I::EngineClient->IsPlayingDemo();
	CTFPlayer* pLocal = nullptr;
	AnimFrame_t tLocalFrame = {};

	for (auto it = m_mTimelines.begin(); it != m_mTimelines.end();)
	{
		const int iIndex = it->first;
		auto& tTimeline = it->second;

		auto pClient = I::ClientEntityList->GetClientEntity(iIndex);
		auto pPlayer = pClient ? pClient->As<CTFPlayer>() : nullptr;
		if (!pPlayer || pPlayer != tTimeline.m_pPlayer)
		{
			it = m_mTimelines.erase(it);
			continue;
		}
		if (!pPlayer->IsAlive() || pPlayer->IsDormant())
		{
			it = m_mTimelines.erase(it);
			continue;
		}
		++it;

		if (!tTimeline.m_bStarted || tTimeline.m_vFrames.empty() || flNow - tTimeline.m_flLastArrival > STALE_TIME
			|| !pPlayer->IsAlive() || pPlayer->IsDormant())
			continue;

		// local: animated by CVisuals::LocalAnimations, except in demos
		// others: only when the resolver drives them, and "remove interpolation" is respected
		const bool bLocal = iIndex == iLocal;
		if (bLocal ? bDemo : (Vars::Visuals::Removals::Interpolation.Value || !F::Resolver.GetAngles(pPlayer)))
			continue;

		Advance(tTimeline, flDeltaTicks);

		AnimFrame_t tFrame = {};
		Applied_t tApplied = { pPlayer, iIndex };
		if (!Sample(tTimeline, tFrame) || tFrame.m_pModel != pPlayer->GetModel() || !Capture(pPlayer, tApplied.m_tTrue))
			continue;

		Apply(pPlayer, tFrame);
		m_vApplied.push_back(tApplied);
		m_mDirtyPlayers[iIndex] = pPlayer;

		if (bLocal)
			pLocal = pPlayer, tLocalFrame = tFrame;
	}

	m_bRendering = true;
	if (pLocal)
		RebuildFakeAngle(pLocal, tLocalFrame);

	// bones cached before this point were built from the simulated state, rebuild them from what we draw
	InvalidateAllBones();
}

void CAnimInterp::RenderEnd()
{
	Restore();
}

void CAnimInterp::Restore()
{
	const bool bInvalidate = std::exchange(m_bRendering, false) || !m_vApplied.empty();
	if (std::exchange(m_bFakeApplied, false))
		memcpy(F::FakeAngle.aBones, m_aOriginalFakeBones, sizeof(m_aOriginalFakeBones));

	for (auto& tApplied : m_vApplied)
	{
		auto pClient = I::ClientEntityList->GetClientEntity(tApplied.m_iIndex);
		if (pClient && pClient->As<CTFPlayer>() == tApplied.m_pPlayer
			&& tApplied.m_pPlayer->GetModel() == tApplied.m_tTrue.m_pModel)
		{
			Apply(tApplied.m_pPlayer, tApplied.m_tTrue);
			m_mDirtyPlayers[tApplied.m_iIndex] = tApplied.m_pPlayer;
		}
	}
	m_vApplied.clear();

	// don't let smoothed or transitioned render bones leak into aimbot, backtrack or hitbox code
	if (bInvalidate)
		InvalidateAllBones();
}

void CAnimInterp::Reset()
{
	m_vApplied.clear(); // entities are going away, don't touch them
	m_mTimelines.clear();
	m_mDirtyPlayers.clear();
	m_bFakeApplied = false;
	m_bRendering = m_bBuildingFake = false;
	m_flLastRender = 0.f;
}

bool CAnimInterp::ShouldTransition(CBaseEntity* pEntity)
{
	if (!m_bRendering || m_bBuildingFake || !Enabled() || F::Backtrack.IsSettingUpBones())
		return false;

	if (Vars::Visuals::Removals::Interpolation.Value && pEntity)
	{	// respect "remove interpolation" for other players
		const int iIndex = pEntity->entindex();
		if (iIndex > 0 && iIndex <= I::GlobalVars->maxClients && iIndex != I::EngineClient->GetLocalPlayer())
			return false;
	}

	return true;
}
