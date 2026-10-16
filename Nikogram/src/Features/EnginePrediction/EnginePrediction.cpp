#include "EnginePrediction.h"

#include "../Ticks/Ticks.h"
#include "../Simulation/MovementSimulation/MovementSimulation.h"

// account for interp and origin compression when simulating local player
void CEnginePrediction::AdjustPlayers(CBaseEntity* pLocal)
{
	m_mRestore.clear();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer == pLocal || !pPlayer->IsAlive() || pPlayer->IsAGhost())
			continue;

		m_mRestore[pPlayer] = { pPlayer->GetAbsOrigin(), pPlayer->m_vecMins(), pPlayer->m_vecMaxs() };

		pPlayer->SetAbsOrigin(pPlayer->m_vecOrigin());
		pPlayer->m_vecMins() += PLAYER_ORIGIN_COMPRESSION;
		pPlayer->m_vecMaxs() -= PLAYER_ORIGIN_COMPRESSION;
	}
}
void CEnginePrediction::RestorePlayers()
{
	for (auto& [pPlayer, tRestore] : m_mRestore)
	{
		pPlayer->SetAbsOrigin(tRestore.m_vOrigin);
		pPlayer->m_vecMins() = tRestore.m_vMins;
		pPlayer->m_vecMaxs() = tRestore.m_vMaxs;
	}
}

void CEnginePrediction::Simulate(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	const int nOldTickBase = pLocal->m_nTickBase();
	const bool bOldIsFirstPrediction = I::Prediction->m_bFirstTimePredicted;
	const bool bOldInPrediction = I::Prediction->m_bInPrediction;
    auto* oldHost=MoveSimulationHost::Current;
    auto* oldCommand=pLocal->m_pCurrentCommand();
    auto* oldPlayer=I::GameMovement->player;
    auto* oldTFPlayer=I::GameMovement->m_pTFPlayer;
    auto* oldMove=I::GameMovement->mv;
    const int oldSeed=G::RandomSeed();

	I::MoveHelper->SetHost(pLocal);
	pLocal->m_pCurrentCommand() = pCmd;
	G::RandomSeed() = MD5_PseudoRandom(pCmd->command_number) & std::numeric_limits<int>::max();

	I::Prediction->m_bFirstTimePredicted = false;
	I::Prediction->m_bInPrediction = true;
	I::Prediction->SetLocalViewAngles(pCmd->viewangles);

	AdjustPlayers(pLocal);
	I::Prediction->SetupMove(pLocal, pCmd, I::MoveHelper, &m_tMoveData);
	I::GameMovement->ProcessMovement(pLocal, &m_tMoveData);
	I::Prediction->FinishMove(pLocal, pCmd, &m_tMoveData);
	RestorePlayers();

	I::MoveHelper->SetHost(oldHost);
	pLocal->m_pCurrentCommand() = oldCommand;
	G::RandomSeed() = oldSeed;
    I::GameMovement->player=oldPlayer;I::GameMovement->m_pTFPlayer=oldTFPlayer;I::GameMovement->mv=oldMove;

	pLocal->m_nTickBase() = nOldTickBase;
	I::Prediction->m_bFirstTimePredicted = bOldIsFirstPrediction;
	I::Prediction->m_bInPrediction = bOldInPrediction;

	if (static int nLastTickBase = 0; nLastTickBase != nOldTickBase)
		F::MoveSim.StorePlayer(pLocal, m_tMoveData, TICKS_TO_TIME(nOldTickBase)), nLastTickBase = nOldTickBase;
}



void CEnginePrediction::Start(CTFPlayer* pLocal, CUserCmd* pCmd)
{
    if (m_bRestorePending) End(m_pRestorePlayer,pCmd);
    m_bInPrediction=false;
    if (!pLocal || !pLocal->IsAlive() || !m_RestoreSnapshot.Capture(pLocal)) return;
    const size_t size=m_RestoreSnapshot.size;
    if (!m_tLocal.m_pData || m_tLocal.m_iSize!=size)
    {
        auto* data=reinterpret_cast<byte*>(I::MemAlloc->Alloc(size));
        if (!data) return;
        if (m_tLocal.m_pData) I::MemAlloc->Free(m_tLocal.m_pData);
        m_tLocal.m_pData=data;m_tLocal.m_iSize=size;
    }
    CPredictionCopy copy{PC_EVERYTHING,m_tLocal.m_pData,PC_DATA_PACKED,pLocal,PC_DATA_NORMAL};
    copy.TransferData("EnginePredictionStart",pLocal->entindex(),m_RestoreSnapshot.map);
    m_nOldTickCount=I::GlobalVars->tickcount;
    m_flOldCurrentTime=I::GlobalVars->curtime;
    m_flOldFrameTime=I::GlobalVars->frametime;
    m_pRestorePlayer=pLocal;m_bRestorePending=m_bInPrediction=true;
    I::GlobalVars->tickcount=pLocal->m_nTickBase();
    I::GlobalVars->curtime=TICKS_TO_TIME(I::GlobalVars->tickcount);
    I::GlobalVars->frametime=I::Prediction->m_bEnginePaused?0.f:TICK_INTERVAL;
    Simulate(pLocal,pCmd);
}

void CEnginePrediction::End(CTFPlayer* pLocal, CUserCmd* pCmd)
{
    m_bInPrediction=false;
    if (!m_bRestorePending) return;
    m_bRestorePending=false;
    // Global clocks must be restored even if the entity died or changed identity.
    I::GlobalVars->tickcount=m_nOldTickCount;
    I::GlobalVars->curtime=m_flOldCurrentTime;
    I::GlobalVars->frametime=m_flOldFrameTime;
    PredictionRestore::Restore(m_pRestorePlayer,m_RestoreSnapshot,m_tLocal.m_pData,"EnginePredictionEnd");
    m_pRestorePlayer=nullptr;
}

void CEnginePrediction::Unload()
{
    if (m_bRestorePending) End(m_pRestorePlayer,nullptr);
	if (m_tLocal.m_pData)
	{
		I::MemAlloc->Free(m_tLocal.m_pData);
		m_tLocal = {};
	}
}