#include "Backtrack.h"
#include "../ImGui/MoonlitHud.h"
#include "BacktrackPolicy.h"
#include "CursorBacktrackPolicy.h"
#include "NetworkTimingPolicy.h"
#include "../Aimbot/Aimbot.h"
#include "../Aimbot/AimbotGlobal/AimbotGlobal.h"
#include "../ImGui/Menu/Menu.h"
#include "../Aimbot/SelfDamageDiagnostics.h"

#include "../PacketManip/FakeLag/FakeLag.h"
#include "../Ticks/Ticks.h"
#include "../AntiCheatCompatibility/AntiCheatCompatibility.h"

void CBacktrack::Reset()
{
	m_mRecords.clear();
	m_dSequences.clear();
    m_iLastInSequence = 0;
    m_mDidShoot.clear();
    m_nOldInSequenceNr=m_nOldInReliableState=m_nLastInSequenceNr=m_nOldTickBase=0;
    m_iTickCount=0;m_flMaxUnlag=1.f;m_flFakeLatency=m_flLatencyDrift=0.f;
    m_flFakeInterp=std::isfinite(G::Lerp)?std::clamp(G::Lerp,0.f,1.f):.015f;
    m_flSentInterp=-1.f;m_bLerpQueued=m_bSettingUpBones=false;
}



float CBacktrack::GetReal(int iFlow, bool bNoFake)
{
	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (!pNetChan)
		return 0.f;

    const float latency=iFlow!=MAX_FLOWS?pNetChan->GetLatency(iFlow)-(bNoFake && iFlow==FLOW_INCOMING?GetFakeLatency():0.f)
        :pNetChan->GetLatency(FLOW_INCOMING)+pNetChan->GetLatency(FLOW_OUTGOING)-(bNoFake?GetFakeLatency():0.f);
    return std::isfinite(latency)?std::max(latency,0.f):0.f;
}

float CBacktrack::ProjectileLead(float simulated, float original)
{
    return float(NetworkTimingPolicy::ProjectileLead(TICKS_TO_TIME(m_iTickCount),simulated,original,
        GetReal(FLOW_OUTGOING),TICKS_TO_TIME(GetAnticipatedChoke()),GetReal(MAX_FLOWS),TICK_INTERVAL));
}

float CBacktrack::GetWishFake()
{
	return std::clamp(Vars::Backtrack::Latency.Value / 1000.f, 0.f, m_flMaxUnlag);
}

float CBacktrack::GetWishLerp()
{
	if (F::AntiCheatCompatibility.Active())
		return std::clamp(Vars::Backtrack::Interp.Value / 1000.f, std::min(G::Lerp, 0.1f), 0.1f);

	return std::clamp(Vars::Backtrack::Interp.Value / 1000.f, std::min(G::Lerp, m_flMaxUnlag), m_flMaxUnlag);
}

float CBacktrack::GetFakeLatency()
{
	return m_flFakeLatency;
}

float CBacktrack::GetFakeInterp()
{
	if (F::AntiCheatCompatibility.Active())
		return std::min(m_flFakeInterp, 0.1f);

	return m_flFakeInterp;
}

float CBacktrack::GetWindow()
{
    return float(BacktrackPolicy::Window(Vars::Backtrack::Window.Value/1000.,TICK_INTERVAL));
}

int CBacktrack::GetAnticipatedChoke(int iMethod)
{
	int iAnticipatedChoke = 0;
    if (F::Ticks.CanChoke() && G::PrimaryWeaponType != EWeaponType::HITSCAN && iMethod == Vars::Aimbot::General::AimTypeEnum::Silent)
		iAnticipatedChoke = 1;
	if (F::FakeLag.m_iGoal && !Vars::Fakelag::UnchokeOnAttack.Value && F::Ticks.m_iShiftedTicks == F::Ticks.m_iShiftedGoal && !F::Ticks.m_bDoubletap && !F::Ticks.m_bSpeedhack)
		iAnticipatedChoke = F::FakeLag.m_iGoal - I::ClientState->chokedcommands; // iffy, unsure if there is a good way to get it to work well without unchoking
    return std::max(iAnticipatedChoke,0);
}

void CBacktrack::CreateMove(CUserCmd* pCmd)
{
	if (F::AntiCheatCompatibility.Active())
		return;

	// correct tick_count for fakeinterp / nointerp
	pCmd->tick_count += TIME_TO_TICKS(GetFakeInterp());
	if (!Vars::Visuals::Removals::Lerp.Value && !Vars::Visuals::Removals::Interpolation.Value)
		pCmd->tick_count -= TIME_TO_TICKS(G::Lerp);
}

CursorHit_t CBacktrack::FindCursorHit(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, const Vec3& shotAngles,
    int hitboxes,int historyMode,bool currentPose,bool respectFilters)
{
    CursorHit_t result;
    if (!pLocal || !pWeapon || !(hitboxes&31)) return result;
    const Vec3 eye=pLocal->GetShootPos();
    Vec3 direction; Math::AngleVectors(shotAngles+pLocal->m_vecPunchAngle(),&direction);
    const auto finite=[](const Vec3& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
    if (!finite(eye) || !finite(direction)) return result;
    const CursorBacktrackPolicy::Point origin={eye.x,eye.y,eye.z}, ray={direction.x,direction.y,direction.z};
    const float range=pWeapon->GetRange();
    if (!std::isfinite(range) || range<=0.f) return result;
    if (currentPose)
    {
        CTraceFilterHitscan liveFilter;liveFilter.pSkip=pLocal;
        CGameTrace live={};
        SDK::Trace(eye,eye+direction*range,MASK_SHOT|CONTENTS_GRATE,&liveFilter,&live);
        if (!live.startsolid && !live.allsolid && live.m_pEnt && live.m_pEnt->IsPlayer())
        {
            auto player=live.m_pEnt->As<CTFPlayer>();
            if (player!=pLocal && player->m_iTeamNum()!=pLocal->m_iTeamNum() && player->IsAlive()
                && !player->IsDormant() && !player->IsAGhost()
                && (!respectFilters || !F::AimbotGlobal.ShouldIgnore(player,pLocal,pWeapon))
                && live.hitbox>=0 && live.hitbox<player->GetNumOfHitboxes()
                && F::AimbotGlobal.IsHitboxValid(player,live.hitbox,hitboxes))
                return {player,nullptr,live.hitbox,0,double(range*live.fraction)};
        }
    }
    if (historyMode!=Vars::Triggerbot::BacktrackEnum::Last && historyMode!=Vars::Triggerbot::BacktrackEnum::All) return result;

    // Trace to historical geometry without mutating the entity's live pose.
    // World, props and other live actors still block the shot.
    class CursorFilter : public CTraceFilterHitscan
    {
    public:
        CBaseEntity* candidate=nullptr;
        bool ShouldHitEntity(IHandleEntity* entity,int mask) override
        {
            if (reinterpret_cast<CBaseEntity*>(entity)==candidate) return false;
            return CTraceFilterHitscan::ShouldHitEntity(entity,mask);
        }
    } filter;
    filter.pSkip=pLocal;
    const TickRecord* selected=nullptr;
    int hitbox=-1, tested=0;
    double nearest=range;
    for (auto entity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
    {
        auto player=entity->As<CTFPlayer>();
        if (!player->IsAlive() || player->IsAGhost() || player->IsDormant()) continue;
        if (respectFilters && F::AimbotGlobal.ShouldIgnore(player,pLocal,pWeapon)) continue;
        auto set=player->GetHitboxSet();
        if (!set) continue;
        std::vector<TickRecord*> records;
        if (!GetRecords(entity,records)) continue;
        auto valid=GetValidRecords(records);
        // Newest matching pose wins ties rather than rewinding unnecessarily.
        std::sort(valid.begin(),valid.end(),[](auto a,auto b){return a->m_flSimTime>b->m_flSimTime;});
        if (historyMode==Vars::Triggerbot::BacktrackEnum::Last && !valid.empty()) valid={valid.back()};
        filter.candidate=entity;
        for (auto record : valid)
        {
            ++tested;
            for (int boxIndex=0; boxIndex<set->numhitboxes; ++boxIndex)
            {
                auto box=set->pHitbox(boxIndex);
                if (!box || box->bone<0 || box->bone>=MAXSTUDIOBONES) continue;
                if (!F::AimbotGlobal.IsHitboxValid(player,boxIndex,hitboxes)) continue;
                CursorBacktrackPolicy::Matrix bone{};
                for (int row=0; row<3; ++row) for (int col=0; col<4; ++col)
                    bone[row][col]=record->m_aBones[box->bone][row][col];
                double distance;
                if (!CursorBacktrackPolicy::HitDistance(origin,ray,
                    {box->bbmin.x,box->bbmin.y,box->bbmin.z}, {box->bbmax.x,box->bbmax.y,box->bbmax.z},
                    bone,range,distance) || (selected && distance>=nearest-.001)) continue;
                CGameTrace trace={};
                SDK::Trace(eye,eye+direction*float(distance),MASK_SHOT|CONTENTS_GRATE,&filter,&trace);
                if (trace.startsolid || trace.allsolid || trace.fraction<1.f) continue;
                selected=record; nearest=distance; hitbox=boxIndex;
                result.player=player;
            }
        }
    }
    result.record=selected;result.hitbox=hitbox;result.tested=tested;result.distance=selected?nearest:0.;
    return result;
}

void CBacktrack::ToCursor(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd,const Vec3& shotAngles)
{
    if (!pLocal || !pWeapon || !pCmd || !pLocal->IsAlive() || !pLocal->CanAttack()
        || I::EngineVGui->IsGameUIVisible() || F::Menu.m_bIsOpen) return;
    if (!CursorBacktrackPolicy::ManualShot(Vars::Triggerbot::BacktrackToCursor.Value,
        !!(G::OriginalCmd.buttons & IN_ATTACK), !!(pCmd->buttons & IN_ATTACK),
        G::CanPrimaryAttack && G::Attacking == 1, G::PrimaryWeaponType == EWeaponType::HITSCAN,
        F::Aimbot.m_bHitscanAssisted, !!(pCmd->buttons & IN_USE), !!pCmd->weaponselect)) return;
    const int weaponID=pWeapon->GetWeaponID();
    if (weaponID==TF_WEAPON_MEDIGUN || weaponID==TF_WEAPON_LASER_POINTER || weaponID==TF_WEAPON_SNIPERRIFLE_CLASSIC) return;
    const auto hit=FindCursorHit(pLocal,pWeapon,shotAngles,31,Vars::Triggerbot::BacktrackEnum::All,false,false);
    const auto selected=hit.record;
    const int target=hit.player?hit.player->entindex():-1;
    const int previousTick=pCmd->tick_count;
    if (selected)
    {
        pCmd->tick_count=TIME_TO_TICKS(selected->m_flSimTime)+TIME_TO_TICKS(GetFakeInterp());
        ReportSelection(selected,pCmd,target);
    }
    if (SelfDamageDiagnostics::Enabled())
    {
        static unsigned long long last=0; const auto now=GetTickCount64();
        if (!last || now-last>=100)
        {
            last=now;
            SelfDamageDiagnostics::Write("cursor_backtrack",std::format(
                "cmd={} result={} target={} hitbox={} distance={} tested_records={} before_tick={} command_tick={} angles_unchanged=1 buttons_unchanged=1 shot_attempt_not_confirmation=1",
                pCmd->command_number,selected?"selected":"no_clear_historical_hit",target,hit.hitbox,hit.distance,hit.tested,previousTick,pCmd->tick_count));
        }
    }
}

void CBacktrack::SendLerp()
{
	static Timer tTimer = {};
	if (!tTimer.Run(0.1f))
		return;

	float flTarget = GetWishLerp();
    if (m_flSentInterp != flTarget || !m_bLerpQueued)
	{
        auto pNetChan = reinterpret_cast<CNetChannel*>(I::EngineClient->GetNetChannelInfo());
        if (pNetChan && I::EngineClient->IsConnected())
        {
            m_flSentInterp = flTarget; // The outgoing-message hook uses this desired value.
            NET_SetConVar tConvar1 = { "cl_interp", std::to_string(m_flSentInterp).c_str() };
            const bool interpQueued=pNetChan->SendNetMsg(tConvar1);

			NET_SetConVar tConvar2 = { "cl_interp_ratio", "1" };
            const bool ratioQueued=pNetChan->SendNetMsg(tConvar2);

			NET_SetConVar tConvar3 = { "cl_interpolate", "1" };
            const bool interpolateQueued=pNetChan->SendNetMsg(tConvar3);
            m_bLerpQueued=interpQueued && ratioQueued && interpolateQueued;
            if(SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("backtrack_interp",std::format("requested={} queued={} applied={}",flTarget,m_bLerpQueued,m_flFakeInterp));
		}
	}
}

void CBacktrack::SetLerp()
{
    if(std::isfinite(m_flSentInterp) && m_flSentInterp>=0.f && m_bLerpQueued)
        m_flFakeInterp=std::clamp(m_flSentInterp,0.f,m_flMaxUnlag);
}

void CBacktrack::UpdateDatagram()
{
	auto pNetChan = reinterpret_cast<CNetChannel*>(I::EngineClient->GetNetChannelInfo());
    if (!pNetChan)
        return;

    if(pNetChan->m_nInSequenceNr<m_iLastInSequence)
    {m_dSequences.clear();m_iLastInSequence=m_nLastInSequenceNr=0;m_flLatencyDrift=m_flFakeLatency=0.f;}

	if (auto pLocal = H::Entities.GetLocal())
		m_nOldTickBase = pLocal->m_nTickBase();

	if (pNetChan->m_nInSequenceNr > m_iLastInSequence)
	{
		m_iLastInSequence = pNetChan->m_nInSequenceNr;
		m_dSequences.emplace_front(pNetChan->m_nInReliableState, pNetChan->m_nInSequenceNr, I::GlobalVars->realtime);
	}

    while(m_dSequences.size()>1 && (m_dSequences.size()>2048
        || I::GlobalVars->realtime-m_dSequences.back().m_flTime>m_flMaxUnlag+2*TICK_INTERVAL)) m_dSequences.pop_back();
}



bool CBacktrack::GetRecords(CBaseEntity* pEntity, std::vector<TickRecord*>& vReturn)
{
    auto it=m_mRecords.find(pEntity);
    if(it==m_mRecords.end() || it->second.empty())
        return false;
    auto& vRecords = it->second;
    vReturn.reserve(vReturn.size()+vRecords.size());
	for (auto& tRecord : vRecords)
		vReturn.push_back(&tRecord);
	return true;
}

std::vector<TickRecord*> CBacktrack::GetValidRecords(std::vector<TickRecord*>& vRecords, CTFPlayer* pLocal, bool bDistance, float flTimeMod)
{
	if (vRecords.empty())
		return {};

	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (!pNetChan)
		return {};

    std::vector<TickRecord*> vReturn = {};
    vReturn.reserve(vRecords.size());
    float flCorrect = float(NetworkTimingPolicy::RewindCorrection(GetReal(MAX_FLOWS, false),
        ROUND_TO_TICKS(GetFakeInterp()), m_flMaxUnlag)) + flTimeMod;
    int iServerTick = m_iTickCount + TIME_TO_TICKS(GetReal(FLOW_OUTGOING)) + GetAnticipatedChoke() + Vars::Backtrack::Offset.Value;
    const float window=GetWindow();
    // Window zero retains the original nearest-record mode, but never forces
    // a record outside the server-safe timing allowance. No unsafe fallback.
    const bool nearestOnly=F::AntiCheatCompatibility.Active() || window<=0.f;
    const float allowance=window>0.f?window:float(BacktrackPolicy::SafeWindow(TICK_INTERVAL));
    TickRecord* nearest=nullptr;float minDelta=allowance;
    int invalid=0,outside=0;
    for(auto* record : vRecords)
    {
        if(!record || !BacktrackPolicy::Usable(record->m_flSimTime,record->m_bInvalid,TICKS_TO_TIME(iServerTick),m_flMaxUnlag,TICK_INTERVAL,flTimeMod))
        {++invalid;continue;}
        const auto finite=[](const Vec3& value){return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z);};
        if(!finite(record->m_vOrigin) || !finite(record->m_vMins) || !finite(record->m_vMaxs)) {++invalid;continue;}
        const float delta=flCorrect-TICKS_TO_TIME(iServerTick-TIME_TO_TICKS(record->m_flSimTime));
        if(!BacktrackPolicy::Within(delta,allowance)) {++outside;continue;}
        if(nearestOnly)
        {if(!nearest || fabsf(delta)<minDelta) {nearest=record;minDelta=fabsf(delta);}}
        else vReturn.push_back(record);
    }
    if(nearest) vReturn.push_back(nearest);
    if(SelfDamageDiagnostics::Enabled())
    {
        static unsigned long long last=0;const auto now=GetTickCount64();
        if(!last || now-last>=250)
        {
            last=now;
            SelfDamageDiagnostics::Write("backtrack_records",std::format(
                "input={} accepted={} invalid={} outside={} requested_ms={} effective_ms={} safe_ms={} correction={} server_tick={} tick_interval={} interp={} max_unlag={} time_mod={} choke={} nearest_only={}",
                vRecords.size(),vReturn.size(),invalid,outside,Vars::Backtrack::Window.Value,allowance*1000,
                BacktrackPolicy::SafeWindow(TICK_INTERVAL)*1000,flCorrect,iServerTick,TICK_INTERVAL,GetFakeInterp(),m_flMaxUnlag,flTimeMod,
                GetAnticipatedChoke(),nearestOnly));
        }
    }
    if (pLocal && vReturn.size() > 1)
	{
		if (bDistance)
			std::sort(vReturn.begin(), vReturn.end(), [&](const TickRecord* a, const TickRecord* b) -> bool
			{
				if (Vars::Backtrack::PreferOnShot.Value && a->m_bOnShot != b->m_bOnShot)
					return a->m_bOnShot > b->m_bOnShot;

				return pLocal->m_vecOrigin().DistToSqr(a->m_vOrigin) < pLocal->m_vecOrigin().DistToSqr(b->m_vOrigin);
			});
		else
			std::sort(vReturn.begin(), vReturn.end(), [&](const TickRecord* a, const TickRecord* b) -> bool
			{
				if (Vars::Backtrack::PreferOnShot.Value && a->m_bOnShot != b->m_bOnShot)
					return a->m_bOnShot > b->m_bOnShot;

				float flADelta = flCorrect - TICKS_TO_TIME(iServerTick - TIME_TO_TICKS(a->m_flSimTime));
				float flBDelta = flCorrect - TICKS_TO_TIME(iServerTick - TIME_TO_TICKS(b->m_flSimTime));
				return fabsf(flADelta) < fabsf(flBDelta);
			});
	}

	return vReturn;
}

matrix3x4* CBacktrack::GetBones(CBaseEntity* pEntity)
{
	std::vector<TickRecord*> vRecords = {};
	if (F::Backtrack.GetRecords(pEntity, vRecords) && !vRecords.empty())
		return vRecords.front()->m_aBones;
	return nullptr;
}



void CBacktrack::MakeRecords()
{
	for (auto& pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer() || !pPlayer->IsAlive() || pPlayer->IsAGhost()
			|| !H::Entities.GetDeltaTime(pPlayer->entindex()))
			continue;

        auto& vRecords = m_mRecords[pPlayer];

        const float simulation=pPlayer->m_flSimulationTime();
        if(!std::isfinite(simulation) || simulation<0.f) continue;
        if(!vRecords.empty())
        {
            if(simulation<vRecords.front().m_flSimTime) {vRecords.clear();m_mDidShoot[pPlayer->entindex()]=false;}
            else if(!BacktrackPolicy::NewSample(simulation,vRecords.front().m_flSimTime)) continue;
        }

		TickRecord* pLastRecord = !vRecords.empty() ? &vRecords.front() : nullptr;
		vRecords.emplace_front(
			pPlayer->m_flSimulationTime(),
			pPlayer->m_vecOrigin(),
			pPlayer->m_vecMins(),
			pPlayer->m_vecMaxs(),
			m_mDidShoot[pPlayer->entindex()]
		);
		TickRecord& tCurRecord = vRecords.front();

		m_bSettingUpBones = true;
		bool bSetup = pPlayer->SetupBones(tCurRecord.m_aBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, tCurRecord.m_flSimTime);
		m_bSettingUpBones = false;
		if (!bSetup)
		{
			vRecords.pop_front();
			continue;
		}

		bool bLagComp = false;
		if (pLastRecord)
		{
			const Vec3 vDelta = tCurRecord.m_vOrigin - pLastRecord->m_vOrigin;
			
			static auto sv_lagcompensation_teleport_dist = H::ConVars.FindVar("sv_lagcompensation_teleport_dist");
			const float flDist = powf(sv_lagcompensation_teleport_dist->GetFloat(), 2.f);
			if (vDelta.Length2DSqr() > flDist)
			{
				bLagComp = true;
				if (!H::Entities.GetLagCompensation(pPlayer->entindex()))
					vRecords.resize(1);
				std::for_each(vRecords.begin() + 1, vRecords.end(), [](auto& tRecord) { tRecord.m_bInvalid = true; });
			}

            // Invalid historical records retain their real pose/timestamp pair.
            // Replacing old geometry with a new pose invents a rewind state.
		}

		H::Entities.SetLagCompensation(pPlayer->entindex(), bLagComp);
		m_mDidShoot[pPlayer->entindex()] = false;
	}
}

void CBacktrack::CleanRecords()
{
	auto& vPlayers = H::Entities.GetGroup(EntityEnum::PlayerAll);
	for (auto it = m_mRecords.begin(); it != m_mRecords.end();)
	{	// prune records of players that are no longer present
		if (std::find(vPlayers.begin(), vPlayers.end(), it->first) == vPlayers.end())
			it = m_mRecords.erase(it);
		else
			++it;
	}

	for (auto& pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer())
			continue;

		auto& vRecords = m_mRecords[pPlayer];

		if (!pPlayer->IsAlive() || pPlayer->IsAGhost())
		{
			vRecords.clear();
			continue;
		}

		//const int iOldSize = pRecords.size();

		const float flDeadtime = TICKS_TO_TIME(m_iTickCount) + GetReal(FLOW_OUTGOING) - m_flMaxUnlag;
		if (vRecords.size() > 1 && vRecords.back().m_flSimTime == std::numeric_limits<float>::max())
			vRecords.pop_back();
		while (!vRecords.empty())
		{
			if (vRecords.back().m_flSimTime < flDeadtime || vRecords.size() > 1 && vRecords.back().m_flSimTime == std::numeric_limits<float>::max())
				vRecords.pop_back();
			else
				break;
		}

		//const int iNewSize = pRecords.size();
		//if (iOldSize != iNewSize)
		//	SDK::Output("Clear", std::format("{} -> {}", iOldSize, iNewSize).c_str(), { 255, 0, 200 }, Vars::Debug::Logging.Value);
	}
}



void CBacktrack::Store()
{
	UpdateDatagram();
	if (!I::EngineClient->IsInGame())
		return;

	static auto sv_maxunlag = H::ConVars.FindVar("sv_maxunlag");
    const float maxUnlag=sv_maxunlag?sv_maxunlag->GetFloat():1.f;
    m_flMaxUnlag=std::isfinite(maxUnlag)?std::clamp(maxUnlag,0.f,1.f):1.f;
	
	MakeRecords();
	CleanRecords();
}

void CBacktrack::ResolverUpdate(CBaseEntity* pEntity)
{
	/*
	if (!m_mRecords.contains(pEntity))
		return;

	m_mRecords[pEntity].clear();
	*/
}

void CBacktrack::ReportShot(int iIndex)
{
	if (!Vars::Backtrack::PreferOnShot.Value)
		return;

	auto pEntity = I::ClientEntityList->GetClientEntity(iIndex);
    if(!pEntity || !pEntity->As<CBaseEntity>()->IsPlayer()) return;
    auto* weapon=pEntity->As<CTFPlayer>()->m_hActiveWeapon().Get();
    if(!weapon || SDK::GetWeaponType(weapon->As<CTFWeaponBase>())!=EWeaponType::HITSCAN)
		return;

    m_mDidShoot[pEntity->entindex()] = true;
}

void CBacktrack::ReportSelection(const TickRecord* record,const CUserCmd* command,int target)
{
    if(!record || !command || !SelfDamageDiagnostics::Enabled()) return;
    static unsigned long long last=0;const auto now=GetTickCount64();
    if(last && now-last<100) return;
    last=now;
    const int serverTick=m_iTickCount+TIME_TO_TICKS(GetReal(FLOW_OUTGOING))+GetAnticipatedChoke()+Vars::Backtrack::Offset.Value;
    const float correction=float(NetworkTimingPolicy::RewindCorrection(GetReal(MAX_FLOWS,false),ROUND_TO_TICKS(GetFakeInterp()),m_flMaxUnlag));
    const float age=TICKS_TO_TIME(serverTick-TIME_TO_TICKS(record->m_flSimTime));
    SelfDamageDiagnostics::Write("backtrack_selection",std::format(
        "cmd={} target={} record_time={} record_tick={} command_tick={} lerp_ticks={} age={} delta={} effective_ms={} invalid={} incoming={} outgoing={} fake_latency={} sent_interp={} applied_interp={} server_tick={} shot_attempt_not_confirmation=1",
        command->command_number,target,record->m_flSimTime,TIME_TO_TICKS(record->m_flSimTime),command->tick_count,TIME_TO_TICKS(GetFakeInterp()),
        age,correction-age,GetWindow()*1000,record->m_bInvalid,GetReal(FLOW_INCOMING,false),GetReal(FLOW_OUTGOING,false),
        GetFakeLatency(),m_flSentInterp,GetFakeInterp(),serverTick));
}

void CBacktrack::AdjustPing(CNetChannel* pNetChan)
{
	m_nOldInSequenceNr = pNetChan->m_nInSequenceNr, m_nOldInReliableState = pNetChan->m_nInReliableState;

	auto fSet = [&]()
	{
		if (!Vars::Backtrack::Latency.Value)
			return 0.f;

		auto pLocal = H::Entities.GetLocal();
		if (!pLocal || !pLocal->m_iClass())
			return 0.f;

        static auto host_timescale = H::ConVars.FindVar("host_timescale");
        float flTimescale = host_timescale?host_timescale->GetFloat():1.f;
        if(!std::isfinite(flTimescale) || flTimescale<=0.f) return 0.f;

        float flFake = GetWishFake(), flReal = TICKS_TO_TIME(pLocal->m_nTickBase() - m_nOldTickBase);
        m_flLatencyDrift += (flReal + 5 * TICK_INTERVAL - m_flLatencyDrift) * 0.1f;

		int nInReliableState = pNetChan->m_nInReliableState, nInSequenceNr = pNetChan->m_nInSequenceNr; float flLatency = 0.f;
		for (auto& cSequence : m_dSequences)
		{
			nInReliableState = cSequence.m_nInReliableState;
			nInSequenceNr = cSequence.m_nSequenceNr;
            flLatency = std::max((I::GlobalVars->realtime - cSequence.m_flTime) * flTimescale - TICK_INTERVAL,0.f);

            if (flLatency > flFake || m_nLastInSequenceNr >= cSequence.m_nSequenceNr || flLatency > m_flMaxUnlag - m_flLatencyDrift)
				break;
		}
        if (!std::isfinite(flLatency) || flLatency > m_flMaxUnlag)
			return 0.f;

		pNetChan->m_nInReliableState = nInReliableState;
		pNetChan->m_nInSequenceNr = nInSequenceNr;
		return flLatency;
	};

	auto flLatency = fSet();
	m_nLastInSequenceNr = pNetChan->m_nInSequenceNr;

	if (Vars::Backtrack::Latency.Value || m_flFakeLatency)
	{
		m_flFakeLatency = std::clamp(m_flFakeLatency + (flLatency - m_flFakeLatency) * 0.1f, m_flFakeLatency - TICK_INTERVAL, m_flFakeLatency + TICK_INTERVAL);
		if (!flLatency && m_flFakeLatency < TICK_INTERVAL)
			m_flFakeLatency = 0.f;
	}
}

void CBacktrack::RestorePing(CNetChannel* pNetChan)
{
	pNetChan->m_nInSequenceNr = m_nOldInSequenceNr, pNetChan->m_nInReliableState = m_nOldInReliableState;
}

void CBacktrack::Draw(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Ping) || !pLocal->IsAlive())
		return;

	auto pResource = H::Entities.GetResource();
	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (!pResource || !pNetChan)
		return;

	static float flFakeLatency = 0.f;
	{
		static Timer tTimer = {};
		if (tTimer.Run(0.5f))
			flFakeLatency = GetFakeLatency();
	}
	float flFakeLerp = GetFakeInterp() > G::Lerp ? GetFakeInterp() : 0.f;

	float flFake = std::min(flFakeLatency + flFakeLerp, m_flMaxUnlag) * 1000;
	float flLatency = std::max(pNetChan->GetLatency(FLOW_INCOMING) + pNetChan->GetLatency(FLOW_OUTGOING) - flFakeLatency, 0.f) * 1000;
	int iLatencyScoreboard = pResource->m_iPing(I::EngineClient->GetLocalPlayer());
	if (MoonlitHud::Enabled())
	{
		const auto pos = Vars::Menu::PingDisplay.Value;
		MoonlitHud::Rows rows{{"scoreboard", std::format("{} ms", iLatencyScoreboard)}};
		if (flFake || Vars::Backtrack::Interp.Value > G::Lerp * 1000)
			rows.insert(rows.begin(), {"added latency", std::format("+{:.0f} ms", flFake)});
		m_vIndicatorSize = MoonlitHud::Info(pos.x, pos.y, "ping", std::format("{:.0f} ms", flLatency), rows);
		return;
	}

	int x = Vars::Menu::PingDisplay.Value.x;
	int y = Vars::Menu::PingDisplay.Value.y + 8;
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

	if (flFake || Vars::Backtrack::Interp.Value > G::Lerp * 1000)
		H::Draw.StringOutlined(fFont, x, y, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, std::format("Ping {:.0f} (+ {:.0f}) ms", flLatency, flFake).c_str());
	else
		H::Draw.StringOutlined(fFont, x, y, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, std::format("Ping {:.0f} ms", flLatency).c_str());
	H::Draw.StringOutlined(fFont, x, y += nTall, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, std::format("Scoreboard {} ms", iLatencyScoreboard).c_str());
}
