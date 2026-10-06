#include "MovementSimulation.h"
#include "PredictionPolicy.h"
#include "LedgePrediction.h"
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
    X(m_pPlayer) X(m_flAverageYaw) X(m_flCounterTime) \
    X(m_vDiagnosticStartOrigin) X(m_vDiagnosticStartVelocity) X(m_bDiagnosticStartGrounded) \
    X(m_bLedgeAware) X(m_bLedgeBraking) X(m_flLedgeStartTime) X(m_bBunnyHop) \
    X(m_flSimTime) X(m_flPredictedDelta) X(m_flPredictedSimTime) X(m_flDiagnosticNetworkOriginTime) \
    X(m_bDirectMove) X(m_bPredictNetworked) X(m_vPredictedOrigin) \
    X(m_bFailed) X(m_bInitFailed) X(m_bInitialized)

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
    CHECK_COUNTER(speed) CHECK_COUNTER(confidence) CHECK_COUNTER(driftLimit)
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
    CPredictionCopy copy={PC_EVERYTHING,storage.m_pPlayer,PC_DATA_NORMAL,frame.packed.data(),PC_DATA_PACKED};
    copy.TransferData("MovementReuseReplay",storage.m_pPlayer->entindex(),storage.m_pPlayer->GetPredDescMap());
    CopyReuseState(storage,frame.state);
    storage.m_pPlayer->SetAbsOrigin(frame.absoluteOrigin);
    storage.m_vPath.push_back(frame.pathPoint);
    if(capture) ++ProjectileDiagnostics::reuse.hits;
    return true;
}

void CMovementSimulation::CaptureReuseTick(MoveStorage& storage,bool path,RunTickCallback* callback)
{
    if(!m_pReuseTarget || storage.m_pPlayer!=m_pReuseTarget || !m_bReuseAllowed || m_bReuseDisabled || !path || callback || LandingReplay::active)
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
	auto pMap = tMoveStorage.m_pPlayer->GetPredDescMap();
	if (!pMap)
		return false;

	size_t iSize = tMoveStorage.m_pPlayer->GetIntermediateDataSize();
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
		if (auto pMap = tMoveStorage.m_pPlayer->GetPredDescMap())
		{
			CPredictionCopy copy = { PC_EVERYTHING, tMoveStorage.m_pPlayer, PC_DATA_NORMAL, tMoveStorage.m_pData, PC_DATA_PACKED };
			copy.TransferData("MovementSimulationReset", tMoveStorage.m_pPlayer->entindex(), pMap);
		}

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
    m_mRecords.clear(); m_mCounterRecords.clear(); m_mSimTimes.clear(); m_mIdentities.clear();
}

void CMovementSimulation::StoreCounterHistory(int index)
{
    const auto& records=m_mRecords[index]; auto& history=m_mCounterRecords[index];
    if(records.size()<=1) history.clear(); // Same identity/mode/discontinuity/wall resets as normal history.
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
    CounterStrafe::Detect(samples,&audit);
    return audit;
}

bool CMovementSimulation::AcceptRecord(CTFPlayer* player, float time, const Vec3& origin, const Vec3& velocity)
{
    const int index = player->entindex();
    const auto identity = static_cast<unsigned long>(player->As<IHandleEntity>()->GetRefEHandle().ToInt());
    auto& records = m_mRecords[index];
    if (!m_mIdentities.contains(index) || m_mIdentities[index] != identity)
    { records.clear(); m_mSimTimes[index].clear(); m_mIdentities[index] = identity; }
    if (!std::isfinite(time)) { records.clear(); m_mCounterRecords[index].clear(); m_mSimTimes[index].clear(); return false; }
    if (!records.empty())
    {
        const auto& last = records.front();
        if (time == last.m_flSimTime) return false;
        if (PredictionPolicy::Discontinuity(time-last.m_flSimTime, origin.DistTo(last.m_vOrigin),
            std::max(velocity.Length(), last.m_vVelocity.Length())) || GetMoveMode(player) != last.m_iMode)
        { records.clear(); m_mSimTimes[index].clear(); }
    }
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
	std::erase_if(m_mIdentities, [&](const auto& item) { return !present.contains(item.first); });

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		auto& vRecords = m_mRecords[pPlayer->entindex()];
		const bool keepReversalStops = Vars::Aimbot::Projectile::StrafePrediction.Value & Vars::Aimbot::Projectile::StrafePredictionEnum::CounterStrafe;
		if (!pPlayer->IsAlive() || pPlayer->IsAGhost() || pPlayer->IsDormant() || (pPlayer->m_vecVelocity().IsZero() && !keepReversalStops))
		{
			vRecords.clear();
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
        StoreCounterHistory(pPlayer->entindex());
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
    StoreCounterHistory(pPlayer->entindex());
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

	if (auto pAvgVelocity = H::Entities.GetAvgVelocity(pPlayer->entindex()))
		pPlayer->m_vecVelocity() = *pAvgVelocity; // only use average velocity here

	pPlayer->m_bDucked() = pPlayer->IsDucking() && !pPlayer->IsDormant();
	pPlayer->m_fFlags() &= ~FL_DUCKING; // breaks origin's z if FL_DUCKING is not removed
	pPlayer->m_flDucktime() = 0.f;
	pPlayer->m_flDuckJumpTime() = 0.f;
	pPlayer->m_bDucking() = false;
	pPlayer->m_bInDuckJump() = false;

	if (pPlayer->entindex() != I::EngineClient->GetLocalPlayer())
	{
		pPlayer->m_vecBaseVelocity() = Vec3(); // residual basevelocity causes issues
		if (pPlayer->IsOnGround() && !pPlayer->IsDormant())
			pPlayer->m_vecVelocity().z = std::min(pPlayer->m_vecVelocity().z, 0.f); // step fix
		else
			pPlayer->m_hGroundEntity() = nullptr; // fix for velocity.z being set to 0 even if in air
	}
	else if (Vars::Misc::Movement::Bunnyhop.Value && G::OriginalCmd.buttons & IN_JUMP)
		tMoveStorage.m_bBunnyHop = true;

	// setup move data
	SetupMoveData(tMoveStorage);
	if (CheckStuck(tMoveStorage))
	{
		tMoveStorage.m_bFailed = true;
		return true;
	}

	// calculate strafe if desired
    float ledgeAge=-1.f,ledgePreviousSpeed=0.f,ledgeDirectionDot=0.f;
    int ledgeRecords=0;
	if (bStrafe && (Vars::Aimbot::Projectile::StrafePrediction.Value & Vars::Aimbot::Projectile::StrafePredictionEnum::LedgeAware)
		&& pPlayer->entindex()!=I::EngineClient->GetLocalPlayer() && pPlayer->IsOnGround()
		&& !pPlayer->IsSwimming() && !pPlayer->InCond(TF_COND_SHIELD_CHARGE))
	{
		const auto& records=m_mRecords[pPlayer->entindex()];
        ledgeRecords=int(records.size());
		for (size_t i=1;i<records.size();++i)
		{
			const auto& now=records.front(); const auto& old=records[i];
			const float age=now.m_flSimTime-old.m_flSimTime;
			if(old.m_iMode!=MoveEnum::Ground || age>.25f) break;
			const auto a=now.m_vVelocity.Normalized2D(), b=old.m_vVelocity.Normalized2D();
            ledgeAge=age;ledgePreviousSpeed=old.m_vVelocity.Length2D();ledgeDirectionDot=a.x*b.x+a.y*b.y;
			if(LedgePrediction::Evidence(now.m_vVelocity.Length2D(),old.m_vVelocity.Length2D(),a.x*b.x+a.y*b.y,age))
			{ tMoveStorage.m_bLedgeAware=true; break; }
		}
		tMoveStorage.m_flLedgeStartTime=tMoveStorage.m_flSimTime;
	}
    if(ProjectileDiagnostics::LedgeSample(0))
        ProjectileDiagnostics::Ledge("ledge_eligibility",std::format("entity={} enabled={} strafe={} local={} grounded={} swimming={} charging={} records={} evidence={} history_age={} previous_speed={} direction_dot={} speed={} simtime={} origin={},{},{}",pPlayer->entindex(),bool(Vars::Aimbot::Projectile::StrafePrediction.Value & Vars::Aimbot::Projectile::StrafePredictionEnum::LedgeAware),bStrafe,pPlayer->entindex()==I::EngineClient->GetLocalPlayer(),pPlayer->IsOnGround(),pPlayer->IsSwimming(),pPlayer->InCond(TF_COND_SHIELD_CHARGE),ledgeRecords,tMoveStorage.m_bLedgeAware,ledgeAge,ledgePreviousSpeed,ledgeDirectionDot,pPlayer->m_vecVelocity().Length2D(),tMoveStorage.m_flSimTime,pPlayer->GetAbsOrigin().x,pPlayer->GetAbsOrigin().y,pPlayer->GetAbsOrigin().z));
	if (!StrafePrediction(tMoveStorage, bStrafe, bHitchance))
	{
		tMoveStorage.m_bFailed = true;
		Restore(tMoveStorage);
		return false;
	}

	tMoveStorage.m_vPath = { tMoveStorage.m_MoveData.m_vecAbsOrigin };
	if (bPredict)
	{
		for (int i = 0; i < H::Entities.GetChoke(pPlayer->entindex()); i++)
			RunTick(tMoveStorage);
	}

	return true;
}

void CMovementSimulation::SetupMoveData(MoveStorage& tMoveStorage)
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
				G::DummyCmd.forwardmove = tRecord.m_vDirection.x;
				G::DummyCmd.sidemove = -tRecord.m_vDirection.y;
				G::DummyCmd.upmove = tRecord.m_vDirection.z;
				SDK::FixMovement(&G::DummyCmd, {}, tMoveStorage.m_MoveData.m_vecViewAngles);
				tMoveStorage.m_MoveData.m_flForwardMove = G::DummyCmd.forwardmove;
				tMoveStorage.m_MoveData.m_flSideMove = G::DummyCmd.sidemove;
				tMoveStorage.m_MoveData.m_flUpMove = G::DummyCmd.upmove;
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
	const bool bStraight = fabsf(flYaw) * tRecord1.m_vVelocity.Length2D() * iTicks < flStraightFuzzyValue; // dumb way to get straight bool

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
	const bool bStraight = fabsf(*pYaw) * tRecord1.m_vVelocity.Length2D() * iTicks < flStraightFuzzyValue; // dumb way to get straight bool

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
    if(ProjectileDiagnostics::MovementSample()) ProjectileDiagnostics::Ledge("movement_settings",std::format("entity={} strafe={} flags={} grounded={} direct_move={} swimming={} charging={} local={} hitchance_check={} hitchance={} ground_samples={} air_samples={} records={} origin={},{},{} velocity={},{},{}",tMoveStorage.m_pPlayer->entindex(),bStrafe,Vars::Aimbot::Projectile::StrafePrediction.Value,tMoveStorage.m_pPlayer->IsOnGround(),tMoveStorage.m_bDirectMove,tMoveStorage.m_pPlayer->IsSwimming(),tMoveStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE),tMoveStorage.m_pPlayer->entindex()==I::EngineClient->GetLocalPlayer(),bHitchance,Vars::Aimbot::Projectile::HitChance.Value,Vars::Aimbot::Projectile::GroundSamples.Value,Vars::Aimbot::Projectile::AirSamples.Value,m_mRecords[tMoveStorage.m_pPlayer->entindex()].size(),tMoveStorage.m_MoveData.m_vecAbsOrigin.x,tMoveStorage.m_MoveData.m_vecAbsOrigin.y,tMoveStorage.m_MoveData.m_vecAbsOrigin.z,tMoveStorage.m_MoveData.m_vecVelocity.x,tMoveStorage.m_MoveData.m_vecVelocity.y,tMoveStorage.m_MoveData.m_vecVelocity.z));
	if (bStrafe && (Vars::Aimbot::Projectile::StrafePrediction.Value & Vars::Aimbot::Projectile::StrafePredictionEnum::CounterStrafe)
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
		tMoveStorage.m_CounterStrafe=CounterStrafe::Detect(samples,logCounter?&audit:nullptr);
        if(logCounter) {
            const auto& e=tMoveStorage.m_CounterStrafe;
            ProjectileDiagnostics::Ledge("counter_model",std::format("entity={} extended={} interval={} freshness={} width_limit={} mean_drift={} cycle_drift={} drift_limit={} policy=cycle_drift_bounded_slow_history",tMoveStorage.m_pPlayer->entindex(),audit.extended,audit.interval,audit.freshness,audit.widthLimit,audit.meanDrift,e.drift,e.driftLimit));
            ProjectileDiagnostics::Ledge("counter_history",std::format("entity={} reversal_age={} low={} high={} rejection_mask={} grounded={} current_lateral={} score_kind=directional_balance_not_probability timing_model=center_convergence_not_next_reversal",tMoveStorage.m_pPlayer->entindex(),audit.age,audit.low,audit.high,audit.rejectionMask,tMoveStorage.m_pPlayer->IsOnGround(),samples.empty()?0.f:samples.back().vx*audit.axisX+samples.back().vy*audit.axisY));
            ProjectileDiagnostics::Ledge("counter_detect",std::format("entity={} reason={} samples={} duration={} rapid={} reversals={} positive={} negative={} variance={} corridor_width={} drift={} agreement={} valid={} axis={},{} center={} speed={} confidence={}",tMoveStorage.m_pPlayer->entindex(),audit.reason,samples.size(),audit.duration,audit.rapid,audit.reversals,audit.positive,audit.negative,audit.variance,audit.width,audit.drift,audit.agreement,e.valid,e.ax,e.ay,e.center,e.speed,e.confidence));
        }
		if(tMoveStorage.m_CounterStrafe.valid)
		{
			// Use reversal balance instead of the constant-turn consistency test for this model.
			if(bHitchance && tMoveStorage.m_CounterStrafe.confidence < Vars::Aimbot::Projectile::HitChance.Value/100.f)
			{
                if(ProjectileDiagnostics::MovementSample()) ProjectileDiagnostics::Ledge("counter_reject",std::format("entity={} reason=confidence threshold={} confidence={}",tMoveStorage.m_pPlayer->entindex(),Vars::Aimbot::Projectile::HitChance.Value,tMoveStorage.m_CounterStrafe.confidence));
				return false;
			}
			tMoveStorage.m_flAverageYaw=0.f;
			return true;
		}
	}
	if (bStrafe && Vars::Aimbot::Projectile::StrafePrediction.Value & (tMoveStorage.m_bDirectMove ? Vars::Aimbot::Projectile::StrafePredictionEnum::Ground : Vars::Aimbot::Projectile::StrafePredictionEnum::Air))
	{
		const int iStrafeSamples = std::max(2, tMoveStorage.m_bDirectMove
			? Vars::Aimbot::Projectile::GroundSamples.Value
			: Vars::Aimbot::Projectile::AirSamples.Value);

		GetAverageYaw(tMoveStorage, iStrafeSamples);

		// really hope this doesn't work like shit
		if (bHitchance && !tMoveStorage.m_pPlayer->m_vecVelocity().IsZero() && Vars::Aimbot::Projectile::HitChance.Value)
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
	if(!storage.m_bLedgeAware) return;
	auto* player=storage.m_pPlayer;
	auto& move=storage.m_MoveData;
	// Never carry an observed ground-braking hypothesis into a jump, fall or swimming.
	if(!player->IsOnGround() || player->IsSwimming() || move.m_vecVelocity.z>50.f
		|| (move.m_nButtons & IN_JUMP) || storage.m_flSimTime-storage.m_flLedgeStartTime>1.f)
	{
        if(ProjectileDiagnostics::LedgeSample(2)) ProjectileDiagnostics::Ledge("ledge_cancel",std::format("entity={} reason=movement_or_horizon grounded={} swimming={} vertical_speed={} jump={} elapsed={} was_braking={}",player->entindex(),player->IsOnGround(),player->IsSwimming(),move.m_vecVelocity.z,bool(move.m_nButtons&IN_JUMP),storage.m_flSimTime-storage.m_flLedgeStartTime,storage.m_bLedgeBraking));
        storage.m_bLedgeAware=false; storage.m_bLedgeBraking=false; return;
    }
	const float speed=move.m_vecVelocity.Length2D();
	if(speed<1.f && !storage.m_bLedgeBraking) return;
	const Vec3 direction=move.m_vecVelocity.Normalized2D();
	const float acceleration=std::max(400.f,move.m_flMaxSpeed*4.f);
	CTraceFilterWorldAndPropsOnly filter={}; filter.pSkip=player;
	const auto mins=player->m_vecMins()+PLAYER_ORIGIN_COMPRESSION;
	const auto maxs=player->m_vecMaxs()-PLAYER_ORIGIN_COMPRESSION;
	auto supported=[&](const Vec3& point, float depth, const char* probe) {
		CGameTrace trace={};
		SDK::TraceHull(point+Vec3(0,0,2),point-Vec3(0,0,depth),mins,maxs,player->SolidMask(),&filter,&trace);
        const bool walkable=LedgePrediction::Walkable(trace.DidHit(),trace.startsolid||trace.allsolid,trace.plane.normal.z);
        if(ProjectileDiagnostics::LedgeSample(1)) ProjectileDiagnostics::Ledge("ledge_support",std::format("entity={} probe={} elapsed={} depth={} walkable={} hit={} fraction={} startsolid={} allsolid={} normal_z={} point={},{},{} end={},{},{} vertical_drop={}",player->entindex(),probe,storage.m_flSimTime-storage.m_flLedgeStartTime,depth,walkable,trace.DidHit(),trace.fraction,trace.startsolid,trace.allsolid,trace.plane.normal.z,point.x,point.y,point.z,trace.endpos.x,trace.endpos.y,trace.endpos.z,point.z-trace.endpos.z));
		return walkable;
	};
	if(!supported(move.m_vecAbsOrigin,24.f,"current")) {
        if(ProjectileDiagnostics::LedgeSample(2)) ProjectileDiagnostics::Ledge("ledge_cancel",std::format("entity={} reason=current_support_missing was_braking={}",player->entindex(),storage.m_bLedgeBraking));
        storage.m_bLedgeAware=false; storage.m_bLedgeBraking=false; return;
    }
	const float distance=LedgePrediction::LookAhead(speed,acceleration,TICK_INTERVAL);
	const Vec3 ahead=move.m_vecAbsOrigin+direction*distance;
	CGameTrace route={};
	SDK::TraceHull(move.m_vecAbsOrigin+Vec3(0,0,2),ahead+Vec3(0,0,2),mins,maxs,player->SolidMask(),&filter,&route);
    if(ProjectileDiagnostics::LedgeSample(1)) ProjectileDiagnostics::Ledge("ledge_route",std::format("entity={} speed={} lookahead={} elapsed={} blocked={} fraction={} startsolid={} allsolid={} ahead={},{},{}",player->entindex(),speed,distance,storage.m_flSimTime-storage.m_flLedgeStartTime,route.DidHit(),route.fraction,route.startsolid,route.allsolid,ahead.x,ahead.y,ahead.z));
	// A wall or rising slope is not evidence of a drop. Leave it to engine collision handling.
	// Permit a walkable downhill slope over the probe distance, not just a flat 24-unit step.
	if(!storage.m_bLedgeBraking && !route.DidHit() && !route.startsolid && !route.allsolid && !supported(ahead,24.f+distance,"ahead"))
	{
		storage.m_bLedgeBraking=true;
        if(ProjectileDiagnostics::LedgeSample(2)) ProjectileDiagnostics::Ledge("ledge_brake_start",std::format("entity={} elapsed={} speed={} lookahead={} origin={},{},{}",player->entindex(),storage.m_flSimTime-storage.m_flLedgeStartTime,speed,distance,move.m_vecAbsOrigin.x,move.m_vecAbsOrigin.y,move.m_vecAbsOrigin.z));
		SDK::Output("LedgePrediction","Supported braking alternative selected near a predicted drop",{100,200,150},Vars::Debug::Logging.Value);
	}
	if(storage.m_bLedgeBraking)
	{
		const float reduced=LedgePrediction::Brake(speed,acceleration,TICK_INTERVAL);
        if(ProjectileDiagnostics::LedgeSample(1)) ProjectileDiagnostics::Ledge("ledge_brake_step",std::format("entity={} elapsed={} before={} after={} acceleration={} origin={},{},{}",player->entindex(),storage.m_flSimTime-storage.m_flLedgeStartTime,speed,reduced,acceleration,move.m_vecAbsOrigin.x,move.m_vecAbsOrigin.y,move.m_vecAbsOrigin.z));
		move.m_vecVelocity.x=direction.x*reduced; move.m_vecVelocity.y=direction.y*reduced;
		move.m_flForwardMove=move.m_flSideMove=0.f;
		storage.m_flAverageYaw=0.f; storage.m_CounterStrafe.valid=false;
	}
}

void CMovementSimulation::RunTick(MoveStorage& tMoveStorage, bool bPath, RunTickCallback* pCallback)
{
    ProjectileDiagnostics::Profile profile(ProjectileDiagnostics::MovementTick);
	if (!tMoveStorage.m_bInitialized || tMoveStorage.m_bFailed || !tMoveStorage.m_pPlayer || !tMoveStorage.m_pPlayer->IsPlayer())
		return;

    if(TryReuseTick(tMoveStorage,bPath,pCallback)) return;

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

	if (auto& counter=tMoveStorage.m_CounterStrafe; counter.valid)
	{
		// Keep the forward trend and vertical physics; converge only the oscillating component.
		auto& move=tMoveStorage.m_MoveData;
		const float elapsed=tMoveStorage.m_flCounterTime;
		const float lateral=move.m_vecVelocity.x*counter.ax+move.m_vecVelocity.y*counter.ay;
		const float correction=CounterStrafe::Correction(counter,
			move.m_vecAbsOrigin.x*counter.ax+move.m_vecAbsOrigin.y*counter.ay,lateral,elapsed);
        if(ProjectileDiagnostics::MovementSample(true)) {
            const auto a=CounterStrafe::Explain(counter,move.m_vecAbsOrigin.x*counter.ax+move.m_vecAbsOrigin.y*counter.ay,lateral,elapsed);
            ProjectileDiagnostics::Ledge("counter_apply",std::format("entity={} elapsed={} lateral_before={} correction={} lateral_after={} center={} drift={} origin={},{},{} drift_offset={} center_error={} raw_steering={} steering={} blend={} desired={} saturated={} opposes_velocity={} zero_drift_correction={} drift_correction_delta={} comparison=same_state_not_resimulation",tMoveStorage.m_pPlayer->entindex(),elapsed,lateral,correction,lateral+correction,counter.center,counter.drift,move.m_vecAbsOrigin.x,move.m_vecAbsOrigin.y,move.m_vecAbsOrigin.z,a.driftOffset,a.centerError,a.rawSteering,a.steering,a.blend,a.desired,a.saturated,a.opposesVelocity,a.zeroDriftCorrection,correction-a.zeroDriftCorrection));
        }
		move.m_vecVelocity.x+=correction*counter.ax;
		move.m_vecVelocity.y+=correction*counter.ay;
		move.m_vecViewAngles.y=Math::VectorAngles(move.m_vecVelocity).y;
		move.m_flForwardMove=tMoveStorage.m_bDirectMove?move.m_vecVelocity.Length2D():0.f;
		move.m_flSideMove=0.f;
		tMoveStorage.m_flCounterTime+=TICK_INTERVAL;
	}
	PredictLedge(tMoveStorage);
    const bool wasGrounded=tMoveStorage.m_pPlayer->IsOnGround();
    const auto beforeOrigin=tMoveStorage.m_MoveData.m_vecAbsOrigin;
    const auto beforeVelocity=tMoveStorage.m_MoveData.m_vecVelocity;
    const float beforeForward=tMoveStorage.m_MoveData.m_flForwardMove,beforeSide=tMoveStorage.m_MoveData.m_flSideMove;
	I::GameMovement->ProcessMovement(tMoveStorage.m_pPlayer, &tMoveStorage.m_MoveData);
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

	if (bPath)
		tMoveStorage.m_vPath.push_back(tMoveStorage.m_MoveData.m_vecAbsOrigin);
    if(!LandingReplay::active) PrivateLearning::EngineTick(&tMoveStorage,tMoveStorage.m_flSimTime,
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

	Reset(tMoveStorage);
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
