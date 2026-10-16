#include "MovementSimulation.h"
#include "MovementImpact.h"
#include "PredictionPolicy.h"
#include "../../Aimbot/ProjectilePerformancePolicy.h"
#include "LedgePrediction.h"
#include "MovementInputPolicy.h"
#include "../../NeuralMovement/NeuralMovement.h"
#include "../../Aimbot/AimbotGlobal/AimbotGlobal.h"
#include "../../Aimbot/ProjectileDiagnostics.h"

#include "../../EnginePrediction/EnginePrediction.h"
#include "../../LearningAccess.h"
#include <numeric>
#include <unordered_set>

void CMovementSimulation::BeginReuse(CBaseEntity* target)
{
    m_pReuseTarget=target;
    m_nReuseCount=m_nReuseCursor=m_nReuseBytes=0;
    m_bReuseChecked=m_bReuseAllowed=m_bReuseDisabled=m_bReuseValidation=false;
    m_iReuseTick=I::GlobalVars->tickcount;
    m_flReuseInterval=TICK_INTERVAL;
    m_nReuseHandle=target?target->As<IHandleEntity>()->GetRefEHandle().ToInt():0;
    m_pReuseMap=nullptr;
}

void CMovementSimulation::EndReuse()
{
    m_pReuseTarget=nullptr;
    m_vReuseFrames.resize(m_nReuseCount); // Release unused snapshots retained from larger targets.
    m_nReuseCount=0; // Storage capacity may be retained; no state is reused outside this scope.
    m_bReuseAllowed=false;
}

bool CMovementSimulation::PackReuse(MoveStorage& storage,std::vector<byte>& packed)
{
    const auto size=storage.m_pPlayer->GetIntermediateDataSize();
    auto* map=storage.m_pPlayer->GetPredDescMap();
    if(!map || !size || size>65536) return false;
    packed.resize(size);
    std::fill(packed.begin(),packed.end(),byte{});
    CPredictionCopy copy={PC_EVERYTHING,packed.data(),PC_DATA_PACKED,storage.m_pPlayer,PC_DATA_NORMAL};
    copy.TransferData("MovementReuseCapture",storage.m_pPlayer->entindex(),map);
    return true;
}

// Exclude the restore buffer, original command and path container: their ownership
// stays with the current live simulation. All prediction scalars are copied/checked.
#define REUSE_FIELDS(X) \
    X(m_pPlayer) X(m_flAverageYaw) X(m_flCounterTime) X(m_vObservedWish) X(m_bObservedWish) \
    X(m_vDiagnosticStartOrigin) X(m_vDiagnosticStartVelocity) X(m_bDiagnosticStartGrounded) \
    X(m_bLedgeAware) X(m_bLedgeBraking) X(m_bLedgeImpulse) X(m_iLedgeLanding) X(m_flLedgeStartTime) X(m_bBunnyHop) \
    X(m_flSimTime) X(m_flPredictedDelta) X(m_flPredictedSimTime) X(m_flDiagnosticNetworkOriginTime) \
    X(m_bDirectMove) X(m_bPredictNetworked) X(m_vPredictedOrigin) \
    X(m_bFailed) X(m_bInitFailed) X(m_bInitialized) \
    X(m_bLearnedTiming) X(m_flTimingFirst) X(m_flTimingLeg) X(m_iTimingSign) X(m_flTimingBraking) \
    X(m_bLedgeEvidence) X(m_bLedgeHold) X(m_bLedgeStopped) X(m_bLedgeSeen) X(m_bLedgeAheadSupport) X(m_iLedgeProbeAge) X(m_vLedgeProbeDirection)

bool CMovementSimulation::SameReuseState(const MoveStorage& a,const MoveStorage& b)
{
#define CHECK_REUSE(field) if(a.field!=b.field) return false;
    REUSE_FIELDS(CHECK_REUSE)
#undef CHECK_REUSE
    // Value comparisons deliberately ignore padding (including bitfield padding).
#define CHECK_MOVE(field) if(a.m_MoveData.field!=b.m_MoveData.field) return false;
    CHECK_MOVE(m_bFirstRunOfFunctions) CHECK_MOVE(m_bGameCodeMovedPlayer) CHECK_MOVE(m_nPlayerHandle)
    CHECK_MOVE(m_nImpulseCommand) CHECK_MOVE(m_vecViewAngles) CHECK_MOVE(m_vecAbsViewAngles)
    CHECK_MOVE(m_nButtons) CHECK_MOVE(m_nOldButtons) CHECK_MOVE(m_flForwardMove) CHECK_MOVE(m_flOldForwardMove)
    CHECK_MOVE(m_flSideMove) CHECK_MOVE(m_flUpMove) CHECK_MOVE(m_flMaxSpeed) CHECK_MOVE(m_flClientMaxSpeed)
    CHECK_MOVE(m_vecVelocity) CHECK_MOVE(m_vecAngles) CHECK_MOVE(m_vecOldAngles) CHECK_MOVE(m_outStepHeight)
    CHECK_MOVE(m_outWishVel) CHECK_MOVE(m_outJumpVel) CHECK_MOVE(m_vecConstraintCenter)
    CHECK_MOVE(m_flConstraintRadius) CHECK_MOVE(m_flConstraintWidth) CHECK_MOVE(m_flConstraintSpeedFactor)
    CHECK_MOVE(m_vecAbsOrigin)
#undef CHECK_MOVE
#define CHECK_COUNTER(field) if(a.m_CounterStrafe.field!=b.m_CounterStrafe.field) return false;
    CHECK_COUNTER(valid) CHECK_COUNTER(ax) CHECK_COUNTER(ay) CHECK_COUNTER(center) CHECK_COUNTER(drift)
    CHECK_COUNTER(speed) CHECK_COUNTER(confidence) CHECK_COUNTER(driftLimit) CHECK_COUNTER(kind) CHECK_COUNTER(repeated) CHECK_COUNTER(response) CHECK_COUNTER(along)
#undef CHECK_COUNTER
    return true;
}

void CMovementSimulation::CopyReuseState(MoveStorage& destination,const MoveStorage& source)
{
#define COPY_REUSE(field) destination.field=source.field;
    REUSE_FIELDS(COPY_REUSE)
#undef COPY_REUSE
    destination.m_MoveData=source.m_MoveData;
    destination.m_CounterStrafe=source.m_CounterStrafe;
}
#undef REUSE_FIELDS

bool CMovementSimulation::TryReuseTick(MoveStorage& storage,bool path,RunTickCallback* callback)
{
    if(!m_pReuseTarget || storage.m_pPlayer!=m_pReuseTarget || m_bReuseDisabled || !path || callback || LandingReplay::active)
        return false;
    auto* capture=ProjectileDiagnostics::current;
    if(m_iReuseTick!=I::GlobalVars->tickcount || m_flReuseInterval!=TICK_INTERVAL
        || m_nReuseHandle!=storage.m_pPlayer->As<IHandleEntity>()->GetRefEHandle().ToInt()
        || (m_pReuseMap && m_pReuseMap!=storage.m_pPlayer->GetPredDescMap()))
    {m_bReuseDisabled=true;return false;}
    if(!m_bReuseChecked)
    {
        ProjectileDiagnostics::Profile timer(ProjectileDiagnostics::MovementReuseCapture);
        m_bReuseChecked=true;
        const Vec3 absoluteOrigin=storage.m_pPlayer->GetAbsOrigin();
        if(!PackReuse(storage,m_vReuseScratch)) {m_bReuseDisabled=true;return false;}
        auto* map=storage.m_pPlayer->GetPredDescMap();
        if(m_nReuseCount && (m_pReuseMap!=map || absoluteOrigin!=m_vReuseInitialAbs || m_vReuseScratch!=m_vReuseInitial || !SameReuseState(storage,m_ReuseInitial)))
        {
            // A changed initialization starts a fresh trajectory, never an approximate match.
            m_nReuseCount=m_nReuseBytes=0;
            if(capture) ++ProjectileDiagnostics::reuse.invalidations;
        }
        if(!m_nReuseCount)
        {
            m_vReuseInitial=m_vReuseScratch;
            m_vReuseInitialAbs=absoluteOrigin;
            CopyReuseState(m_ReuseInitial,storage);
            m_pReuseMap=map;
        }
        m_bReuseAllowed=true;
    }
    if(!m_bReuseAllowed) return false;
    if(m_nReuseCursor && m_nReuseCursor<=m_nReuseCount)
    {
        ProjectileDiagnostics::Profile timer(ProjectileDiagnostics::MovementReuseCapture);
        if(storage.m_pPlayer->GetAbsOrigin()!=m_vReuseFrames[m_nReuseCursor-1].absoluteOrigin || !PackReuse(storage,m_vReuseScratch)
            || m_vReuseScratch!=m_vReuseFrames[m_nReuseCursor-1].packed
            || !SameReuseState(storage,m_vReuseFrames[m_nReuseCursor-1].state))
        {
            m_bReuseDisabled=true;
            if(capture) ++ProjectileDiagnostics::reuse.invalidations;
            return false;
        }
    }
    if(m_nReuseCursor>=m_nReuseCount) return false;
    // On sampled commands compare selected cached ticks with genuine fresh simulation.
    // The fresh result remains authoritative; a mismatch disables reuse for this scope.
    if(capture && m_nReuseCursor%32==0)
    {m_bReuseValidation=true;return false;}
    ProjectileDiagnostics::Profile timer(ProjectileDiagnostics::MovementReuseReplay);
    auto& frame=m_vReuseFrames[m_nReuseCursor++];
    if (!PredictionRestore::Restore(storage.m_pPlayer,storage.m_RestoreSnapshot,frame.packed.data(),"MovementReuseReplay"))
    { storage.m_bFailed=true; m_bReuseDisabled=true; return true; }
    CopyReuseState(storage,frame.state);
    if(storage.m_bLedgeSeen) m_bLedgeEncountered=true; // replayed ticks still reached the edge
    storage.m_pPlayer->SetAbsOrigin(frame.absoluteOrigin);
    storage.m_vPath.push_back(frame.pathPoint);
    if(capture) ++ProjectileDiagnostics::reuse.hits;
    return true;
}

void CMovementSimulation::CaptureReuseTick(MoveStorage& storage,bool path,RunTickCallback* callback)
{
    if(MovementImpact::Active() || !m_pReuseTarget || storage.m_pPlayer!=m_pReuseTarget || !m_bReuseAllowed || m_bReuseDisabled || !path || callback || LandingReplay::active)
        return;
    ProjectileDiagnostics::Profile timer(ProjectileDiagnostics::MovementReuseCapture);
    auto* capture=ProjectileDiagnostics::current;
    if(m_bReuseValidation)
    {
        m_bReuseValidation=false;
        const bool matched=storage.m_pPlayer->GetAbsOrigin()==m_vReuseFrames[m_nReuseCursor].absoluteOrigin && PackReuse(storage,m_vReuseScratch)
            && m_vReuseScratch==m_vReuseFrames[m_nReuseCursor].packed
            && SameReuseState(storage,m_vReuseFrames[m_nReuseCursor].state);
        if(capture) {++ProjectileDiagnostics::reuse.validations;if(!matched) ++ProjectileDiagnostics::reuse.mismatches;}
        if(!matched) {m_bReuseDisabled=true;return;}
        ++m_nReuseCursor;
        return;
    }
    if(m_nReuseCursor!=m_nReuseCount) return;
    if(m_nReuseCount>=512) {m_bReuseAllowed=false;return;}
    const size_t retained=m_nReuseCount<m_vReuseFrames.size()?m_vReuseFrames[m_nReuseCount].packed.capacity():0;
    const size_t bytes=std::max(size_t(storage.m_pPlayer->GetIntermediateDataSize()),retained)+sizeof(ReuseFrame);
    if(bytes>4*1024*1024 || m_nReuseBytes>4*1024*1024-bytes) {m_bReuseAllowed=false;return;}
    if(m_vReuseFrames.size()<=m_nReuseCount) m_vReuseFrames.emplace_back();
    auto& frame=m_vReuseFrames[m_nReuseCount];
    frame.absoluteOrigin=storage.m_pPlayer->GetAbsOrigin();
    if(!PackReuse(storage,frame.packed)) {m_bReuseDisabled=true;return;}
    CopyReuseState(frame.state,storage);
    frame.pathPoint=storage.m_MoveData.m_vecAbsOrigin;
    ++m_nReuseCount;++m_nReuseCursor;m_nReuseBytes+=bytes;
    if(capture) {++ProjectileDiagnostics::reuse.captures;ProjectileDiagnostics::reuse.bytes+=bytes;}
}

bool CMovementSimulation::Store(MoveStorage& tMoveStorage)
{
    if (!tMoveStorage.m_RestoreSnapshot.Capture(tMoveStorage.m_pPlayer)) return false;
	auto pMap = tMoveStorage.m_RestoreSnapshot.map;
	size_t iSize = tMoveStorage.m_RestoreSnapshot.size;
	if (!iSize) return false;
	tMoveStorage.m_pData = reinterpret_cast<byte*>(I::MemAlloc->Alloc(iSize));
	if (!tMoveStorage.m_pData) return false;

	CPredictionCopy copy = { PC_EVERYTHING, tMoveStorage.m_pData, PC_DATA_PACKED, tMoveStorage.m_pPlayer, PC_DATA_NORMAL };
	copy.TransferData("MovementSimulationStore", tMoveStorage.m_pPlayer->entindex(), pMap);
	return true;
}

void CMovementSimulation::Reset(MoveStorage& tMoveStorage)
{
	if (tMoveStorage.m_pData)
	{
        if (!PredictionRestore::Restore(tMoveStorage.m_pPlayer,tMoveStorage.m_RestoreSnapshot,
            tMoveStorage.m_pData,"MovementSimulationReset")) tMoveStorage.m_bFailed=true;

		I::MemAlloc->Free(tMoveStorage.m_pData);
		tMoveStorage.m_pData = nullptr;
	}
}

static inline int GetMoveMode(CTFPlayer* pPlayer)
{
	if (pPlayer->IsSwimming())
		return MoveEnum::Swim;

	if (pPlayer->IsOnGround())
		return MoveEnum::Ground;

	return MoveEnum::Air;
}

static inline void HandleMovement(CTFPlayer* pPlayer, MoveData* pLastRecord, MoveData& tCurRecord, std::deque<MoveData>& vRecords)
{
	bool bLocal = pPlayer->entindex() == I::EngineClient->GetLocalPlayer();

	if (pLastRecord && !pPlayer->IsDormant())
	{
		/*
		if (tRecord.m_iMode != pLastRecord->m_iMode)
		{
			pLastRecord = nullptr;
			vRecords.clear();
		}
		else */
		{	// does this eat up fps? i can't tell currently
			CGameTrace trace = {};
			CTraceFilterWorldAndPropsOnly filter = {};
			filter.pSkip = pPlayer;

			SDK::TraceHull(pLastRecord->m_vOrigin, pLastRecord->m_vOrigin + pLastRecord->m_vVelocity * TICK_INTERVAL, pPlayer->m_vecMins() + PLAYER_ORIGIN_COMPRESSION, pPlayer->m_vecMaxs() - PLAYER_ORIGIN_COMPRESSION, pPlayer->SolidMask(), &filter, &trace);
			if (trace.DidHit() && trace.plane.normal.z < 0.707f)
			{
				pLastRecord = nullptr;
				vRecords.resize(1);
			}
		}
	}

	if (pPlayer->InCond(TF_COND_SHIELD_CHARGE))
	{
		G::DummyCmd.forwardmove = 520.f;
		G::DummyCmd.sidemove = 0.f;
		SDK::FixMovement(&G::DummyCmd, bLocal ? G::CurrentUserCmd->viewangles : pPlayer->GetEyeAngles(), {});
		tCurRecord.m_vDirection.x = G::DummyCmd.forwardmove;
		tCurRecord.m_vDirection.y = -G::DummyCmd.sidemove;
		return;
	}

	tCurRecord.m_bInputDirection = bLocal;
	switch (tCurRecord.m_iMode)
	{
	case MoveEnum::Ground:
		if (!bLocal)
			break;
		else if (Vars::Misc::Movement::AutoStrafe.Value || Vars::Misc::Movement::Bunnyhop.Value && G::OriginalCmd.buttons & IN_JUMP)
			tCurRecord.m_vDirection = tCurRecord.m_vVelocity.Normalized2D() * 520.f, tCurRecord.m_bInputDirection = false;
		break;
	case MoveEnum::Air:
		if (!bLocal)
			break;
		else if (Vars::Misc::Movement::AutoStrafe.Value || tCurRecord.m_vDirection.To2D().IsZero() || pLastRecord && tCurRecord.m_vVelocity.To2D() == pLastRecord->m_vVelocity.To2D())
			tCurRecord.m_vDirection = tCurRecord.m_vVelocity.Normalized2D() * 520.f, tCurRecord.m_bInputDirection = false;
		break;
	case MoveEnum::Swim:
		if (!bLocal)
			tCurRecord.m_vDirection = tCurRecord.m_vVelocity.Normalized() * 520.f, tCurRecord.m_bInputDirection = false;
	}
}

void CMovementSimulation::Clear()
{
    m_mRecords.clear(); m_mCounterRecords.clear(); m_mSimTimes.clear(); m_mIdentities.clear(); m_mMotionReceipts.clear();
    m_bReuseDisabled=true; // Clear also invalidates an active same-target reuse scope.
}

void CMovementSimulation::StoreCounterHistory(int index,bool continuing)
{
    const auto& records=m_mRecords[index]; auto& history=m_mCounterRecords[index];
    // Identity, mode and discontinuity resets clear the pattern history. The
    // ordinary history's wall-contact trim does not: a strafe leg that touches a
    // wall is still part of the rhythm (Detect has its own corridor checks).
    if(!continuing) history.clear();
    if(records.empty()) return;
    history.push_front(records.front());
    while(history.size()>160 || (!history.empty() && history.front().m_flSimTime-history.back().m_flSimTime>CounterStrafe::HistorySeconds)) history.pop_back();
}

CounterStrafe::Audit CMovementSimulation::DiagnosticCounterAudit(CTFPlayer* player)
{
    CounterStrafe::Audit audit;
    const auto found=m_mCounterRecords.find(player->entindex());
    if(found==m_mCounterRecords.end() || found->second.empty()) return audit;
    const auto& records=found->second;
    if(records.front().m_iMode!=MoveEnum::Ground || player->m_flSimulationTime()-records.front().m_flSimTime>.1f) return audit;
    std::vector<CounterStrafe::Sample> samples;
    for(const auto& record:records) {
        if(record.m_iMode!=MoveEnum::Ground || records.front().m_flSimTime-record.m_flSimTime>CounterStrafe::HistorySeconds) break;
        samples.push_back({record.m_flSimTime,record.m_vOrigin.x,record.m_vOrigin.y,record.m_vVelocity.x,record.m_vVelocity.y});
    }
    std::reverse(samples.begin(),samples.end());
    CounterStrafe::Detect(samples,&audit,true,true,TICK_INTERVAL);
    return audit;
}

bool CMovementSimulation::HistoryCurrent(CTFPlayer* player) const
{
    const int index=player->entindex();
    const auto identity=m_mIdentities.find(index);
    const auto records=m_mRecords.find(index);
    return identity!=m_mIdentities.end()
        && identity->second.handle==player->As<IHandleEntity>()->GetRefEHandle().ToInt()
        && identity->second.spawn==player->m_iSpawnCounter()
        && identity->second.server==I::ClientState->m_nServerCount
        && records!=m_mRecords.end() && !records->second.empty()
        && records->second.front().m_flSimTime==player->m_flSimulationTime()
        && records->second.front().m_iMode==GetMoveMode(player);
}

bool CMovementSimulation::PatternHistoryFresh(CTFPlayer* player) const
{
    const auto found=m_mMotionReceipts.find(player->entindex());
    if(found==m_mMotionReceipts.end() || !found->second.valid || !HistoryCurrent(player)) return false;
    const float age=std::chrono::duration<float>(std::chrono::steady_clock::now()-found->second.received).count();
    // Receipt age, not simulation time: unchanged/stale packets cannot keep a
    // remembered oscillation alive. Do not change legacy ordinary history rules.
    return age>=0.f && age<=.2f;
}

bool CMovementSimulation::AcceptRecord(CTFPlayer* player, float time, const Vec3& origin, const Vec3& velocity)
{
    const int index = player->entindex();
    const HistoryIdentity identity{static_cast<unsigned long>(player->As<IHandleEntity>()->GetRefEHandle().ToInt()),
        player->m_iSpawnCounter(),I::ClientState->m_nServerCount};
    auto& records = m_mRecords[index];
    const auto clear=[&] { records.clear(); m_mCounterRecords[index].clear(); m_mSimTimes[index].clear(); };
    if (!m_mIdentities.contains(index) || m_mIdentities[index]!=identity)
    { clear(); m_mMotionReceipts.erase(index); m_mIdentities[index]=identity; }
    if (!std::isfinite(time) || !std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)
        || !std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z))
    { clear(); m_mMotionReceipts.erase(index); return false; }
    if (!records.empty())
    {
        const auto& last = records.front();
        if (time == last.m_flSimTime) return false;
        if (PredictionPolicy::Discontinuity(time-last.m_flSimTime, origin.DistTo(last.m_vOrigin),
            std::max(velocity.Length(), last.m_vVelocity.Length())) || GetMoveMode(player) != last.m_iMode)
            clear();
    }
    auto& receipt=m_mMotionReceipts[index];
    const float dt=time-receipt.time;
    // Ground/air mode resets do not erase an observed impulse. This is an
    // evidence guard, not a claim that future buttons or jump type are known.
    const float maxSpeed=std::max(SDK::MaxSpeed(player),1.f);
    const bool sudden=receipt.valid && dt>0.f && dt<=.12f
        && (velocity-receipt.velocity).Length2D()>maxSpeed*14.f*dt+80.f;
    receipt.impulse=sudden || (receipt.valid && dt>0.f && dt<=.12f && velocity.z-receipt.velocity.z>350.f)
        || player->InCond(TF_COND_BLASTJUMPING) || velocity.Length2D()>maxSpeed*1.3f
        || player->InCond(TF_COND_LOST_FOOTING) || player->InCond(TF_COND_AIR_CURRENT);
    receipt.received=std::chrono::steady_clock::now();receipt.velocity=velocity;receipt.time=time;receipt.valid=true;
    return true;
}

void CMovementSimulation::Store()
{
	if (I::EngineClient->IsPlayingDemo())
		return;

	std::unordered_set<int> present;
	for (auto entity : H::Entities.GetGroup(EntityEnum::PlayerAll)) present.insert(entity->entindex());
	std::erase_if(m_mRecords, [&](const auto& item) { return !present.contains(item.first); });
    std::erase_if(m_mCounterRecords, [&](const auto& item) { return !present.contains(item.first); });
	std::erase_if(m_mSimTimes, [&](const auto& item) { return !present.contains(item.first); });
	std::erase_if(m_mMotionReceipts, [&](const auto& item) { return !present.contains(item.first); });
	std::erase_if(m_mIdentities, [&](const auto& item) { return !present.contains(item.first); });

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		auto& vRecords = m_mRecords[pPlayer->entindex()];
		const bool keepReversalStops = Vars::Aimbot::Projectile::StrafePrediction.Value & (Vars::Aimbot::Projectile::StrafePredictionEnum::CounterStrafe | Vars::Aimbot::Projectile::StrafePredictionEnum::Snake);
		if (!pPlayer->IsAlive() || pPlayer->IsAGhost() || pPlayer->IsDormant() || (pPlayer->m_vecVelocity().IsZero() && !keepReversalStops))
		{
			vRecords.clear();
            m_mMotionReceipts.erase(pPlayer->entindex());
			m_mCounterRecords[pPlayer->entindex()].clear();
			m_mSimTimes[pPlayer->entindex()].clear();
			continue;
		}
		else if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer() || !H::Entities.GetDeltaTime(pPlayer->entindex()) && !pPlayer->IsDormant())
			continue;

		Vec3 vVelocity = pPlayer->m_vecVelocity();
		Vec3 vOrigin = pPlayer->m_vecOrigin();
		Vec3 vDirection = vVelocity.To2D();
		if (!AcceptRecord(pPlayer, pPlayer->m_flSimulationTime(), vOrigin, vVelocity)) continue;
        const bool continuing=!vRecords.empty();

		MoveData* pLastRecord = !vRecords.empty() ? &vRecords.front() : nullptr;
		vRecords.emplace_front(
			vDirection,
			pPlayer->m_flSimulationTime(),
			GetMoveMode(pPlayer),
			vVelocity,
			vOrigin
		);
		MoveData& tCurRecord = vRecords.front();
		if (vRecords.size() > 66)
			vRecords.pop_back();

		HandleMovement(pPlayer, pLastRecord, tCurRecord, vRecords);
        StoreCounterHistory(pPlayer->entindex(),continuing);
	}

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		auto& vSimTimes = m_mSimTimes[pPlayer->entindex()];
		if (pEntity->entindex() == I::EngineClient->GetLocalPlayer() || !pPlayer->IsAlive() || pPlayer->IsAGhost() || pPlayer->IsDormant())
		{
			vSimTimes.clear();
			continue;
		}

		float flDeltaTime = H::Entities.GetDeltaTime(pPlayer->entindex());
		if (!flDeltaTime)
			continue;

		vSimTimes.push_front(flDeltaTime);
		if (vSimTimes.size() > Vars::Aimbot::Projectile::DeltaCount.Value)
			vSimTimes.pop_back();
	}
}

void CMovementSimulation::StorePlayer(CTFPlayer* pPlayer, CMoveData& tMoveData, float flTime)
{
	auto& vRecords = m_mRecords[pPlayer->entindex()];
	if (!pPlayer->IsAlive() || pPlayer->IsAGhost() || pPlayer->IsDormant() || pPlayer->m_vecVelocity().IsZero())
	{
		vRecords.clear();
		m_mCounterRecords[pPlayer->entindex()].clear();
		return;
	}

	Vec3 vVelocity = tMoveData.m_vecVelocity;
	Vec3 vOrigin = tMoveData.m_vecAbsOrigin;
	Vec3 vDirection = Math::RotatePoint({ tMoveData.m_flForwardMove, -tMoveData.m_flSideMove, tMoveData.m_flUpMove }, {}, { 0, tMoveData.m_vecViewAngles.y, 0 });
	if (!AcceptRecord(pPlayer, flTime, vOrigin, vVelocity)) return;
    const bool continuing=!vRecords.empty();

	MoveData* pLastRecord = !vRecords.empty() ? &vRecords.front() : nullptr;
	vRecords.emplace_front(
		vDirection,
		flTime,
		GetMoveMode(pPlayer),
		vVelocity,
		vOrigin
	);
	MoveData& tCurRecord = vRecords.front();
	if (vRecords.size() > 66)
		vRecords.pop_back();

	HandleMovement(pPlayer, pLastRecord, tCurRecord, vRecords);
    StoreCounterHistory(pPlayer->entindex(),continuing);
}



bool CMovementSimulation::Initialize(CBaseEntity* pEntity, MoveStorage& tMoveStorage, bool bHitchance, bool bStrafe, bool bPredict)
{
    if(pEntity==m_pReuseTarget) {m_nReuseCursor=0;m_bReuseChecked=m_bReuseAllowed=m_bReuseValidation=false;}
    ProjectileDiagnostics::Profile profile(ProjectileDiagnostics::MovementInit);
	if (tMoveStorage.m_bInitialized) Restore(tMoveStorage);
	tMoveStorage = {};
	if (!pEntity || !pEntity->IsPlayer() || !pEntity->As<CTFPlayer>()->IsAlive())
	{
		tMoveStorage.m_bInitFailed = tMoveStorage.m_bFailed = true;
		return false;
	}

	auto pPlayer = pEntity->As<CTFPlayer>();
	tMoveStorage.m_pPlayer = pPlayer;
    tMoveStorage.m_vDiagnosticStartOrigin=pPlayer->m_vecOrigin();
    tMoveStorage.m_vDiagnosticStartVelocity=pPlayer->m_vecVelocity();
    tMoveStorage.m_bDiagnosticStartGrounded=pPlayer->IsOnGround();


	// store restore data
	if (!Store(tMoveStorage))
	{
		tMoveStorage.m_bInitFailed = tMoveStorage.m_bFailed = true;
		return false;
	}
	tMoveStorage.m_pOriginalCommand = pPlayer->m_pCurrentCommand();
	tMoveStorage.m_bInitialized = true;

	// the hacks that make it work
	pPlayer->m_pCurrentCommand() = &G::DummyCmd;
	pPlayer->m_nPlayerState() = TF_STATE_ACTIVE;
	if (pPlayer->m_MoveType() == MOVETYPE_NONE || pPlayer->m_MoveType() == MOVETYPE_OBSERVER)
		pPlayer->m_MoveType() = MOVETYPE_WALK;

    // History belongs to a full entity lifetime, not merely a reused player index.
    // Never revive pre-respawn/old-mode steering or an unstamped average buffer.
    if (pPlayer->entindex()!=I::EngineClient->GetLocalPlayer() && !HistoryCurrent(pPlayer))
    {
        m_mRecords[pPlayer->entindex()].clear();
        m_mCounterRecords[pPlayer->entindex()].clear();
        m_mSimTimes[pPlayer->entindex()].clear();
    }
    const bool patternAllowed=bStrafe && !NeuralMovement::TargetForced(pPlayer)
        && pPlayer->entindex()!=I::EngineClient->GetLocalPlayer()
        && (Vars::Aimbot::Projectile::StrafePrediction.Value & (Vars::Aimbot::Projectile::StrafePredictionEnum::CounterStrafe
            | Vars::Aimbot::Projectile::StrafePredictionEnum::Snake | Vars::Aimbot::Projectile::StrafePredictionEnum::LedgeAware
            | Vars::Aimbot::Projectile::StrafePredictionEnum::Knockback));
    const bool freshPattern=patternAllowed && PatternHistoryFresh(pPlayer);
    // Starting velocity, independent of which pattern predictors are enabled
    // (enabling Counter/Snake/Ledge used to silently switch this off for every
    // target). The averaged estimate only extends back over records consistent
    // with one velocity, so it follows changes quickly but filters the 1/8-unit
    // origin noise and corrects airborne gravity. A fresh received impulse keeps
    // the raw chord. The ground input fit below still overrides this when valid.
    if (HistoryCurrent(pPlayer) && m_mRecords[pPlayer->entindex()].size()>=2)
    {
        const bool impulse=PatternHistoryFresh(pPlayer) && m_mMotionReceipts[pPlayer->entindex()].impulse;
        if (!impulse)
            if (auto pAvgVelocity=H::Entities.GetAvgVelocity(pPlayer->entindex()); pAvgVelocity
                && std::isfinite(pAvgVelocity->x) && std::isfinite(pAvgVelocity->y) && std::isfinite(pAvgVelocity->z))
                pPlayer->m_vecVelocity()=*pAvgVelocity;
    }

	pPlayer->m_bDucked() = pPlayer->IsDucking() && !pPlayer->IsDormant();
	pPlayer->m_fFlags() &= ~FL_DUCKING; // breaks origin's z if FL_DUCKING is not removed
	pPlayer->m_flDucktime() = 0.f;
	pPlayer->m_flDuckJumpTime() = 0.f;
	pPlayer->m_bDucking() = false;
	pPlayer->m_bInDuckJump() = false;

	if (pPlayer->entindex() != I::EngineClient->GetLocalPlayer())
	{
		pPlayer->m_vecBaseVelocity() = Vec3(); // residual basevelocity causes issues
		if (pPlayer->IsOnGround() && !pPlayer->IsDormant() && !(freshPattern && m_mMotionReceipts[pPlayer->entindex()].impulse))
			pPlayer->m_vecVelocity().z = std::min(pPlayer->m_vecVelocity().z, 0.f); // step fix
		else
			pPlayer->m_hGroundEntity() = nullptr; // fix for velocity.z being set to 0 even if in air
	}
	else if (Vars::Misc::Movement::Bunnyhop.Value && G::OriginalCmd.buttons & IN_JUMP)
		tMoveStorage.m_bBunnyHop = true;

	// setup move data
	SetupMoveData(tMoveStorage,freshPattern && !m_mMotionReceipts[pPlayer->entindex()].impulse
        && (Vars::Aimbot::Projectile::StrafePrediction.Value & (Vars::Aimbot::Projectile::StrafePredictionEnum::CounterStrafe
            | Vars::Aimbot::Projectile::StrafePredictionEnum::Snake)));
	if (CheckStuck(tMoveStorage))
	{
		tMoveStorage.m_bFailed = true;
		return true;
	}

	// calculate strafe if desired
    tMoveStorage.m_bLedgeAware=freshPattern
        && (Vars::Aimbot::Projectile::StrafePrediction.Value & Vars::Aimbot::Projectile::StrafePredictionEnum::LedgeAware)
        && !pPlayer->IsSwimming() && !pPlayer->InCond(TF_COND_SHIELD_CHARGE);
    tMoveStorage.m_bLedgeImpulse=freshPattern && m_mMotionReceipts[pPlayer->entindex()].impulse;
    tMoveStorage.m_flLedgeStartTime=tMoveStorage.m_flSimTime;
    if(tMoveStorage.m_bLedgeAware) {
        tMoveStorage.m_bLedgeEvidence=LedgeEvidence(pPlayer);
        tMoveStorage.m_bLedgeHold=LedgePrediction::HoldHypothesis(tMoveStorage.m_bLedgeEvidence,m_iLedgeHypothesis!=0);
    }
	if (!StrafePrediction(tMoveStorage, bStrafe, bHitchance))
	{
		tMoveStorage.m_bFailed = true;
		Restore(tMoveStorage);
		return false;
	}

	tMoveStorage.m_vPath = { tMoveStorage.m_MoveData.m_vecAbsOrigin };
	if (bPredict)
	{
        for (int i=0, count=H::Entities.GetChoke(pPlayer->entindex()); i<count; ++i)
        {
            if (!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Prediction))
            { tMoveStorage.m_bFailed=true; Restore(tMoveStorage); return false; }
            RunTick(tMoveStorage);
            if (tMoveStorage.m_bFailed) { Restore(tMoveStorage); return false; }
        }
	}

	return true;
}

static MovementInputPolicy::Physics MovementPhysics(CTFPlayer* player,float maxSpeed,bool ground)
{
    static auto friction=H::ConVars.FindVar("sv_friction");
    static auto stop=H::ConVars.FindVar("sv_stopspeed");
    static auto accelerate=H::ConVars.FindVar("sv_accelerate");
    static auto airAccelerate=H::ConVars.FindVar("sv_airaccelerate");
    if(!friction || !stop || !accelerate || !airAccelerate) return {};
    return {TICK_INTERVAL,maxSpeed,friction->GetFloat(),stop->GetFloat(),
        (ground?accelerate:airAccelerate)->GetFloat(),player->m_surfaceFriction(),player->InCond(TF_COND_LOST_FOOTING)};
}

// World-space wish expressed in the CURRENT view frame (view yaw unchanged).
static void SetWorldWish(CMoveData& move,MovementInputPolicy::Vector wish)
{
    Vec3 forward,right;
    Math::AngleVectors({0,move.m_vecViewAngles.y,0},&forward,&right);
    move.m_flForwardMove=wish.x*forward.x+wish.y*forward.y;
    move.m_flSideMove=wish.x*right.x+wish.y*right.y;
}

// Remote view angles are unknown, so the simulated view faces the wish. A view
// fixed elsewhere would make native WalkMove apply its backpedal clamp
// (tf_clamp_back_speed 0.9) to every reversed counter-strafe leg.
static void ApplyMovementInput(CMoveData& move,MovementInputPolicy::Vector wish)
{
    const float speed=std::hypot(wish.x,wish.y);
    if(!std::isfinite(speed) || speed<.001f) {move.m_flForwardMove=move.m_flSideMove=0.f;return;}
    move.m_vecViewAngles.y=Math::VectorAngles(Vec3(wish.x,wish.y,0.f)).y;
    move.m_flForwardMove=speed;move.m_flSideMove=0.f;
}

bool CMovementSimulation::LedgeEvidence(CTFPlayer* player)
{
    // Received slowdown or a sharp turn shortly before now: the player is
    // reacting (to an edge, or anything). Only consulted once the simulation
    // actually reaches a drop, where it decides the primary ledge hypothesis.
    const auto found=m_mRecords.find(player->entindex());
    if(found==m_mRecords.end() || found->second.size()<2) return false;
    const auto& records=found->second;
    const auto& now=records.front();
    if(now.m_iMode!=MoveEnum::Ground) return false;
    const MoveData* best=nullptr;float bestScore=1e30f;
    for(size_t i=1;i<records.size();++i) {
        const float age=now.m_flSimTime-records[i].m_flSimTime;
        if(age>.25f || records[i].m_iMode!=MoveEnum::Ground) break;
        if(age<.06f) continue;
        const float score=std::fabs(age-.12f);
        if(score<bestScore) {bestScore=score;best=&records[i];}
    }
    if(!best) return false;
    const float speed=now.m_vVelocity.Length2D(),previous=best->m_vVelocity.Length2D();
    const float dot=speed>1.f&&previous>1.f?now.m_vVelocity.Normalized2D().Dot(best->m_vVelocity.Normalized2D()):1.f;
    return LedgePrediction::Evidence(speed,previous,dot,now.m_flSimTime-best->m_flSimTime);
}

void CMovementSimulation::SetupMoveData(MoveStorage& tMoveStorage, bool observedPattern)
{
	tMoveStorage.m_MoveData.m_bFirstRunOfFunctions = false;
	tMoveStorage.m_MoveData.m_bGameCodeMovedPlayer = false;
	tMoveStorage.m_MoveData.m_nPlayerHandle = tMoveStorage.m_pPlayer->As<IHandleEntity>()->GetRefEHandle();

	tMoveStorage.m_MoveData.m_vecAbsOrigin = tMoveStorage.m_pPlayer->m_vecOrigin();
	tMoveStorage.m_MoveData.m_vecVelocity = tMoveStorage.m_pPlayer->m_vecVelocity();
	tMoveStorage.m_MoveData.m_flMaxSpeed = SDK::MaxSpeed(tMoveStorage.m_pPlayer);
	tMoveStorage.m_MoveData.m_flClientMaxSpeed = tMoveStorage.m_MoveData.m_flMaxSpeed;

	if (!tMoveStorage.m_MoveData.m_vecVelocity.To2D().IsZero())
	{
		int iIndex = tMoveStorage.m_pPlayer->entindex();
		if (iIndex == I::EngineClient->GetLocalPlayer() && G::CurrentUserCmd)
			tMoveStorage.m_MoveData.m_vecViewAngles = G::CurrentUserCmd->viewangles;
		else
		{
			if (!tMoveStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE))
				tMoveStorage.m_MoveData.m_vecViewAngles = { 0.f, Math::VectorAngles(tMoveStorage.m_MoveData.m_vecVelocity).y, 0.f };
			else
				tMoveStorage.m_MoveData.m_vecViewAngles = H::Entities.GetEyeAngles(iIndex);
		}

		const auto& vRecords = m_mRecords[iIndex];
		if (!vRecords.empty())
		{
			auto& tRecord = vRecords.front();
			if (!tRecord.m_vDirection.IsZero())
			{
                Vec3 direction=tRecord.m_vDirection;
                // A remote ground player who is still gaining speed along a steady
                // heading is holding a key: wish full speed, not the current speed.
                // (The exact input fit below overrides this whenever it succeeds.)
                if (iIndex != I::EngineClient->GetLocalPlayer() && !tRecord.m_bInputDirection && vRecords.size()>=2
                    && !tMoveStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE)
                    && tRecord.m_iMode==MoveEnum::Ground && vRecords[1].m_iMode==MoveEnum::Ground)
                {
                    const auto& older=vRecords[1];
                    const float dt=tRecord.m_flSimTime-older.m_flSimTime;
                    const float speed=tRecord.m_vVelocity.Length2D(),before=older.m_vVelocity.Length2D();
                    const float noise=dt>0.f?2.f*MovementInputPolicy::OriginQuantum/dt:1e30f;
                    if (speed>1.f && before>1.f && speed>before+noise && speed<tMoveStorage.m_MoveData.m_flMaxSpeed-1.f
                        && tRecord.m_vVelocity.Normalized2D().Dot(older.m_vVelocity.Normalized2D())>.95f)
                        direction=tRecord.m_vVelocity.Normalized2D()*tMoveStorage.m_MoveData.m_flMaxSpeed;
                }
				G::DummyCmd.forwardmove = direction.x;
				G::DummyCmd.sidemove = -direction.y;
				G::DummyCmd.upmove = direction.z;
				SDK::FixMovement(&G::DummyCmd, {}, tMoveStorage.m_MoveData.m_vecViewAngles);
				tMoveStorage.m_MoveData.m_flForwardMove = G::DummyCmd.forwardmove;
				tMoveStorage.m_MoveData.m_flSideMove = G::DummyCmd.sidemove;
				tMoveStorage.m_MoveData.m_flUpMove = G::DummyCmd.upmove;
			}
		}
	}

    auto* player=tMoveStorage.m_pPlayer;
    auto& move=tMoveStorage.m_MoveData;
    const auto& history=m_mRecords[player->entindex()];
    if(player->entindex()!=I::EngineClient->GetLocalPlayer() && HistoryCurrent(player)
        && player->IsOnGround() && !player->IsSwimming() && !player->m_bDucked()
        && player->m_MoveType()==MOVETYPE_WALK && !player->InCond(TF_COND_SHIELD_CHARGE)
        && !player->InCond(TF_COND_LOST_FOOTING) && !player->InCond(TF_COND_AIR_CURRENT)
        && history.size()>=2)
    {
        const auto& current=history[0]; const auto& previous=history[1];
        // Ground contacts/knockback/slopes do not uniquely identify input.
        // The enabled pattern path can also fit a short received interval, but
        // must reproduce both its velocity and displacement on clear flat ground.
        if(previous.m_iMode==MoveEnum::Ground && current.m_iMode==MoveEnum::Ground
            && previous.m_vVelocity.z==0.f && current.m_vVelocity.z==0.f
            && std::abs(current.m_vOrigin.z-previous.m_vOrigin.z)<=PLAYER_ORIGIN_COMPRESSION)
        {
            auto inferred=MovementInputPolicy::InferGround(
                {previous.m_vVelocity.x,previous.m_vVelocity.y},{current.m_vVelocity.x,current.m_vVelocity.y},
                current.m_flSimTime-previous.m_flSimTime,MovementPhysics(player,move.m_flMaxSpeed,true));
            if(observedPattern) {
                const auto interval=MovementInputPolicy::InferGroundInterval(
                    {previous.m_vVelocity.x,previous.m_vVelocity.y},{current.m_vVelocity.x,current.m_vVelocity.y},
                    {current.m_vOrigin.x-previous.m_vOrigin.x,current.m_vOrigin.y-previous.m_vOrigin.y},
                    current.m_flSimTime-previous.m_flSimTime,MovementPhysics(player,move.m_flMaxSpeed,true),
                    std::max(.0001f,std::nextafter(current.m_flSimTime,std::numeric_limits<float>::infinity())-current.m_flSimTime));
                if(interval.valid)inferred=interval;
                else observedPattern=false; // Never label an unverified adjacent fallback as observed input.
            }
            if(inferred.valid)
            {
                CTraceFilterWorldAndPropsOnly filter={};filter.pSkip=player;
                CGameTrace route={};
                SDK::TraceHull(previous.m_vOrigin,current.m_vOrigin,player->m_vecMins()+PLAYER_ORIGIN_COMPRESSION,
                    player->m_vecMaxs()-PLAYER_ORIGIN_COMPRESSION,player->SolidMask(),&filter,&route);
                if(!route.DidHit() && !route.startsolid && !route.allsolid)
                {
                    // The inverse was fitted to this exact observation. Applying it
                    // to an older averaged starting speed would reintroduce lag.
                    // A multi-tick fit also gives the native END velocity, which the
                    // received chord (an interval mean) lags behind.
                    move.m_vecVelocity.x=inferred.hasEnd?inferred.end.x:current.m_vVelocity.x;
                    move.m_vecVelocity.y=inferred.hasEnd?inferred.end.y:current.m_vVelocity.y;
                    ApplyMovementInput(move,inferred.wish);
                    if(observedPattern) {
                        tMoveStorage.m_vObservedWish={inferred.wish.x,inferred.wish.y,0};
                        tMoveStorage.m_bObservedWish=true;
                    }
                }
            }
        }
    }

	tMoveStorage.m_MoveData.m_vecAngles = tMoveStorage.m_MoveData.m_vecOldAngles = tMoveStorage.m_MoveData.m_vecViewAngles;
	if (auto pConstraintEntity = tMoveStorage.m_pPlayer->m_hConstraintEntity().Get())
		tMoveStorage.m_MoveData.m_vecConstraintCenter = pConstraintEntity->GetAbsOrigin();
	else
		tMoveStorage.m_MoveData.m_vecConstraintCenter = tMoveStorage.m_pPlayer->m_vecConstraintCenter();
	tMoveStorage.m_MoveData.m_flConstraintRadius = tMoveStorage.m_pPlayer->m_flConstraintRadius();
	tMoveStorage.m_MoveData.m_flConstraintWidth = tMoveStorage.m_pPlayer->m_flConstraintWidth();
	tMoveStorage.m_MoveData.m_flConstraintSpeedFactor = tMoveStorage.m_pPlayer->m_flConstraintSpeedFactor();

	tMoveStorage.m_flPredictedDelta = GetPredictedDelta(tMoveStorage.m_pPlayer);
	tMoveStorage.m_flSimTime = tMoveStorage.m_pPlayer->m_flSimulationTime();
    tMoveStorage.m_flDiagnosticNetworkOriginTime = tMoveStorage.m_flSimTime;
	tMoveStorage.m_flPredictedSimTime = tMoveStorage.m_flSimTime + tMoveStorage.m_flPredictedDelta;
	tMoveStorage.m_vPredictedOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;
	tMoveStorage.m_bDirectMove = tMoveStorage.m_pPlayer->IsOnGround() || tMoveStorage.m_pPlayer->IsSwimming();
}

bool CMovementSimulation::CheckStuck(MoveStorage& tMoveStorage)
{
	auto* oldPlayer = I::GameMovement->player;
	auto* oldTFPlayer = I::GameMovement->m_pTFPlayer;
	auto* oldMove = I::GameMovement->mv;
	PredictionPolicy::ScopeExit restore([&] {
		RestoreBounds(tMoveStorage.m_pPlayer);
		I::GameMovement->player=oldPlayer; I::GameMovement->m_pTFPlayer=oldTFPlayer; I::GameMovement->mv=oldMove;
	});
	I::GameMovement->player = I::GameMovement->m_pTFPlayer = tMoveStorage.m_pPlayer, I::GameMovement->mv = &tMoveStorage.m_MoveData;
	SetBounds(tMoveStorage.m_pPlayer);
	bool bReturn = I::GameMovement->CheckStuck();
	return bReturn;
}

static inline float GetFrictionScale(float flVelocityXY, float flTurn, float flVelocityZ, float flMin = 50.f, float flMax = 150.f)
{
	if (0.f >= flVelocityZ || flVelocityZ > 250.f)
		return 1.f;

	static auto sv_airaccelerate = H::ConVars.FindVar("sv_airaccelerate");
	float flScale = std::max(sv_airaccelerate->GetFloat(), 1.f);
	flMin *= flScale, flMax *= flScale;

	// entity friction will be 0.25f if velocity is between 0.f and 250.f
	return Math::RemapVal(fabsf(flVelocityXY * flTurn), flMin, flMax, 1.f, 0.25f);
}

//#define VISUALIZE_RECORDS
#ifdef VISUALIZE_RECORDS
static inline void VisualizeRecords(MoveData& tRecord1, MoveData& tRecord2, Color_t tColor, float flStraightFuzzyValue)
{
	static int iStaticTickcount = I::GlobalVars->tickcount;
	if (I::GlobalVars->tickcount != iStaticTickcount)
	{
		G::LineStorage.clear();
		iStaticTickcount = I::GlobalVars->tickcount;
	}

	const float flYaw1 = Math::VectorAngles(tRecord1.m_vDirection).y, flYaw2 = Math::VectorAngles(tRecord2.m_vDirection).y;
	const float flTime1 = tRecord1.m_flSimTime, flTime2 = tRecord2.m_flSimTime;
	const int iTicks = std::max(TIME_TO_TICKS(flTime1 - flTime2), 1);
	const float flYaw = Math::NormalizeAngle(flYaw1 - flYaw2);
	const bool bStraight = fabsf(flYaw) / iTicks * tRecord1.m_vVelocity.Length2D() < flStraightFuzzyValue;

	G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(tRecord1.m_vOrigin, tRecord2.m_vOrigin), I::GlobalVars->curtime + 5.f, tColor);
	G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(tRecord1.m_vOrigin, tRecord1.m_vOrigin + Vec3(0, 0, 5)), I::GlobalVars->curtime + 5.f, tColor);
	if (!bStraight && flYaw)
	{
		Vec3 vVelocity = tRecord1.m_vVelocity.Normalized2D() * 5;
		vVelocity = Math::RotatePoint(vVelocity, {}, { 0, flYaw > 0 ? 90.f : -90.f, 0 });
		if (Vars::Aimbot::Projectile::MovesimFrictionFlags.Value & Vars::Aimbot::Projectile::MovesimFrictionFlagsEnum::CalculateIncrease && tRecord1.m_iMode == MoveEnum::Air)
			vVelocity /= GetFrictionScale(tRecord1.m_vVelocity.Length2D(), flYaw, tRecord1.m_vVelocity.z + SDK::GetGravity() * TICK_INTERVAL, 0.f, 56.f);
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(tRecord1.m_vOrigin, tRecord1.m_vOrigin + vVelocity), I::GlobalVars->curtime + 5.f, tColor);
	}
}
#endif

static inline bool GetYawDifference(MoveData& tRecord1, MoveData& tRecord2, bool bStart, float* pYaw, float flStraightFuzzyValue, int iMaxChanges = 0, int iMaxChangeTime = 0, float flMaxSpeed = 0.f)
{
	const float flYaw1 = Math::VectorAngles(tRecord1.m_vDirection).y, flYaw2 = Math::VectorAngles(tRecord2.m_vDirection).y;
	const float flTime1 = tRecord1.m_flSimTime, flTime2 = tRecord2.m_flSimTime;
	const int iTicks = std::max(TIME_TO_TICKS(flTime1 - flTime2), 1);

	*pYaw = Math::NormalizeAngle(flYaw1 - flYaw2);
	if (flMaxSpeed && tRecord1.m_iMode != MoveEnum::Air)
		*pYaw *= std::clamp(tRecord1.m_vVelocity.Length2D() / flMaxSpeed, 0.f, 1.f);
	if (Vars::Aimbot::Projectile::MovesimFrictionFlags.Value & Vars::Aimbot::Projectile::MovesimFrictionFlagsEnum::CalculateIncrease && tRecord1.m_iMode == 1)
		*pYaw /= GetFrictionScale(tRecord1.m_vVelocity.Length2D(), *pYaw, tRecord1.m_vVelocity.z + SDK::GetGravity() * TICK_INTERVAL, 0.f, 56.f);
	if (fabsf(*pYaw) > 45.f)
		return false;

	static int iChanges, iStart;

	static int iStaticSign = 0;
	static bool bStaticZero = false;
	if (bStart) // don't carry state over from a previous calculation
		iStaticSign = 0, bStaticZero = false;

	const int iLastSign = iStaticSign;
	const int iCurrSign = iStaticSign = *pYaw ? sign(*pYaw) : iStaticSign;

	const bool iLastZero = bStaticZero;
	const bool iCurrZero = bStaticZero = !*pYaw;

	const bool bChanged = iCurrSign != iLastSign || iCurrZero && iLastZero;
	// Per-tick turn: *pYaw already spans iTicks, so divide (it used to multiply).
	const bool bStraight = fabsf(*pYaw) / iTicks * tRecord1.m_vVelocity.Length2D() < flStraightFuzzyValue;

	if (bStart)
	{
		iChanges = 0, iStart = TIME_TO_TICKS(flTime1);
		if (bStraight && ++iChanges > iMaxChanges)
			return false;
		return true;
	}
	else
	{
		if ((bChanged || bStraight) && ++iChanges > iMaxChanges)
			return false;
		return iChanges && iStart - TIME_TO_TICKS(flTime2) > iMaxChangeTime ? false : true;
	}
}

void CMovementSimulation::GetAverageYaw(MoveStorage& tMoveStorage, int iSamples)
{
    const auto yawLog=[&](const char* reason,int records,int ticks,float yaw,int minimum){
        if(ProjectileDiagnostics::MovementSample()) ProjectileDiagnostics::Ledge("ground_yaw",std::format("entity={} direct_move={} reason={} available_records={} requested_samples={} ticks={} yaw_per_tick={} minimum={} speed={}",tMoveStorage.m_pPlayer->entindex(),tMoveStorage.m_bDirectMove,reason,records,iSamples,ticks,yaw,minimum,tMoveStorage.m_MoveData.m_vecVelocity.Length2D()));
    };
	auto pPlayer = tMoveStorage.m_pPlayer;
	bool bLocal = pPlayer->entindex() == I::EngineClient->GetLocalPlayer();
	auto& vRecords = m_mRecords[pPlayer->entindex()];
	if (vRecords.empty())
	{
        yawLog("no_records",0,0,0,0);
		return;
	}

	bool bGround = tMoveStorage.m_bDirectMove; int iMinimumStrafes = 4;
	float flMaxSpeed = SDK::MaxSpeed(tMoveStorage.m_pPlayer, false, true);
	float flLowMinimumDistance = bGround ? Vars::Aimbot::Projectile::GroundLowMinimumDistance.Value : Vars::Aimbot::Projectile::AirLowMinimumDistance.Value;
	float flLowMinimumSamples = bGround ? Vars::Aimbot::Projectile::GroundLowMinimumSamples.Value : Vars::Aimbot::Projectile::AirLowMinimumSamples.Value;
	float flHighMinimumDistance = bGround ? Vars::Aimbot::Projectile::GroundHighMinimumDistance.Value : Vars::Aimbot::Projectile::AirHighMinimumDistance.Value;
	float flHighMinimumSamples = bGround ? Vars::Aimbot::Projectile::GroundHighMinimumSamples.Value : Vars::Aimbot::Projectile::AirHighMinimumSamples.Value;

	float flAverageYaw = 0.f; int iTicks = 0, iSkips = 0;
	iSamples = std::min(iSamples, int(vRecords.size()));
	size_t i = 1; for (; i < iSamples; i++)
	{
		auto& tRecord1 = vRecords[i - 1];
		auto& tRecord2 = vRecords[i];
		if (tRecord1.m_iMode != tRecord2.m_iMode)
		{
			break;
		}

		bGround = tRecord1.m_iMode != MoveEnum::Air;
		float flStraightFuzzyValue = bGround ? Vars::Aimbot::Projectile::GroundStraightFuzzyValue.Value : Vars::Aimbot::Projectile::AirStraightFuzzyValue.Value;
		int iMaxChanges = bLocal ? 0 : bGround ? Vars::Aimbot::Projectile::GroundMaxChanges.Value : Vars::Aimbot::Projectile::AirMaxChanges.Value;
		int iMaxChangeTime = bLocal ? 0 : bGround ? Vars::Aimbot::Projectile::GroundMaxChangeTime.Value : Vars::Aimbot::Projectile::AirMaxChangeTime.Value;
		iMinimumStrafes = 4 + iMaxChanges;
#ifdef VISUALIZE_RECORDS
		VisualizeRecords(tRecord1, tRecord2, { 255, 0, 0 }, flStraightFuzzyValue);
#endif

		float flYaw = 0.f;
		bool bResult = GetYawDifference(tRecord1, tRecord2, !iTicks, &flYaw, flStraightFuzzyValue, iMaxChanges, iMaxChangeTime, flMaxSpeed);
		if (Vars::Debug::Logging.Value)
			SDK::Output("GetYawDifference", std::format("{} ({}): {}, {}", i, iTicks, flYaw, bResult).c_str(), { 50, 127, 75 }, Vars::Debug::Logging.Value);
		if (!bResult)
			break;

		flAverageYaw += flYaw;
		iTicks += std::max(TIME_TO_TICKS(tRecord1.m_flSimTime - tRecord2.m_flSimTime), 1);
	}
#ifdef VISUALIZE_RECORDS
	size_t i2 = i; for (; i2 < iSamples; i2++)
	{
		auto& tRecord1 = vRecords[i2 - 1];
		auto& tRecord2 = vRecords[i2];

		float flStraightFuzzyValue = bGround ? Vars::Aimbot::Projectile::GroundStraightFuzzyValue.Value : Vars::Aimbot::Projectile::AirStraightFuzzyValue.Value;
		VisualizeRecords(tRecord1, tRecord2, { 0, 0, 0 }, flStraightFuzzyValue);
	}
	/*
	for (; i2 < vRecords.size(); i2++)
	{
		auto& tRecord1 = vRecords[i2 - 1];
		auto& tRecord2 = vRecords[i2];

		float flStraightFuzzyValue = bGround ? Vars::Aimbot::Projectile::GroundStraightFuzzyValue.Value : Vars::Aimbot::Projectile::AirStraightFuzzyValue.Value;
		VisualizeRecords(tRecord1, tRecord2, { 0, 0, 0, 100 }, flStraightFuzzyValue);
	}
	*/
#endif
	if (i <= size_t(iMinimumStrafes + iSkips)) // valid strafes not high enough
	{
        yawLog("insufficient_consistent_strafes",int(vRecords.size()),iTicks,flAverageYaw,iMinimumStrafes+iSkips);
		return;
	}

	int iMinimum = flLowMinimumSamples;
	if (!bLocal)
	{
		float flDistance = 0.f;
		if (auto pLocal = H::Entities.GetLocal())
			flDistance = pLocal->m_vecOrigin().DistTo(tMoveStorage.m_pPlayer->m_vecOrigin());
		iMinimum = flDistance < flLowMinimumDistance ? flLowMinimumSamples : Math::RemapVal(flDistance, flLowMinimumDistance, flHighMinimumDistance, flLowMinimumSamples + 1, flHighMinimumSamples);
	}

	flAverageYaw /= std::max(iTicks, iMinimum);
	if (fabsf(flAverageYaw) < 0.36f)
	{
        yawLog("below_turn_threshold",int(vRecords.size()),iTicks,flAverageYaw,iMinimum);
		return;
	}

	tMoveStorage.m_flAverageYaw = flAverageYaw;
    yawLog("accepted",int(vRecords.size()),iTicks,flAverageYaw,iMinimum);
	if (Vars::Debug::Logging.Value)
		SDK::Output("MovementSimulation", std::format("flAverageYaw calculated to {} from {} ({}){}", flAverageYaw, iTicks, iMinimum, bLocal ? " (local)" : "").c_str(), { 100, 255, 150 }, Vars::Debug::Logging.Value);
}

bool CMovementSimulation::StrafePrediction(MoveStorage& tMoveStorage, bool bStrafe, bool bHitchance)
{
    bool patternEnded=false; // a recently received strafe pattern just ended
    if(ProjectileDiagnostics::MovementSample()) ProjectileDiagnostics::Ledge("movement_settings",std::format("entity={} strafe={} flags={} grounded={} direct_move={} swimming={} charging={} local={} hitchance_check={} hitchance={} ground_samples={} air_samples={} records={} origin={},{},{} velocity={},{},{}",tMoveStorage.m_pPlayer->entindex(),bStrafe,Vars::Aimbot::Projectile::StrafePrediction.Value,tMoveStorage.m_pPlayer->IsOnGround(),tMoveStorage.m_bDirectMove,tMoveStorage.m_pPlayer->IsSwimming(),tMoveStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE),tMoveStorage.m_pPlayer->entindex()==I::EngineClient->GetLocalPlayer(),bHitchance,Vars::Aimbot::Projectile::HitChance.Value,Vars::Aimbot::Projectile::GroundSamples.Value,Vars::Aimbot::Projectile::AirSamples.Value,m_mRecords[tMoveStorage.m_pPlayer->entindex()].size(),tMoveStorage.m_MoveData.m_vecAbsOrigin.x,tMoveStorage.m_MoveData.m_vecAbsOrigin.y,tMoveStorage.m_MoveData.m_vecAbsOrigin.z,tMoveStorage.m_MoveData.m_vecVelocity.x,tMoveStorage.m_MoveData.m_vecVelocity.y,tMoveStorage.m_MoveData.m_vecVelocity.z));
	if (bStrafe && !NeuralMovement::TargetForced(tMoveStorage.m_pPlayer)
        && PatternHistoryFresh(tMoveStorage.m_pPlayer)
        && !m_mMotionReceipts[tMoveStorage.m_pPlayer->entindex()].impulse
        && (Vars::Aimbot::Projectile::StrafePrediction.Value & (Vars::Aimbot::Projectile::StrafePredictionEnum::CounterStrafe | Vars::Aimbot::Projectile::StrafePredictionEnum::Snake))
		&& tMoveStorage.m_pPlayer->entindex() != I::EngineClient->GetLocalPlayer()
		&& !tMoveStorage.m_pPlayer->IsSwimming() && !tMoveStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE))
	{
		const auto& records=m_mCounterRecords[tMoveStorage.m_pPlayer->entindex()];
		std::vector<CounterStrafe::Sample> samples;
		for(const auto& r:records)
		{
			if(r.m_iMode!=records.front().m_iMode || records.front().m_flSimTime-r.m_flSimTime>CounterStrafe::HistorySeconds) break;
			samples.push_back({r.m_flSimTime,r.m_vOrigin.x,r.m_vOrigin.y,r.m_vVelocity.x,r.m_vVelocity.y});
		}
		std::reverse(samples.begin(),samples.end());
        CounterStrafe::Audit audit;
        const bool logCounter=ProjectileDiagnostics::MovementSample();
		tMoveStorage.m_CounterStrafe=CounterStrafe::Detect(samples,&audit,
            bool(Vars::Aimbot::Projectile::StrafePrediction.Value & Vars::Aimbot::Projectile::StrafePredictionEnum::CounterStrafe),
            bool(Vars::Aimbot::Projectile::StrafePrediction.Value & Vars::Aimbot::Projectile::StrafePredictionEnum::Snake),TICK_INTERVAL);
        if(logCounter) {
            const auto& e=tMoveStorage.m_CounterStrafe;
            ProjectileDiagnostics::Ledge("counter_model",std::format("entity={} extended={} interval={} freshness={} width_limit={} mean_drift={} cycle_drift={} drift_limit={} policy=cycle_drift_bounded_slow_history",tMoveStorage.m_pPlayer->entindex(),audit.extended,audit.interval,audit.freshness,audit.widthLimit,audit.meanDrift,e.drift,e.driftLimit));
            ProjectileDiagnostics::Ledge("counter_history",std::format("entity={} reversal_age={} low={} high={} rejection_mask={} grounded={} current_lateral={} score_kind=directional_balance_not_probability timing_model=center_convergence_not_next_reversal",tMoveStorage.m_pPlayer->entindex(),audit.age,audit.low,audit.high,audit.rejectionMask,tMoveStorage.m_pPlayer->IsOnGround(),samples.empty()?0.f:samples.back().vx*audit.axisX+samples.back().vy*audit.axisY));
            ProjectileDiagnostics::Ledge("counter_detect",std::format("entity={} reason={} samples={} duration={} rapid={} reversals={} positive={} negative={} variance={} corridor_width={} drift={} agreement={} valid={} axis={},{} center={} speed={} confidence={}",tMoveStorage.m_pPlayer->entindex(),audit.reason,samples.size(),audit.duration,audit.rapid,audit.reversals,audit.positive,audit.negative,audit.variance,audit.width,audit.drift,audit.agreement,e.valid,e.ax,e.ay,e.center,e.speed,e.confidence));
        }
        if(!tMoveStorage.m_CounterStrafe.valid &&
            (!std::strcmp(audit.reason,"stopped_pattern") || !std::strcmp(audit.reason,"direction_changed_or_stopped"))) {
            tMoveStorage.m_flAverageYaw=0.f;return true;
        }
        // A pattern that ended (held leg, broke out of its corridor, a single turn
        // that was not repeated) no longer suppresses the ordinary turn predictor
        // for the rest of the 2.1 s window. Its own consistency test only uses the
        // records since the last sign change. The strafing history must not fail
        // the ordinary hit chance windows, so that check is skipped here.
        if(!tMoveStorage.m_CounterStrafe.valid &&
            (!std::strcmp(audit.reason,"outside_corridor") || !std::strcmp(audit.reason,"pattern_ended")
                || !std::strcmp(audit.reason,"single_turn_finished")))
            patternEnded=true;
		if(tMoveStorage.m_CounterStrafe.valid)
		{
			// Use reversal balance instead of the constant-turn consistency test for this model.
			if(bHitchance && tMoveStorage.m_CounterStrafe.confidence < Vars::Aimbot::Projectile::HitChance.Value/100.f)
			{
                if(ProjectileDiagnostics::MovementSample()) ProjectileDiagnostics::Ledge("counter_reject",std::format("entity={} reason=confidence threshold={} confidence={}",tMoveStorage.m_pPlayer->entindex(),Vars::Aimbot::Projectile::HitChance.Value,tMoveStorage.m_CounterStrafe.confidence));
				return false;
			}
			tMoveStorage.m_flAverageYaw=0.f;
            // Learned strafe timing replaces centre convergence for repeated (non-rapid) patterns.
            tMoveStorage.m_bLearnedTiming=false;
            if(const auto& e=tMoveStorage.m_CounterStrafe; m_bAllowLearnedTiming && e.repeated && e.kind!=CounterStrafe::Kind::Single && e.response>.07f) {
                float first=0.f,leg=0.f,dx=0.f,dy=0.f;
                const auto& v=tMoveStorage.m_MoveData.m_vecVelocity;const float lateral=v.x*e.ax+v.y*e.ay-e.drift;
                // The learned leg's direction must agree with the corridor's current lateral direction.
                if(std::fabs(lateral)>=20.f && NeuralMovement::ReversalPlan(tMoveStorage.m_pPlayer,m_iReversalHypothesis,first,leg,dx,dy)
                    && (dx*e.ax+dy*e.ay)*lateral>0.f) {
                    tMoveStorage.m_bLearnedTiming=true;tMoveStorage.m_flTimingFirst=first;tMoveStorage.m_flTimingLeg=leg;
                    // Native braking time from the reversed wish to the zero crossing
                    // (~0.08 s at full speed for every class; not proportional to speed).
                    tMoveStorage.m_flTimingBraking=MovementInputPolicy::ReversalTime(e.speed,
                        MovementPhysics(tMoveStorage.m_pPlayer,std::min(tMoveStorage.m_MoveData.m_flMaxSpeed,tMoveStorage.m_MoveData.m_flClientMaxSpeed),true));
                    tMoveStorage.m_iTimingSign=lateral>0.f?1:-1;m_bReversalPlanned=true;m_flPlannedFirst=first;
                }
            }
			return true;
		}
        // Respond to measured braking before a complete reversal exists.
        // Passive friction remains zero input; neither case invents recurrence.
        if(tMoveStorage.m_bObservedWish
            && tMoveStorage.m_vObservedWish.Dot(tMoveStorage.m_MoveData.m_vecVelocity)<=0.f) {
            tMoveStorage.m_flAverageYaw=0.f;return true;
        }
	}
	if (bStrafe && Vars::Aimbot::Projectile::StrafePrediction.Value & (tMoveStorage.m_bDirectMove ? Vars::Aimbot::Projectile::StrafePredictionEnum::Ground : Vars::Aimbot::Projectile::StrafePredictionEnum::Air))
	{
		const int iStrafeSamples = std::max(2, tMoveStorage.m_bDirectMove
			? Vars::Aimbot::Projectile::GroundSamples.Value
			: Vars::Aimbot::Projectile::AirSamples.Value);

		GetAverageYaw(tMoveStorage, iStrafeSamples);

		// really hope this doesn't work like shit
		if (bHitchance && !patternEnded && !tMoveStorage.m_pPlayer->m_vecVelocity().IsZero() && Vars::Aimbot::Projectile::HitChance.Value)
		{
			const auto& vRecords = m_mRecords[tMoveStorage.m_pPlayer->entindex()];
			const auto iSamples = vRecords.size();

			float flCurrentChance = 1.f, flAverageYaw = 0.f;
			int windows = 0, failed = 0, windowTicks = 0;
			for (size_t i = 0; i + 1 < iSamples; i++)
			{
				const auto& pRecord1 = vRecords[i], & pRecord2 = vRecords[i + 1];
				if (pRecord1.m_iMode != pRecord2.m_iMode) break;
				const float flYaw1 = Math::VectorAngles(pRecord1.m_vDirection).y, flYaw2 = Math::VectorAngles(pRecord2.m_vDirection).y;
				const float flTime1 = pRecord1.m_flSimTime, flTime2 = pRecord2.m_flSimTime;
				const int iTicks = std::max(TIME_TO_TICKS(flTime1 - flTime2), 1);

				float flYaw = Math::NormalizeAngle(flYaw1 - flYaw2) / iTicks;
				if (pRecord1.m_iMode != MoveEnum::Air && tMoveStorage.m_MoveData.m_flMaxSpeed)
					flYaw *= std::clamp(pRecord1.m_vVelocity.Length2D() / tMoveStorage.m_MoveData.m_flMaxSpeed, 0.f, 1.f);
				if (pRecord1.m_iMode == MoveEnum::Air && (Vars::Aimbot::Projectile::MovesimFrictionFlags.Value & Vars::Aimbot::Projectile::MovesimFrictionFlagsEnum::CalculateIncrease))
					flYaw /= GetFrictionScale(pRecord1.m_vVelocity.Length2D(), flYaw * iTicks,
						pRecord1.m_vVelocity.z + SDK::GetGravity() * TICK_INTERVAL, 0.f, 56.f);
				flAverageYaw += flYaw * iTicks;
				windowTicks += iTicks;

				if ((i + 1) % iStrafeSamples == 0 || i + 2 == iSamples)
				{
					flAverageYaw /= std::max(windowTicks, 1);
					++windows;
					if (fabsf(tMoveStorage.m_flAverageYaw - flAverageYaw) > 0.5f)
						++failed;
					flAverageYaw = 0.f;
					windowTicks = 0;
				}
			}

			flCurrentChance = iSamples >= 3 ? PredictionPolicy::Confidence(windows, failed) : 0.f;
            if(ProjectileDiagnostics::MovementSample()) ProjectileDiagnostics::Ledge("ground_confidence",std::format("entity={} direct_move={} samples={} windows={} failed={} confidence={} required={} rejected={}",tMoveStorage.m_pPlayer->entindex(),tMoveStorage.m_bDirectMove,iSamples,windows,failed,flCurrentChance,Vars::Aimbot::Projectile::HitChance.Value/100.f,flCurrentChance<Vars::Aimbot::Projectile::HitChance.Value/100.f));
			if (flCurrentChance < Vars::Aimbot::Projectile::HitChance.Value / 100)
			{
				SDK::Output("MovementSimulation", std::format("Hitchance ({}% < {}%)", flCurrentChance * 100, Vars::Aimbot::Projectile::HitChance.Value).c_str(), { 80, 200, 120 }, Vars::Debug::Logging.Value);
				return false;
			}
		}
	}

	if (!tMoveStorage.m_bDirectMove)
	{
		if (!tMoveStorage.m_flAverageYaw)
			tMoveStorage.m_MoveData.m_flForwardMove = tMoveStorage.m_MoveData.m_flSideMove = 0.f;
		else
		{
			const auto& vRecords = m_mRecords[tMoveStorage.m_pPlayer->entindex()];
			if (!vRecords.empty())
			{
				auto& tRecord = vRecords.front();
				Vec3 vDirection = tRecord.m_vVelocity;
				if (!vDirection.IsZero() && tRecord.m_bInputDirection)
				{	// fix inputs for turning
					int iSign = sign(tMoveStorage.m_flAverageYaw);
					float flForward = tMoveStorage.m_MoveData.m_flForwardMove, flSide = tMoveStorage.m_MoveData.m_flSideMove;
					tMoveStorage.m_MoveData.m_flForwardMove = -flSide * iSign, tMoveStorage.m_MoveData.m_flSideMove = flForward * iSign;
				}
			}
		}
	}

	return true;
}

bool CMovementSimulation::SetDuck(MoveStorage& tMoveStorage, bool bDuck) // this only touches origin, bounds
{
	if (bDuck == tMoveStorage.m_pPlayer->m_bDucked())
		return true;

	auto pGameRules = I::TFGameRules();
	auto pViewVectors = pGameRules ? pGameRules->GetViewVectors() : nullptr;
	float flScale = tMoveStorage.m_pPlayer->m_flModelScale();

	if (!tMoveStorage.m_pPlayer->IsOnGround())
	{
		Vec3 vHullMins = (pViewVectors ? pViewVectors->m_vHullMin : Vec3(-24, -24, 0)) * flScale;
		Vec3 vHullMaxs = (pViewVectors ? pViewVectors->m_vHullMax : Vec3(24, 24, 82)) * flScale;
		Vec3 vDuckHullMins = (pViewVectors ? pViewVectors->m_vDuckHullMin : Vec3(-24, -24, 0)) * flScale;
		Vec3 vDuckHullMaxs = (pViewVectors ? pViewVectors->m_vDuckHullMax : Vec3(24, 24, 62)) * flScale;

		if (bDuck)
			tMoveStorage.m_MoveData.m_vecAbsOrigin += (vHullMaxs - vHullMins) - (vDuckHullMaxs - vDuckHullMins);
		else
		{
			Vec3 vOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin - ((vHullMaxs - vHullMins) - (vDuckHullMaxs - vDuckHullMins));

			CGameTrace trace = {};
			CTraceFilterWorldAndPropsOnly filter = {};
			SDK::TraceHull(vOrigin, vOrigin, vHullMins, vHullMaxs, tMoveStorage.m_pPlayer->SolidMask(), &filter, &trace);
			if (trace.DidHit())
				return false;

			tMoveStorage.m_MoveData.m_vecAbsOrigin = vOrigin;
		}
	}
	tMoveStorage.m_pPlayer->m_bDucked() = bDuck;

	return true;
}

void CMovementSimulation::SetBounds(CTFPlayer* pPlayer)
{
	if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer())
		return;

	// fixes issues with origin compression
	if (auto pGameRules = I::TFGameRules())
	{
		if (auto pViewVectors = pGameRules->GetViewVectors())
		{
			m_vBounds.push_back({ pViewVectors->m_vHullMin, pViewVectors->m_vHullMax, pViewVectors->m_vDuckHullMin, pViewVectors->m_vDuckHullMax });
            // Still push/pop the bounds stack for the nominal diagnostic branch.
            if(!PredictionPolicy::UseCompressedHull(LandingReplay::active,LandingReplay::nominalHull)) return;
			pViewVectors->m_vHullMin = Vec3(-24, -24, 0) + PLAYER_ORIGIN_COMPRESSION;
			pViewVectors->m_vHullMax = Vec3(24, 24, 82) - PLAYER_ORIGIN_COMPRESSION;
			pViewVectors->m_vDuckHullMin = Vec3(-24, -24, 0) + PLAYER_ORIGIN_COMPRESSION;
			pViewVectors->m_vDuckHullMax = Vec3(24, 24, 62) - PLAYER_ORIGIN_COMPRESSION;
		}
	}
}

void CMovementSimulation::RestoreBounds(CTFPlayer* pPlayer)
{
	if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer())
		return;

	if (auto pGameRules = I::TFGameRules())
	{
		if (auto pViewVectors = pGameRules->GetViewVectors())
		{
			if (m_vBounds.empty()) return;
			const auto saved = m_vBounds.back(); m_vBounds.pop_back();
			pViewVectors->m_vHullMin = saved[0]; pViewVectors->m_vHullMax = saved[1];
			pViewVectors->m_vDuckHullMin = saved[2]; pViewVectors->m_vDuckHullMax = saved[3];
		}
	}
}

void CMovementSimulation::PredictLedge(MoveStorage& storage)
{
    storage.m_bLedgeBraking=false;
    if(!storage.m_bLedgeAware) return;
    auto* player=storage.m_pPlayer;auto& move=storage.m_MoveData;
    if(player->IsSwimming() || player->InCond(TF_COND_SHIELD_CHARGE)) return;
    if(!player->IsOnGround()) {
        // The following native ProcessMovement step assesses the actual flight,
        // hull contacts and landing support. Do not replace it with an approximate
        // gravity parabola (which ignores air input, attributes and collisions).
        storage.m_iLedgeLanding=-1;storage.m_bLedgeStopped=false;storage.m_iLedgeProbeAge=0;
        return;
    }
    storage.m_iLedgeLanding=1;
    // Observed external momentum overrides an uncertain edge hold. Native
    // ground categorization already reports support, so no extra trace here.
    if(storage.m_bLedgeImpulse) return;
    const auto physics=MovementPhysics(player,std::min(move.m_flMaxSpeed,move.m_flClientMaxSpeed),true);
    if(!MovementInputPolicy::Valid(physics)) return;
    const float speed=move.m_vecVelocity.Length2D();
    // Active braking toward zero, the way players stop at an edge (counter-
    // strafe), instead of releasing the keys and sliding ~60 units on friction.
    const auto brake=[&] {
        const auto input=MovementInputPolicy::ForVelocity({move.m_vecVelocity.x,move.m_vecVelocity.y},{0.f,0.f},physics,true);
        if(input.valid) SetWorldWish(move,input.wish); else move.m_flForwardMove=move.m_flSideMove=0.f;
        storage.m_bLedgeBraking=true;
    };
    // Held at the edge: stay stopped for the rest of this hypothesis (a counter
    // strafe keeps its own reversals, so it is never latched).
    if(storage.m_bLedgeStopped) {brake();return;}
    if(speed<1.f) return;
    CTraceFilterWorldAndPropsOnly filter={};filter.pSkip=player;
    const auto mins=player->m_vecMins()+PLAYER_ORIGIN_COMPRESSION;
    const auto maxs=player->m_vecMaxs()-PLAYER_ORIGIN_COMPRESSION;
    auto trace=[&](const Vec3& a,const Vec3& b,CGameTrace& result) {
        if(!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Geometry)) {storage.m_bFailed=true;return false;}
        SDK::TraceHull(a,b,mins,maxs,player->SolidMask(),&filter,&result);return true;
    };
    // Probe every other tick: the look-ahead covers the extra tick of travel.
    const float stop=MovementInputPolicy::ActiveStopDistance(speed,physics);
    const float distance=std::clamp(stop+speed*TICK_INTERVAL*2.f+LedgePrediction::EdgeMargin,2.f,96.f);
    bool aheadSupport=storage.m_bLedgeAheadSupport;
    const Vec3 heading=move.m_vecVelocity.Normalized2D();
    // Re-probe at once after a reversal or sharp turn: the cached result faced elsewhere.
    if(storage.m_iLedgeProbeAge>0 && heading.Dot(storage.m_vLedgeProbeDirection)>=.7f) --storage.m_iLedgeProbeAge;
    else {
        storage.m_iLedgeProbeAge=1;storage.m_vLedgeProbeDirection=heading;
        const Vec3 ahead=move.m_vecAbsOrigin+move.m_vecVelocity.Normalized2D()*distance;
        CGameTrace route={};if(!trace(move.m_vecAbsOrigin+Vec3(0,0,2),ahead+Vec3(0,0,2),route))return;
        // A wall ahead is handled by native collision; steps and slopes are support.
        if(route.DidHit()||route.startsolid||route.allsolid) aheadSupport=true;
        else {
            CGameTrace down={};if(!trace(ahead+Vec3(0,0,2),ahead-Vec3(0,0,LedgePrediction::DropDepth),down))return;
            aheadSupport=LedgePrediction::Walkable(down.DidHit(),down.startsolid||down.allsolid,down.plane.normal.z);
        }
        storage.m_bLedgeAheadSupport=aheadSupport;
    }
    if(storage.m_bFailed || aheadSupport) return;
    storage.m_bLedgeSeen=true;m_bLedgeEncountered=true;
    if(!storage.m_bLedgeHold) return; // walk-off hypothesis: native movement goes over the edge
    brake();
    if(!(storage.m_CounterStrafe.valid && storage.m_CounterStrafe.repeated)) storage.m_bLedgeStopped=true;
    if(ProjectileDiagnostics::LedgeSample(2))ProjectileDiagnostics::Ledge("ledge_hold",std::format("entity={} speed={} lookahead={} evidence={} alternate={} policy=active_brake_hold",player->entindex(),speed,distance,storage.m_bLedgeEvidence,m_iLedgeHypothesis));
}

void CMovementSimulation::RunTick(MoveStorage& tMoveStorage, bool bPath, RunTickCallback* pCallback)
{
    ProjectileDiagnostics::Profile profile(ProjectileDiagnostics::MovementTick);
	if (!tMoveStorage.m_bInitialized || tMoveStorage.m_bFailed || !tMoveStorage.m_pPlayer) return;
    if (!tMoveStorage.m_RestoreSnapshot.Matches(tMoveStorage.m_pPlayer))
    { tMoveStorage.m_bFailed=true; return; }

    MovementImpact::AtBoundary(tMoveStorage);
    if(tMoveStorage.m_bFailed)return;
    if(!MovementImpact::Active() && TryReuseTick(tMoveStorage,bPath,pCallback)) return;

	// make sure frametime and prediction vars are right
	const bool oldPrediction = I::Prediction->m_bInPrediction, oldFirst = I::Prediction->m_bFirstTimePredicted;
	const float oldFrametime = I::GlobalVars->frametime;
	auto* oldHost = MoveSimulationHost::Current;
	auto* oldPlayer = I::GameMovement->player;
	auto* oldTFPlayer = I::GameMovement->m_pTFPlayer;
	auto* oldMove = I::GameMovement->mv;
	PredictionPolicy::ScopeExit restore([&] {
		RestoreBounds(tMoveStorage.m_pPlayer);
		I::Prediction->m_bInPrediction=oldPrediction; I::Prediction->m_bFirstTimePredicted=oldFirst;
		I::MoveHelper->SetHost(oldHost);
		I::GlobalVars->frametime=oldFrametime;
		I::GameMovement->player=oldPlayer; I::GameMovement->m_pTFPlayer=oldTFPlayer; I::GameMovement->mv=oldMove;
	});
	I::Prediction->m_bInPrediction = true;
	I::MoveHelper->SetHost(tMoveStorage.m_pPlayer);
	I::Prediction->m_bFirstTimePredicted = false;
	I::GlobalVars->frametime = I::Prediction->m_bEnginePaused ? 0.f : TICK_INTERVAL;
	SetBounds(tMoveStorage.m_pPlayer);

	if (tMoveStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE))
	{
		static auto tf_demoman_charge_drain_time = H::ConVars.FindVar("tf_demoman_charge_drain_time");

		float flDrainTime = SDK::AttribHookValue(tf_demoman_charge_drain_time->GetFloat(), "mod_charge_time", tMoveStorage.m_pPlayer);
		tMoveStorage.m_pPlayer->m_flChargeMeter() -= TICK_INTERVAL * 100.f / flDrainTime;

		if (tMoveStorage.m_pPlayer->m_flChargeMeter() <= 0.f)
		{
			tMoveStorage.m_pPlayer->RemoveCond(TF_COND_SHIELD_CHARGE);
			tMoveStorage.m_MoveData.m_flMaxSpeed = tMoveStorage.m_MoveData.m_flClientMaxSpeed = SDK::MaxSpeed(tMoveStorage.m_pPlayer);
			tMoveStorage.m_pPlayer->m_flMaxspeed() = tMoveStorage.m_MoveData.m_flMaxSpeed;
		}
	}

	float flCorrection = 0.f;
	if (tMoveStorage.m_flAverageYaw)
	{
		float flMult = 1.f;
		if (!tMoveStorage.m_bDirectMove && !tMoveStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE))
		{
			flCorrection = 90.f * sign(tMoveStorage.m_flAverageYaw);
			if (Vars::Aimbot::Projectile::MovesimFrictionFlags.Value & Vars::Aimbot::Projectile::MovesimFrictionFlagsEnum::RunReduce)
				flMult = GetFrictionScale(tMoveStorage.m_MoveData.m_vecVelocity.Length2D(), tMoveStorage.m_flAverageYaw, tMoveStorage.m_MoveData.m_vecVelocity.z + SDK::GetGravity() * TICK_INTERVAL);
		}
		tMoveStorage.m_MoveData.m_vecViewAngles.y += tMoveStorage.m_flAverageYaw * flMult + flCorrection;
	}

	float flOldSpeed = tMoveStorage.m_MoveData.m_flClientMaxSpeed;
	if (tMoveStorage.m_pPlayer->m_bDucked() && tMoveStorage.m_pPlayer->IsOnGround() && !tMoveStorage.m_pPlayer->IsSwimming())
		tMoveStorage.m_MoveData.m_flClientMaxSpeed /= 3;

	if (tMoveStorage.m_bBunnyHop && tMoveStorage.m_pPlayer->IsOnGround() && !tMoveStorage.m_pPlayer->m_bDucked())
	{
		tMoveStorage.m_MoveData.m_nOldButtons = 0;
		tMoveStorage.m_MoveData.m_nButtons |= IN_JUMP;
	}

	if (auto& counter=tMoveStorage.m_CounterStrafe; counter.valid && counter.repeated)
	{
		// Keep the forward trend and vertical physics; converge only the oscillating component.
		auto& move=tMoveStorage.m_MoveData;
		const float elapsed=tMoveStorage.m_flCounterTime;
		const float lateral=move.m_vecVelocity.x*counter.ax+move.m_vecVelocity.y*counter.ay;
		float correction=CounterStrafe::Correction(counter,
			move.m_vecAbsOrigin.x*counter.ax+move.m_vecAbsOrigin.y*counter.ay,lateral,elapsed);
        if(tMoveStorage.m_bLearnedTiming) {
            // Hold the current leg until the learned reversal, then alternate every learned leg.
            // The wish flips early by the braking time so lateral speed crosses zero on time.
            const float braking=tMoveStorage.m_flTimingBraking,flip=tMoveStorage.m_flTimingFirst-braking;
            int sign=tMoveStorage.m_iTimingSign;
            if(elapsed>=flip) {const int flips=1+int((elapsed-flip)/std::max(tMoveStorage.m_flTimingLeg,.05f));if(flips%2)sign=-sign;}
            correction=CounterStrafe::DriftVelocity(counter,elapsed)+float(sign)*counter.speed-lateral;
        }
        if(ProjectileDiagnostics::MovementSample(true)) {
            const auto a=CounterStrafe::Explain(counter,move.m_vecAbsOrigin.x*counter.ax+move.m_vecAbsOrigin.y*counter.ay,lateral,elapsed);
            ProjectileDiagnostics::Ledge("counter_apply",std::format("entity={} elapsed={} lateral_before={} correction={} lateral_desired={} center={} drift={} origin={},{},{} drift_offset={} center_error={} raw_steering={} steering={} blend={} desired={} saturated={} opposes_velocity={} zero_drift_correction={} drift_correction_delta={} comparison=same_state_not_resimulation application=native_wish_input",tMoveStorage.m_pPlayer->entindex(),elapsed,lateral,correction,lateral+correction,counter.center,counter.drift,move.m_vecAbsOrigin.x,move.m_vecAbsOrigin.y,move.m_vecAbsOrigin.z,a.driftOffset,a.centerError,a.rawSteering,a.steering,a.blend,a.desired,a.saturated,a.opposesVelocity,a.zeroDriftCorrection,correction-a.zeroDriftCorrection));
        }
        const bool ground=tMoveStorage.m_pPlayer->IsOnGround();
        const float alongVelocity=-move.m_vecVelocity.x*counter.ay+move.m_vecVelocity.y*counter.ax;
        float alongCorrection=CounterStrafe::AlongCorrection(counter,alongVelocity);
        if(ground && tMoveStorage.m_bObservedWish) {
            const auto wish=tMoveStorage.m_vObservedWish;
            const float alongWish=-wish.x*counter.ay+wish.y*counter.ax;
            const float receivedAlong=counter.along!=0.f?counter.along:alongVelocity;
            const bool braking=alongWish*receivedAlong<=0.f;
            // A stationary corridor can survive the first received departure
            // from wall contact. Honor that corroborated acceleration without
            // replacing established Snake travel with a held acceleration.
            const bool departing=counter.kind==CounterStrafe::Kind::Counter
                && std::abs(counter.along)<35.f && std::abs(alongVelocity)>35.f
                && alongWish*alongVelocity>0.f;
            if(braking || departing) {
                const auto next=MovementInputPolicy::GroundInterval({move.m_vecVelocity.x,move.m_vecVelocity.y},
                    {wish.x,wish.y},1,MovementPhysics(tMoveStorage.m_pPlayer,std::min(move.m_flMaxSpeed,move.m_flClientMaxSpeed),true));
                float desired=-next.velocity.x*counter.ay+next.velocity.y*counter.ax;
                // Braking does not invent an unreceived longitudinal reversal.
                if(braking && desired*receivedAlong<=0.f)desired=0.f;
                alongCorrection=desired-alongVelocity;
            }
        }
        const auto input=MovementInputPolicy::ForVelocity(
            {move.m_vecVelocity.x,move.m_vecVelocity.y},
            {move.m_vecVelocity.x+correction*counter.ax-alongCorrection*counter.ay,move.m_vecVelocity.y+correction*counter.ay+alongCorrection*counter.ax},
            MovementPhysics(tMoveStorage.m_pPlayer,std::min(move.m_flMaxSpeed,move.m_flClientMaxSpeed),ground),ground);
        if(input.valid) ApplyMovementInput(move,input.wish);
        // The corridor is a desired motion hypothesis, not an impulse. Native
        // ground/air acceleration is the only code allowed to realize it.

		tMoveStorage.m_flCounterTime+=TICK_INTERVAL;
	}
    const float supportForward=tMoveStorage.m_MoveData.m_flForwardMove,supportSide=tMoveStorage.m_MoveData.m_flSideMove;
    PredictLedge(tMoveStorage);
    if(tMoveStorage.m_bFailed) return;
    const bool wasGrounded=tMoveStorage.m_pPlayer->IsOnGround();
    const auto beforeOrigin=tMoveStorage.m_MoveData.m_vecAbsOrigin;
    const auto beforeVelocity=tMoveStorage.m_MoveData.m_vecVelocity;
    const float beforeForward=tMoveStorage.m_MoveData.m_flForwardMove,beforeSide=tMoveStorage.m_MoveData.m_flSideMove;
    MovementImpact::BeforeMovement(tMoveStorage);
    const bool impactInputChanged=beforeForward!=tMoveStorage.m_MoveData.m_flForwardMove||beforeSide!=tMoveStorage.m_MoveData.m_flSideMove;
	I::GameMovement->ProcessMovement(tMoveStorage.m_pPlayer, &tMoveStorage.m_MoveData);
    if(impactInputChanged){tMoveStorage.m_MoveData.m_flForwardMove=beforeForward;tMoveStorage.m_MoveData.m_flSideMove=beforeSide;}
    if(tMoveStorage.m_bLedgeBraking) {
        tMoveStorage.m_MoveData.m_flForwardMove=supportForward;tMoveStorage.m_MoveData.m_flSideMove=supportSide;
    }
    if(tMoveStorage.m_bLedgeAware) {
        const bool landed=tMoveStorage.m_pPlayer->IsOnGround();
        tMoveStorage.m_iLedgeLanding=landed?1:-1;
        if(!landed)tMoveStorage.m_bLedgeBraking=false;
        if(landed && !wasGrounded)tMoveStorage.m_bLedgeImpulse=false;
        if(ProjectileDiagnostics::LedgeSample(2))ProjectileDiagnostics::Ledge("ledge_native_support",std::format("entity={} grounded={} landing={} impulse={} origin_z={} velocity_z={} policy=native_movement_contacts",tMoveStorage.m_pPlayer->entindex(),landed,landed&&!wasGrounded,tMoveStorage.m_bLedgeImpulse,tMoveStorage.m_MoveData.m_vecAbsOrigin.z,tMoveStorage.m_MoveData.m_vecVelocity.z));
    }
    if(LandingReplay::active && wasGrounded!=tMoveStorage.m_pPlayer->IsOnGround()) {
        auto* rules=I::TFGameRules();auto* vectors=rules?rules->GetViewVectors():nullptr;
        if(vectors) {
            const bool duck=tMoveStorage.m_pPlayer->m_bDucked();
            const float scale=tMoveStorage.m_pPlayer->m_flModelScale();
            const auto mins=(duck?vectors->m_vDuckHullMin:vectors->m_vHullMin)*scale;
            const auto maxs=(duck?vectors->m_vDuckHullMax:vectors->m_vHullMax)*scale;
            SelfDamageDiagnostics::Write("landing_effective_hull",std::format(
                "cmd={} entity={} time={} nominal={} uncorrected={} from_ground={} to_ground={} mins={},{},{} maxs={},{},{} note=captured_before_bounds_restore",
                LandingReplay::command,tMoveStorage.m_pPlayer->entindex(),ROUND_TO_TICKS(tMoveStorage.m_flSimTime+TICK_INTERVAL),LandingReplay::nominalHull,LandingReplay::uncorrected,
                wasGrounded,tMoveStorage.m_pPlayer->IsOnGround(),mins.x,mins.y,mins.z,maxs.x,maxs.y,maxs.z));
        }
    }
    if(ProjectileDiagnostics::MovementSample(true)) ProjectileDiagnostics::Ledge("movement_step",std::format("entity={} simtime={} yaw_per_tick={} counter={} ledge_braking={} grounded={} origin={},{},{} velocity={},{},{}",tMoveStorage.m_pPlayer->entindex(),tMoveStorage.m_flSimTime,tMoveStorage.m_flAverageYaw,tMoveStorage.m_CounterStrafe.valid,tMoveStorage.m_bLedgeBraking,tMoveStorage.m_pPlayer->IsOnGround(),tMoveStorage.m_MoveData.m_vecAbsOrigin.x,tMoveStorage.m_MoveData.m_vecAbsOrigin.y,tMoveStorage.m_MoveData.m_vecAbsOrigin.z,tMoveStorage.m_MoveData.m_vecVelocity.x,tMoveStorage.m_MoveData.m_vecVelocity.y,tMoveStorage.m_MoveData.m_vecVelocity.z));
	if (pCallback)
		(*pCallback)(tMoveStorage.m_MoveData);

	tMoveStorage.m_MoveData.m_flClientMaxSpeed = flOldSpeed;

	tMoveStorage.m_flSimTime = ROUND_TO_TICKS(tMoveStorage.m_flSimTime + TICK_INTERVAL);
	tMoveStorage.m_bPredictNetworked = TIME_TO_TICKS(tMoveStorage.m_flSimTime) >= TIME_TO_TICKS(tMoveStorage.m_flPredictedSimTime);
	if (tMoveStorage.m_bPredictNetworked)
	{
		tMoveStorage.m_vPredictedOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;
        tMoveStorage.m_flDiagnosticNetworkOriginTime = tMoveStorage.m_flSimTime;
		tMoveStorage.m_flPredictedSimTime += tMoveStorage.m_flPredictedDelta;
	}
	bool bLastbDirectMove = tMoveStorage.m_bDirectMove;
	tMoveStorage.m_bDirectMove = tMoveStorage.m_pPlayer->IsOnGround() || tMoveStorage.m_pPlayer->IsSwimming();

	if(tMoveStorage.m_bDirectMove!=bLastbDirectMove) {
        if(ProjectileDiagnostics::MovementSample()) ProjectileDiagnostics::Ledge("movement_transition",std::format("entity={} from_direct={} to_direct={} counter_was_valid={}",tMoveStorage.m_pPlayer->entindex(),bLastbDirectMove,tMoveStorage.m_bDirectMove,tMoveStorage.m_CounterStrafe.valid));
        tMoveStorage.m_CounterStrafe.valid=false;
    }
	if (tMoveStorage.m_flAverageYaw)
		tMoveStorage.m_MoveData.m_vecViewAngles.y -= flCorrection;

    const bool grounded=tMoveStorage.m_pPlayer->IsOnGround();
    // Preserve special-mode and local-player handling outside remote walking prediction.
    if(tMoveStorage.m_bDirectMove && !bLastbDirectMove
        && (tMoveStorage.m_pPlayer->entindex()==I::EngineClient->GetLocalPlayer()
            || tMoveStorage.m_pPlayer->IsSwimming() || tMoveStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE)))
    {
        if(tMoveStorage.m_flAverageYaw)
            tMoveStorage.m_flAverageYaw=PredictionPolicy::LandingYaw(tMoveStorage.m_flAverageYaw);
        else if(!tMoveStorage.m_MoveData.m_flForwardMove && !tMoveStorage.m_MoveData.m_flSideMove
            && tMoveStorage.m_MoveData.m_vecVelocity.Length2D()>tMoveStorage.m_MoveData.m_flMaxSpeed*.015f)
        {
            const auto direction=tMoveStorage.m_MoveData.m_vecVelocity.Normalized2D()*520.f;
            G::DummyCmd.forwardmove=direction.x;G::DummyCmd.sidemove=-direction.y;
            SDK::FixMovement(&G::DummyCmd,{},tMoveStorage.m_MoveData.m_vecViewAngles);
            tMoveStorage.m_MoveData.m_flForwardMove=G::DummyCmd.forwardmove;
            tMoveStorage.m_MoveData.m_flSideMove=G::DummyCmd.sidemove;
        }
    }
    if(grounded!=wasGrounded)
    {
        auto& move=tMoveStorage.m_MoveData;
        const bool reconcile=tMoveStorage.m_pPlayer->entindex()!=I::EngineClient->GetLocalPlayer()
            && !tMoveStorage.m_pPlayer->IsSwimming() && !tMoveStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE);
        if(reconcile)
        {
            if((LandingReplay::active || ProjectileDiagnostics::current) && grounded && beforeVelocity.z<-100.f
                && (move.m_vecVelocity-beforeVelocity).Length2D()>=80.f)
            {
                auto* player=tMoveStorage.m_pPlayer;
                auto* rules=I::TFGameRules();
                auto* vectors=rules?rules->GetViewVectors():nullptr;
                if(vectors)
                {
                    const bool duck=player->m_bDucked();
                    const float scale=player->m_flModelScale();
                    const Vec3 mins=(duck?vectors->m_vDuckHullMin:vectors->m_vHullMin)*scale;
                    const Vec3 maxs=(duck?vectors->m_vDuckHullMax:vectors->m_vHullMax)*scale;
                    CTraceFilterWorldAndPropsOnly filter={};filter.pSkip=player;
                    CGameTrace impact={},support={};
                    const Vec3 sweepEnd=beforeOrigin+beforeVelocity*TICK_INTERVAL;
                    SDK::TraceHull(beforeOrigin,sweepEnd,mins,maxs,player->SolidMask(),&filter,&impact);
                    SDK::TraceHull(move.m_vecAbsOrigin+Vec3(0,0,2),move.m_vecAbsOrigin-Vec3(0,0,4),mins,maxs,player->SolidMask(),&filter,&support);
                    // A reconstructed velocity sweep can narrowly miss the engine's
                    // landing. Retry only a clean miss near verified support, extending
                    // downward by at most 1/8 unit. Never replace an existing collision.
                    const bool originalHit=impact.DidHit();
                    const bool retry=!originalHit && !impact.startsolid && !impact.allsolid
                        && support.DidHit() && !support.startsolid && !support.allsolid
                        && PredictionPolicy::NearLandingSupport(sweepEnd.z,support.endpos.z);
                    if(retry)
                        SDK::TraceHull(beforeOrigin,sweepEnd-Vec3(0,0,PredictionPolicy::LandingSweepTolerance),mins,maxs,player->SolidMask(),&filter,&impact);
                    const auto normal=impact.plane.normal;
                    const bool supported=impact.DidHit() && support.DidHit() && !impact.startsolid && !impact.allsolid
                        && !support.startsolid && !support.allsolid && impact.m_pEnt==support.m_pEnt
                        && (support.plane.normal-normal).Length()<.05f;
                    const bool correct=supported && PredictionPolicy::VerticalLandingDeflection(beforeVelocity.x,beforeVelocity.y,beforeVelocity.z,
                        move.m_vecVelocity.x,move.m_vecVelocity.y,normal.x,normal.y,normal.z);
                    const auto uncorrected=move.m_vecVelocity;
                    const bool applied=PredictionPolicy::ApplyLandingAlternative(correct,LandingReplay::active,LandingReplay::uncorrected);
                    if(applied)
                        {move.m_vecVelocity.x=beforeVelocity.x;move.m_vecVelocity.y=beforeVelocity.y;}
                    if(ProjectileDiagnostics::current) ProjectileDiagnostics::Ledge("landing_deflection",std::format(
                        "entity={} simtime={} supported={} corrected={} normal={},{},{} before_velocity={},{},{} collision_velocity={},{},{} final_velocity={},{},{} original_hit={} sweep_retry={} retry_hit={} support_gap={} alternative_candidate={} policy=engine_slide_default",
                        player->entindex(),tMoveStorage.m_flSimTime,supported,applied,normal.x,normal.y,normal.z,
                        beforeVelocity.x,beforeVelocity.y,beforeVelocity.z,uncorrected.x,uncorrected.y,uncorrected.z,
                        move.m_vecVelocity.x,move.m_vecVelocity.y,move.m_vecVelocity.z,
                        originalHit,retry,retry && impact.DidHit(),sweepEnd.z-support.endpos.z,correct));
                }
            }
            tMoveStorage.m_flAverageYaw=PredictionPolicy::LandingYaw(tMoveStorage.m_flAverageYaw);
            tMoveStorage.m_CounterStrafe.valid=false;
            move.m_flSideMove=0.f;
            // Contact may deflect falling velocity sideways. Preserve the observed
            // pre-contact horizontal intent rather than converting that deflection into input.
            // The landing-deflection velocity correction is diagnostic-replay-only.
            const auto intent=PredictionPolicy::PreContactIntent(beforeVelocity.x,beforeVelocity.y,move.m_flMaxSpeed);
            move.m_flForwardMove=grounded?intent.speed:0.f;
            if(grounded && move.m_flForwardMove>0.f)
                move.m_vecViewAngles.y=Math::VectorAngles(Vec3(intent.x,intent.y,0)).y;
        }
        if(ProjectileDiagnostics::TransitionSample())
        {
            ProjectileDiagnostics::Ledge("movement_contact_transition",std::format(
                "entity={} simtime={} from_ground={} to_ground={} reconciled={} before_origin={},{},{} after_origin={},{},{} before_velocity={},{},{} after_velocity={},{},{} before_input={},{} after_input={},{} yaw={} yaw_per_tick={} landing_intent=pre_contact_velocity",
                tMoveStorage.m_pPlayer->entindex(),tMoveStorage.m_flSimTime,wasGrounded,grounded,reconcile,
                beforeOrigin.x,beforeOrigin.y,beforeOrigin.z,move.m_vecAbsOrigin.x,move.m_vecAbsOrigin.y,move.m_vecAbsOrigin.z,
                beforeVelocity.x,beforeVelocity.y,beforeVelocity.z,move.m_vecVelocity.x,move.m_vecVelocity.y,move.m_vecVelocity.z,
                beforeForward,beforeSide,move.m_flForwardMove,move.m_flSideMove,move.m_vecViewAngles.y,tMoveStorage.m_flAverageYaw));
            // Independent world/prop probes, not the engine's internal collision trace.
            // Their results are logged only and never applied to movement.
            auto* player=tMoveStorage.m_pPlayer;
            auto* rules=I::TFGameRules();
            auto* vectors=rules?rules->GetViewVectors():nullptr;
            const bool duck=player->m_bDucked();
            const float scale=player->m_flModelScale();
            const Vec3 mins=(vectors?(duck?vectors->m_vDuckHullMin:vectors->m_vHullMin):Vec3(-24,-24,0))*scale;
            const Vec3 maxs=(vectors?(duck?vectors->m_vDuckHullMax:vectors->m_vHullMax):Vec3(24,24,duck?62:82))*scale;
            auto probe=[&](const char* name,const Vec3& start,const Vec3& end)
            {
                CTraceFilterWorldAndPropsOnly filter={};filter.pSkip=player;
                CGameTrace trace={};
                SDK::TraceHull(start,end,mins,maxs,player->SolidMask(),&filter,&trace);
                ProjectileDiagnostics::Ledge("movement_contact_probe",std::format(
                    "entity={} simtime={} probe={} reconstructed=true duck={} scale={} mins={},{},{} maxs={},{},{} from={},{},{} to={},{},{} hit={} hit_entity={} fraction={} startsolid={} allsolid={} normal={},{},{} impact={},{},{}",
                    player->entindex(),tMoveStorage.m_flSimTime,name,duck,scale,mins.x,mins.y,mins.z,maxs.x,maxs.y,maxs.z,
                    start.x,start.y,start.z,end.x,end.y,end.z,trace.DidHit(),trace.m_pEnt?trace.m_pEnt->entindex():-1,trace.fraction,trace.startsolid,trace.allsolid,
                    trace.plane.normal.x,trace.plane.normal.y,trace.plane.normal.z,trace.endpos.x,trace.endpos.y,trace.endpos.z));
            };
            probe("velocity_sweep",beforeOrigin,beforeOrigin+beforeVelocity*TICK_INTERVAL);
            probe("before_support",beforeOrigin+Vec3(0,0,2),beforeOrigin-Vec3(0,0,24));
            probe("after_support",move.m_vecAbsOrigin+Vec3(0,0,2),move.m_vecAbsOrigin-Vec3(0,0,24));
        }
    }

    MovementImpact::AtBoundary(tMoveStorage);
	if (bPath)
		tMoveStorage.m_vPath.push_back(tMoveStorage.m_MoveData.m_vecAbsOrigin);
    if(!LandingReplay::active&&!MovementImpact::hypothetical) PrivateLearning::EngineTick(&tMoveStorage,tMoveStorage.m_flSimTime,
        tMoveStorage.m_MoveData.m_vecAbsOrigin.x,tMoveStorage.m_MoveData.m_vecAbsOrigin.y,
        tMoveStorage.m_vPredictedOrigin.x,tMoveStorage.m_vPredictedOrigin.y);
    CaptureReuseTick(tMoveStorage,bPath,pCallback);
}

void CMovementSimulation::RunTick(MoveStorage& tMoveStorage, bool bPath, RunTickCallback fCallback)
{
	RunTick(tMoveStorage, bPath, &fCallback);
}

void CMovementSimulation::Restore(MoveStorage& tMoveStorage)
{
	if (tMoveStorage.m_bInitFailed || !tMoveStorage.m_pPlayer || !tMoveStorage.m_bInitialized)
		return;
	tMoveStorage.m_bInitialized = false; // make restoring more than once harmless

    const bool sameEntity=I::ClientState->m_nServerCount==tMoveStorage.m_RestoreSnapshot.server
        && I::ClientEntityList->GetClientEntityFromHandle(tMoveStorage.m_RestoreSnapshot.handle)==tMoveStorage.m_pPlayer;
	Reset(tMoveStorage);
    // Pointer ownership is independent of whether the packed model/map is still usable.
    if (sameEntity && tMoveStorage.m_pPlayer->m_pCurrentCommand()==&G::DummyCmd)
        tMoveStorage.m_pPlayer->m_pCurrentCommand() = tMoveStorage.m_pOriginalCommand;
}

float CMovementSimulation::GetPredictedDelta(CBaseEntity* pEntity)
{
	auto& vSimTimes = m_mSimTimes[pEntity->entindex()];
	if (!vSimTimes.empty())
	{
		switch (Vars::Aimbot::Projectile::DeltaMode.Value)
		{
		case 0: return std::reduce(vSimTimes.begin(), vSimTimes.end()) / vSimTimes.size();
		case 1: return *std::max_element(vSimTimes.begin(), vSimTimes.end());
		}
	}
	return TICK_INTERVAL;
}
