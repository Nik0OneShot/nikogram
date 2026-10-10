#include "AntiAim.h"

#include "../../Ticks/Ticks.h"
#include "../../Players/PlayerUtils.h"
#include "../../Misc/Misc.h"
#include "../../Aimbot/AutoRocketJump/AutoRocketJump.h"
#include "../../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include "../../Visuals/AnimInterp/AnimInterp.h"
#include "../../ImGui/MenuMode.h"
#include "EdgeCoverPolicy.h"

bool CAntiAim::UsingLegitAA() const
{
	return MenuMode::Custom(MenuMode::Active) && Vars::AntiAim::LegitEnabled.Value;
}

LegitAAPolicy::Preset CAntiAim::LegitPreset() const
{
	static_assert(TF_CLASS_SCOUT == LegitAAPolicy::Scout && TF_CLASS_SNIPER == LegitAAPolicy::Sniper
		&& TF_CLASS_SOLDIER == LegitAAPolicy::Soldier && TF_CLASS_DEMOMAN == LegitAAPolicy::Demoman
		&& TF_CLASS_MEDIC == LegitAAPolicy::Medic && TF_CLASS_HEAVY == LegitAAPolicy::Heavy
		&& TF_CLASS_PYRO == LegitAAPolicy::Pyro && TF_CLASS_SPY == LegitAAPolicy::Spy
		&& TF_CLASS_ENGINEER == LegitAAPolicy::Engineer);
	auto pLocal = H::Entities.GetLocal();
	auto pWeapon = H::Entities.GetWeapon();
	if (!pLocal || !pWeapon)
		return {};
	using Weapon = LegitAAPolicy::Weapon;
	Weapon held = Weapon::Other;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_MEDIGUN: held = Weapon::Medigun; break;
	case TF_WEAPON_SNIPERRIFLE: case TF_WEAPON_SNIPERRIFLE_DECAP: case TF_WEAPON_SNIPERRIFLE_CLASSIC:
		held = Weapon::SniperRifle; break;
	case TF_WEAPON_SMG: case TF_WEAPON_CHARGED_SMG: held = Weapon::SMG; break;
	case TF_WEAPON_REVOLVER: held = Weapon::Revolver; break;
	case TF_WEAPON_BUILDER: case TF_WEAPON_PDA_SPY_BUILD: held = Weapon::Sapper; break;
	case TF_WEAPON_KNIFE: held = Weapon::Knife; break;
	case TF_WEAPON_PDA_SPY: held = Weapon::DisguiseKit; break;
	default: if (pWeapon->GetSlot() == 2) held = Weapon::Melee; break;
	}
	return LegitAAPolicy::Select(pLocal->m_iClass(), held);
}

bool CAntiAim::UseMinWalk() const
{
	return !UsingLegitAA() && Vars::AntiAim::MinWalk.Value;
}

bool CAntiAim::AntiAimOn()
{
	if (UsingLegitAA())
		return LegitPreset().enabled;
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
	if (UsingLegitAA())
		return LegitPreset().enabled;
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
	if (UsingLegitAA() || !Vars::AntiAim::HidePitchOnShot.Value || G::Attacking != 1 || G::PrimaryWeaponType != EWeaponType::HITSCAN || pLocal->m_MoveType() != MOVETYPE_WALK)
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

static inline int GetJitter(uint32_t uHash)
{
	struct Entry { int command=-1;bool side=false; };
	static std::unordered_map<uint32_t, Entry> mJitter;
	auto& entry=mJitter[uHash];
	if(!I::ClientState->chokedcommands&&entry.command!=G::OriginalCmd.command_number)
		entry.side=!entry.side,entry.command=G::OriginalCmd.command_number;
	return entry.side?1:-1;
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
	case Vars::AntiAim::YawEnum::Edge: return 0.f; // Solved as an absolute yaw by EdgeYaw, never a render-camera offset.
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
	case Vars::AntiAim::YawModeEnum::View: return G::OriginalCmd.viewangles.y + flOffset;
	case Vars::AntiAim::YawModeEnum::Target:
	{
		float flSmallestAngleTo = 0.f; float flSmallestFovTo = 360.f;
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
		{
			auto pPlayer = pEntity->As<CTFPlayer>();
			if (pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->IsAGhost() || F::PlayerUtils.IsIgnored(pPlayer->entindex()))
				continue;
			
			const Vec3 vAngleTo = Math::CalcAngle(pLocal->m_vecOrigin(), pPlayer->m_vecOrigin());
			const float flFOVTo = Math::CalcFov(G::OriginalCmd.viewangles, vAngleTo);

			if (flFOVTo < flSmallestFovTo)
			{
				flSmallestAngleTo = vAngleTo.y;
				flSmallestFovTo = flFOVTo;
			}
		}
		return (flSmallestFovTo == 360.f ? G::OriginalCmd.viewangles.y + flOffset : flSmallestAngleTo + flOffset);
	}
	}
	return G::OriginalCmd.viewangles.y;
}

float CAntiAim::EdgeYaw(CTFPlayer* local, CUserCmd* cmd, bool fake)
{
    const auto model=local->GetModel();
    if(m_pEdgeLocal!=local||m_pEdgeModel!=model)
    {m_iEdgeCommand=-1;m_bEdgeValid=false;m_pEdgeLocal=local;m_pEdgeModel=model;}
    if(m_iEdgeCommand==cmd->command_number)return fake?m_flEdgeFake:m_flEdgeReal;
    if(m_iEdgeCommand>=0&&cmd->command_number!=m_iEdgeCommand+1)m_bEdgeValid=false;
    const bool previousValid=m_iEdgeCommand>=0&&cmd->command_number==m_iEdgeCommand+1;
    const float previousReal=m_flEdgeReal;
    const float previousFake=m_flEdgeFake;
    m_iEdgeCommand=cmd->command_number;
    const float base=GetBaseYaw(local,cmd,false),fakeBase=GetBaseYaw(local,cmd,true);
    const bool realEdge=Vars::AntiAim::YawReal.Value==Vars::AntiAim::YawEnum::Edge;
    const bool fakeEdge=Vars::AntiAim::YawFake.Value==Vars::AntiAim::YawEnum::Edge;
    const float configuredReal=base+(realEdge?0.f:GetYawOffset(local,false));
    const float configuredFake=fakeBase+(fakeEdge?0.f:GetYawOffset(local,true));
    const float barrier=fakeEdge&&previousValid?previousFake:configuredFake;
    // These remain simulation-space estimates, NOT cached render bones or a
    // guarantee of exact animated/server head positions.
    const Vec3 origin=local->m_vecOrigin(),center=local->GetCenter();
    const auto head=EdgeCoverPolicy::HeadProbe(local->m_vecViewOffset().z,local->m_flModelScale());
    const Vec3 anchor=origin+Vec3{0,0,head.height};
    CTraceFilterWorldAndPropsOnly filter;
    struct Wall {float yaw=0.f,distance=97.f;Vec3 contact;};
    std::array<Wall,3> walls{};
    int wallCount=0;
    // Find a physical, near-vertical wall at head height. Do not mistake the
    // floor/ceiling or a waist-high obstacle for head protection.
    for(int i=0;i<48;++i)
    {
        const Vec3 end=anchor+Math::RotatePoint({96,0,0},{},{0,float(i)*7.5f,0});
        CGameTrace tr{};SDK::Trace(anchor,end,MASK_SHOT|CONTENTS_GRATE,&filter,&tr);
        vEdgeTrace.emplace_back(anchor,tr.endpos);
        const Vec3 n=tr.plane.normal;
        const float horizontal=n.x*n.x+n.y*n.y,distance=tr.fraction*96.f;
        if(!tr.DidHit()||tr.startsolid||tr.allsolid||!std::isfinite(distance)||
            !std::isfinite(horizontal)||horizontal<.5f||std::abs(n.z)>.5f)continue;
        const float yaw=EdgeCoverPolicy::Normalize(std::atan2(-n.y,-n.x)*57.295779513f);
        int slot=-1;
        for(int w=0;w<wallCount;++w)
            if(EdgeCoverPolicy::Distance(yaw,walls[w].yaw)<10.f){slot=w;break;}
        if(slot<0&&wallCount<int(walls.size()))slot=wallCount++;
        if(slot<0)
        {
            slot=0;
            for(int w=1;w<wallCount;++w)if(walls[w].distance>walls[slot].distance)slot=w;
            if(distance>=walls[slot].distance)continue;
            walls[slot]={yaw,distance,tr.endpos};
        }
        else if(distance<walls[slot].distance)walls[slot]={yaw,distance,tr.endpos};
    }
    // Keep competing corner/doorway faces, not just the single nearest normal.
    // Pick a small, deterministic set of nearest live enemies; never use the
    // camera direction or a render bone cache as the cover reference.
    std::array<std::pair<float,Vec3>,3> threats{};
    int threatCount=0;
    for(auto entity:H::Entities.GetGroup(EntityEnum::PlayerEnemy))
    {
        auto player=entity->As<CTFPlayer>();
        if(player->IsDormant()||!player->IsAlive()||player->IsAGhost()||F::PlayerUtils.IsIgnored(player->entindex()))continue;
        const Vec3 eye=player->GetShootPos();
        const float distance=(eye-anchor).Length();
        if(!std::isfinite(distance)||distance<1.f)continue;
        int slot=threatCount;
        if(threatCount==int(threats.size()))
        {
            slot=0;for(int t=1;t<threatCount;++t)if(threats[t].first>threats[slot].first)slot=t;
            if(distance>=threats[slot].first)continue;
        }
        else ++threatCount;
        threats[slot]={distance,eye};
    }
    int selectedWall=-1;
    for(int w=0;w<wallCount;++w)
        if(selectedWall<0||walls[w].distance<walls[selectedWall].distance)selectedWall=w;
    // Keep the same adjacent face on near ties, not different walls per yaw.
    if(selectedWall>=0&&previousValid&&m_bEdgeValid)
        for(int w=0;w<wallCount;++w)
            if(EdgeCoverPolicy::Distance(walls[w].yaw,m_flEdgeWallYaw)<10.f&&
                walls[w].distance<=walls[selectedWall].distance+4.f){selectedWall=w;break;}
    if(selectedWall>=0)m_flEdgeWallYaw=walls[selectedWall].yaw;
    float approach=base;
    if(threatCount)
    {
        int nearest=0;
        for(int t=1;t<threatCount;++t)if(threats[t].first<threats[nearest].first)nearest=t;
        const Vec3 direction=threats[nearest].second-anchor;
        approach=std::atan2(direction.y,direction.x)*57.295779513f;
    }
    auto fit=[&](float yaw)
    {
        const Vec3 h=origin+Math::RotatePoint({head.forward,0,head.height},{},{0,yaw,0});
        EdgeCoverPolicy::CoverFit result{};
        for(int w=0;w<wallCount;++w)
        {
            if(w!=selectedWall)continue;
            const Vec3 toward=Math::RotatePoint({96,0,0},{},{0,walls[w].yaw,0});
            const Vec3 margin=Math::RotatePoint({0,head.forward*.5f,0},{},{0,walls[w].yaw,0});
            EdgeCoverPolicy::CoverFit support{};support.misses=0;support.head=0;support.valid=true;
            for(const Vec3& point:{h,h+margin,h-margin})
            {
                CGameTrace tr{};SDK::Trace(point,point+toward,MASK_SHOT|CONTENTS_GRATE,&filter,&tr);
                if(!std::isfinite(tr.fraction)){support.valid=false;break;}
                float clearance=tr.startsolid||tr.allsolid?0.f:std::clamp(tr.fraction,0.f,1.f)*96.f;
                if(!tr.DidHit()&&!tr.startsolid&&!tr.allsolid)
                {
                    ++support.misses;
                    // Parallel rays can all run PAST a thin jamb. Its observed
                    // contact is still useful geometry: keep its actual distance
                    // instead of assigning every yaw the same artificial 96.
                    const Vec3 target=walls[w].contact+toward*(8.f/96.f);
                    SDK::Trace(point,target,MASK_SHOT|CONTENTS_GRATE,&filter,&tr);
                    if(tr.DidHit()&&!tr.startsolid&&!tr.allsolid&&std::isfinite(tr.fraction))
                        clearance=(tr.endpos-point).Length();
                }
                support.head=std::max(support.head,clearance);
            }
            CGameTrace tr{};SDK::Trace(center,center+toward,MASK_SHOT|CONTENTS_GRATE,&filter,&tr);
            if(std::isfinite(tr.fraction))support.body=tr.startsolid||tr.allsolid?0.f:std::clamp(tr.fraction,0.f,1.f)*96.f;
            if(EdgeCoverPolicy::BetterCover(support,result))result=support;
        }
        const Vec3 side=Math::RotatePoint({0,head.forward*.5f,0},{},{0,yaw,0});
        const Vec3 vertical{0,0,head.forward*.375f};
        for(int t=0;t<threatCount;++t)
        {
            int visible=0;
            for(const Vec3& point:{h,h+side,h-side,h+vertical,h-vertical})
            {
                CGameTrace tr{};SDK::Trace(threats[t].second,point,MASK_SHOT|CONTENTS_GRATE,&filter,&tr);
                // A bad/solid attacker origin is unknown, not proof of cover.
                if(tr.startsolid||tr.allsolid||!std::isfinite(tr.fraction)||!tr.DidHit()||tr.fraction>=.999f)++visible;
            }
            result.visible+=visible;result.worstVisible=std::max(result.worstVisible,visible);
            CGameTrace tr{};SDK::Trace(threats[t].second,center,MASK_SHOT|CONTENTS_GRATE,&filter,&tr);
            if(tr.startsolid||tr.allsolid||!std::isfinite(tr.fraction)||!tr.DidHit()||tr.fraction>=.999f)++result.bodyVisible;
        }
        if(!threatCount&&selectedWall>=0)
        {
            // Explicit synthetic approach when alone. This is an occlusion
            // estimate, not an invented enemy or native hitbox verification.
            const Vec3 source=anchor+Math::RotatePoint({512,0,0},{},{0,approach,0});
            for(const Vec3& point:{h,h+side,h-side,h+vertical,h-vertical})
            {
                CGameTrace tr{};SDK::Trace(source,point,MASK_SHOT|CONTENTS_GRATE,&filter,&tr);
                if(tr.startsolid||tr.allsolid||!std::isfinite(tr.fraction)||!tr.DidHit()||tr.fraction>=.999f)++result.visible;
            }
            result.worstVisible=result.visible;
        }
        return result;
    };
    // Open space is a lateral-displacement problem, not a largest-yaw-gap
    // problem. Only Real Edge owns this fallback; fixed selectors stay fixed.
    auto fallback=[&]()
    {
        auto pair=EdgeCoverPolicy::Fallback(base,fakeBase,realEdge,fakeEdge,configuredReal,configuredFake);
        if(realEdge)
        {
            const float reference=approach;
            const float left=EdgeCoverPolicy::Normalize(reference+90.f),right=EdgeCoverPolicy::Normalize(reference-90.f);
            const auto leftFit=fit(left),rightFit=fit(right);
            pair.real=EdgeCoverPolicy::Lateral(reference,previousValid,previousReal,leftFit,rightFit);
            if(!EdgeCoverPolicy::Separated(pair.real,barrier))
            {
                const float other=EdgeCoverPolicy::Normalize(pair.real+180.f);
                if(EdgeCoverPolicy::Separated(other,barrier))pair.real=other;
                else pair.real=EdgeCoverPolicy::Normalize(barrier+90.f);
            }
            pair.real=EdgeCoverPolicy::Route(previousReal,pair.real,barrier,previousValid);
            if(fakeEdge)pair.fake=EdgeCoverPolicy::Normalize(pair.real+180.f);
        }
        else if(fakeEdge)pair.fake=EdgeCoverPolicy::Route(previousFake,pair.fake,pair.real,previousValid);
        m_bEdgeValid=false;m_flEdgeReal=pair.real;m_flEdgeFake=pair.fake;
        return fake?m_flEdgeFake:m_flEdgeReal;
    };
    if(!wallCount)return fallback();
    struct Candidate {float yaw;EdgeCoverPolicy::CoverFit fit;};
    std::array<Candidate,64> choices{};
    int count=0;
    int best=-1;
    auto add=[&](float yaw)
    {
        yaw=EdgeCoverPolicy::Normalize(yaw);
        for(int c=0;c<count;++c)if(EdgeCoverPolicy::Distance(yaw,choices[c].yaw)<.01f)return;
        if(count==int(choices.size()))return;
        const int index=count++;choices[index]={yaw,fit(yaw)};
        if((!realEdge||EdgeCoverPolicy::Separated(yaw,barrier))&&
            (best<0||EdgeCoverPolicy::BetterCover(choices[index].fit,choices[best].fit)))best=index;
    };
    for(int i=0;i<36;++i)add(float(i)*10.f);
    for(int w=0;w<wallCount;++w)
    {
        add(walls[w].yaw);add(walls[w].yaw+90.f);add(walls[w].yaw-90.f);
        const Vec3 direction=walls[w].contact-anchor;
        add(std::atan2(direction.y,direction.x)*57.295779513f);
    }
    add(configuredFake+180.f);add(configuredFake+90.f);add(configuredFake-90.f);
    add(barrier+90.f);add(barrier-90.f);add(barrier+180.f);
    if(!realEdge){add(configuredReal+90.f);add(configuredReal-90.f);}
    if(m_bEdgeValid)add(m_flEdgeReal);
    if(best<0||!choices[best].fit.valid)return fallback();
    const float coarse=choices[best].yaw;
    for(float offset:{-5.f,-2.5f,2.5f,5.f})add(coarse+offset);
    // A finite edge may only cover part of the head envelope. Retain the BEST
    // available wall fit rather than discarding the wall and facing backwards.
    // Concealment first: no head-offset allowance for a larger angular gap.
    // Continuity and separation only break practically identical cover ties.
    const auto bestFit=choices[best].fit;
    if(realEdge&&!fakeEdge)
    {
        for(int c=0;c<count;++c)
        {
            if(!EdgeCoverPolicy::Separated(choices[c].yaw,barrier))continue;
            if(!EdgeCoverPolicy::ComparableCover(choices[c].fit,bestFit,.05f))continue;
            const float gap=EdgeCoverPolicy::Distance(choices[c].yaw,configuredFake);
            const float oldGap=EdgeCoverPolicy::Distance(choices[best].yaw,configuredFake);
            if(gap>oldGap+.05f||(std::abs(gap-oldGap)<=.05f&&EdgeCoverPolicy::BetterCover(choices[c].fit,choices[best].fit)))best=c;
        }
    }
    if(realEdge&&previousValid)
        for(int c=0;c<count;++c)
            if(EdgeCoverPolicy::Distance(choices[c].yaw,previousReal)<.01f&&
                EdgeCoverPolicy::Separated(choices[c].yaw,barrier)&&
                EdgeCoverPolicy::ComparableCover(choices[c].fit,choices[best].fit,.05f))
                {best=c;break;}
    m_flEdgeReal=realEdge?EdgeCoverPolicy::Route(previousReal,choices[best].yaw,barrier,previousValid):EdgeCoverPolicy::Normalize(configuredReal);m_bEdgeValid=true;
    // Each selector owns only its own yaw. A fixed fake must stay exactly fixed,
    // even when that limits the separation available to a protected real.
    if(!fakeEdge)
    {
        m_flEdgeFake=EdgeCoverPolicy::Normalize(configuredFake);
        return fake?m_flEdgeFake:m_flEdgeReal;
    }
    // Only Fake Edge may optimize the decoy; never move a fixed real selector.
    int decoy=-1;
    const float opposite=EdgeCoverPolicy::Normalize(m_flEdgeReal+180.f);
    const Candidate exact{opposite,fit(opposite)};
    auto chooseFake=[&](const Candidate& candidate,int index)
    {
        if(!candidate.fit.valid||EdgeCoverPolicy::Distance(candidate.yaw,m_flEdgeReal)<90.f)return;
        if(realEdge&&previousValid&&!EdgeCoverPolicy::ClearArc(previousReal,m_flEdgeReal,candidate.yaw))return;
        const auto old=decoy<0?EdgeCoverPolicy::CoverFit{}:decoy==64?exact.fit:choices[decoy].fit;
        const float oldYaw=decoy==64?exact.yaw:decoy<0?m_flEdgeReal:choices[decoy].yaw;
        if(EdgeCoverPolicy::MoreExposed(candidate.fit,old)||
            (!EdgeCoverPolicy::MoreExposed(old,candidate.fit)&&
             EdgeCoverPolicy::Distance(candidate.yaw,m_flEdgeReal)>EdgeCoverPolicy::Distance(oldYaw,m_flEdgeReal)+.001f))
            decoy=index;
    };
    for(int i=0;i<count;++i)chooseFake(choices[i],i);
    chooseFake(exact,64);
    m_flEdgeFake=decoy==64?exact.yaw:decoy>=0?choices[decoy].yaw:opposite;
    if(!realEdge)m_flEdgeFake=EdgeCoverPolicy::Route(previousFake,m_flEdgeFake,m_flEdgeReal,previousValid);
    return fake?m_flEdgeFake:m_flEdgeReal;
}

float CAntiAim::GetYaw(CTFPlayer* pLocal, CUserCmd* pCmd, bool bFake)
{
	if (UsingLegitAA())
	{
		const auto preset = LegitPreset();
		return BodyYawPolicy::Normalize(G::OriginalCmd.viewangles.y + (bFake ? preset.fake : preset.real));
	}
    if((bFake?Vars::AntiAim::YawFake.Value:Vars::AntiAim::YawReal.Value)==Vars::AntiAim::YawEnum::Edge)
        return EdgeYaw(pLocal,pCmd,bFake);
	float flYaw = GetBaseYaw(pLocal, pCmd, bFake) + GetYawOffset(pLocal, bFake);
	return BodyYawPolicy::Normalize(flYaw);
}

float CAntiAim::GetPitch(float flCurPitch)
{
	if (UsingLegitAA())
		return flCurPitch; // Neither real nor fake pitch is modified by this preset.
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
	if (!UseMinWalk() || !YawOn() || !pLocal->m_hGroundEntity() || pLocal->InCond(TF_COND_HALLOWEEN_KART))
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



int CAntiAim::AntiAimTicks()
{
    auto local=H::Entities.GetLocal();
    if(!local||!local->m_PlayerAnimState()||F::AntiCheatCompatibility.Active())return 2;
    if(I::ClientState->chokedcommands&&m_bBodyValid&&m_pBodyLocal==local&&m_iBatchTicks)return m_iBatchTicks;
    const bool moving=local->m_vecVelocity().Length()>1.f
        || (UseMinWalk()&&local->m_hGroundEntity());
    return BodyYawPolicy::Budget(moving);
}

void CAntiAim::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, bool bSendPacket, bool bPacketControl, bool externalCorrection)
{
	G::AntiAim = bPacketControl && !externalCorrection && AntiAimOn() && ShouldRun(pLocal, pWeapon, pCmd);

	int iAntiBackstab = externalCorrection ? 0 : F::Misc.AntiBackstab(pLocal, pCmd, bSendPacket);
	if (!iAntiBackstab && !externalCorrection)
		FakeShotAngles(pLocal, pWeapon, pCmd);

	if (!G::AntiAim)
	{
        m_iEdgeCommand=-1;m_bEdgeValid=false;
		m_bBodyValid = false;
		m_iBatchTicks = 0;
		m_tPreviousReal = m_tBatchReal = {};
		vRealAngles = { pCmd->viewangles.x, pCmd->viewangles.y };
		vFakeAngles = { pCmd->viewangles.x, pCmd->viewangles.y };
		return;
	}

	vEdgeTrace.clear();

	const float flPitch = iAntiBackstab != 2 ? GetPitch(pCmd->viewangles.x) : pCmd->viewangles.x;
	// Refresh both intents from the same unmodified view, including camera turns.
	vRealAngles = { flPitch, !iAntiBackstab ? GetYaw(pLocal, pCmd, false) : pCmd->viewangles.y };
	vFakeAngles = { flPitch, !iAntiBackstab ? GetYaw(pLocal, pCmd, true) : pCmd->viewangles.y };

	if (F::AntiCheatCompatibility.Active())
	{
		Math::ClampAngles(vRealAngles);
		Math::ClampAngles(vFakeAngles);
	}

	// Intents are desired directions; steering commands remain internal.
	Vec2 vSend = bSendPacket ? vFakeAngles : vRealAngles;
	auto pAnimState = pLocal->m_PlayerAnimState();
	const bool bControl = YawOn() && !F::AntiCheatCompatibility.Active() && !iAntiBackstab && pAnimState;
	if (bControl)
	{
		const bool bMoving = pLocal->m_vecVelocity().Length() > 1.f || UseMinWalk() && pLocal->m_hGroundEntity();
		const bool bRepeat = m_bBodyValid && m_pBodyLocal == pLocal && m_pBodyModel == pLocal->GetModel()
			&& pCmd->command_number == m_iBodyCommand && I::ClientState->chokedcommands == m_iBodyChoke;
		const bool bRestart = !m_bBodyValid || !bRepeat && (!I::ClientState->chokedcommands
			|| m_pBodyLocal != pLocal || m_pBodyModel != pLocal->GetModel()
			|| pCmd->command_number != m_iBodyCommand + 1);
		if (bRepeat)
		{
			m_tBody = m_tBodyBeforeCommand;
			m_tPreviousReal = m_tPreviousRealBeforeCommand;
			m_tBatchReal = m_tBatchRealBeforeCommand;
		}
		if (bRestart)
		{
			// A normal packet boundary preserves the previous real pose; a new
			// entity/model, disabled controller or command gap must not reuse it.
			if (!m_bBodyValid || m_pBodyLocal != pLocal || m_pBodyModel != pLocal->GetModel()
				|| pCmd->command_number != m_iBodyCommand + 1)
				m_tPreviousReal = {};
			m_tBatchReal = {};
			// AnimInterp restores the native state before CreateMove prediction.
			m_tBody = { pAnimState->m_flCurrentFeetYaw, pAnimState->m_flGoalFeetYaw };
			m_pBodyLocal = pLocal;
			m_pBodyModel = pLocal->GetModel();
			m_iBatchTicks = BodyYawPolicy::Budget(bMoving);
			m_bBodyValid = BodyYawPolicy::Valid(m_tBody);
		}
		if (m_bBodyValid)
		{
			m_tBodyBeforeCommand = m_tBody;
			m_tPreviousRealBeforeCommand = m_tPreviousReal;
			m_tBatchRealBeforeCommand = m_tBatchReal;
			if (!bSendPacket)
				vSend.y = BodyYawPolicy::Choose(m_tBody, vRealAngles.y, vFakeAngles.y, bMoving,
						std::max(1, m_iBatchTicks - I::ClientState->chokedcommands), TICK_INTERVAL, m_tPreviousReal,
                        !UsingLegitAA()&&Vars::AntiAim::YawReal.Value==Vars::AntiAim::YawEnum::Edge,
                        m_tBatchReal.valid?m_tBatchReal.eye:m_tPreviousReal.eye).yaw;
			BodyYawPolicy::Step(m_tBody, vSend.y, bMoving, TICK_INTERVAL);
			if (!bSendPacket)
				m_tBatchReal = { m_tBody.feet, vRealAngles.y, true, vSend.y };
			else
				m_tPreviousReal = m_tBatchReal;
			m_iBodyCommand = pCmd->command_number;
			m_iBodyChoke = I::ClientState->chokedcommands;
		}
	}
	else
	{
		m_bBodyValid = false, m_iBatchTicks = 0;
		m_tPreviousReal = m_tBatchReal = {};
	}

	SDK::FixMovement(pCmd, vSend);
	pCmd->viewangles.x = vSend.x;
	pCmd->viewangles.y = vSend.y;

	MinWalk(pLocal, pCmd);
}

void CAntiAim::SealCommand(int sequence, const CUserCmd* command, bool send)
{
	m_tPacket = command ? AntiAimPacketPolicy::Ticket{ sequence, command->command_number,
		G::AntiAim, send, vFakeAngles.x, vFakeAngles.y } : AntiAimPacketPolicy::Ticket{};
}

bool CAntiAim::PrepareForSend(int sequence, CUserCmd* command)
{
	if (!command || !m_tPacket.NeedsRepair(sequence, command->command_number,
		command->viewangles.x, command->viewangles.y)) return false;

	// This is the newest command in an ACTUAL CLC_Move, not a predicted send.
	// Do not let a late phase change or angle overwrite publish a real command.
	const Vec3 fake = { m_tPacket.fakePitch, m_tPacket.fakeYaw, command->viewangles.z };
	SDK::FixMovement(command, fake);
	command->viewangles = fake;
	I::Input->CommitUserCmd(sequence);
	m_tPacket.selectedSend = true; // Serialization retries must be idempotent.
	++m_iPacketRepairs;
	// A repaired batch no longer matches the body's speculative simulation.
	m_bBodyValid = false;
	m_iBatchTicks = 0;
	m_tPreviousReal = m_tBatchReal = {};
	return true;
}

void CAntiAim::AuditSerialized(int sequence, const CUserCmd* command, bool queued)
{
	if (!command || !m_tPacket.Matches(sequence, command->command_number)) return;
	m_iSerializedCommand = command->command_number;
	m_flSerializedYaw = command->viewangles.y;
	m_bPacketQueued = queued;
	if (!Vars::Debug::Logging.Value || !AntiAimOn()) return;
	static ULONGLONG nextReport = 0;
	const auto now = GetTickCount64();
	if (now < nextReport) return;
	nextReport = now + 1000;
	SDK::Output("AA packet", std::format(
		"cmd={} active={} selected_send={} fake_yaw={:.2f} serialized_yaw={:.2f} queued={} repairs={} (not server acknowledgement)",
		command->command_number, m_tPacket.controlled, m_tPacket.selectedSend,
		m_tPacket.fakeYaw, m_flSerializedYaw, queued, m_iPacketRepairs).c_str(),
		Vars::Menu::Theme::Accent.Value, OUTPUT_CONSOLE | OUTPUT_DEBUG);
}

void CAntiAim::ResetPacketState()
{
    m_iEdgeCommand=-1;m_bEdgeValid=false;
	m_tPacket = {};
	m_bBodyValid = false;
	m_iBatchTicks = 0;
	m_tPreviousReal = m_tBatchReal = {};
	m_iSerializedCommand = -1;
	m_iPacketRepairs = 0;
	m_bPacketQueued = false;
}

void CAntiAim::Draw(CTFPlayer* pLocal)
{
    if(!pLocal->IsAlive()||pLocal->IsAGhost()||!AntiAimOn())return;
    if(!I::Input->CAM_IsThirdPerson()&&Vars::AntiAim::FirstPersonRing.Value&&G::AntiAim)
    {
        static float real=0,fake=0,last=0;static bool valid=false;
        const float now=I::GlobalVars->realtime,view=I::EngineClient->GetViewAngles().y;
        if(!valid||now<last||now-last>.25f){real=vRealAngles.y-view;fake=vFakeAngles.y-view;valid=true;}
        else{real=AngleDisplayPolicy::SmoothAngle(real,vRealAngles.y-view,now-last);fake=AngleDisplayPolicy::SmoothAngle(fake,vFakeAngles.y-view,now-last);}
        last=now;
        const float offset=std::isfinite(Vars::AntiAim::RingOffset.Value)?std::clamp(Vars::AntiAim::RingOffset.Value,35.f,250.f):75.f;
        const int x=H::Draw.m_nScreenW/2,y=H::Draw.m_nScreenH/2+int(offset);
        H::Draw.LineCircle(x,y,22,48,{180,180,190,150});
        auto marker=[&](float angle,Color_t colour,const char* text)
        {
            const float rad=angle*.01745329252f;
            const int px=x-int(std::sin(rad)*22),py=y-int(std::cos(rad)*22);
            H::Draw.Line(x,y,px,py,colour);H::Draw.FillCircle(px,py,3,12,colour);
            H::Draw.StringOutlined(H::Fonts.GetFont(FONT_INDICATORS),px+4,py,colour,{0,0,0,220},ALIGN_TOPLEFT,text);
        };
        marker(real,{100,240,150,255},"R");marker(fake,{245,120,145,255},"F");
    }
	if (!I::Input->CAM_IsThirdPerson())
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
			if (auto pAnimState = pLocal->m_PlayerAnimState())
			{
				const auto& font = H::Fonts.GetFont(FONT_INDICATORS);
				const Color_t bodyColour = { 100, 190, 255, 255 };
				if (SDK::W2S(vOrigin + Math::RotatePoint({ 65, 0, 0 }, {}, { 0, pAnimState->m_flCurrentFeetYaw, 0 }), vScreen2))
					H::Draw.Line(vScreen1.x, vScreen1.y, vScreen2.x, vScreen2.y, bodyColour);
				H::Draw.StringOutlined(font, vScreen1.x, vScreen1.y + font.m_nTall, bodyColour,
					Vars::Menu::Theme::Background.Value, ALIGN_TOPLEFT,
					std::format("AA target {:.1f} | completed body {:.1f} | error {:.1f}", vRealAngles.y,
						pAnimState->m_flCurrentFeetYaw, BodyYawPolicy::Distance(vRealAngles.y, pAnimState->m_flCurrentFeetYaw)).c_str());
				H::Draw.StringOutlined(font, vScreen1.x, vScreen1.y + font.m_nTall * 2, bodyColour,
					Vars::Menu::Theme::Background.Value, ALIGN_TOPLEFT,
					std::format("Fake {:.1f} | completed eye {:.1f} | render body {:.1f}", vFakeAngles.y,
						pAnimState->m_flEyeYaw, pAnimState->m_angRender.y).c_str());
				if (const auto* real = F::AnimInterp.LocalRealFrame(pLocal))
					H::Draw.StringOutlined(font, vScreen1.x, vScreen1.y + font.m_nTall * 3, bodyColour,
						Vars::Menu::Theme::Background.Value, ALIGN_TOPLEFT,
						std::format("Real command pose: eye {:.1f} | body {:.1f}", real->m_vRecordedEyeAngles.y,
								real->m_vRenderAngles.y).c_str());
				if (m_iSerializedCommand >= 0)
					H::Draw.StringOutlined(font, vScreen1.x, vScreen1.y + font.m_nTall * 4, bodyColour,
						Vars::Menu::Theme::Background.Value, ALIGN_TOPLEFT,
						std::format("Packet cmd {} | yaw {:.1f} | queued {} | repairs {} (not server ack)",
							m_iSerializedCommand, m_flSerializedYaw, m_bPacketQueued, m_iPacketRepairs).c_str());
			}
		}

		for (auto& vPair : vEdgeTrace)
		{
			if (SDK::W2S(vPair.first, vScreen1) && SDK::W2S(vPair.second, vScreen2))
				H::Draw.Line(vScreen1.x, vScreen1.y, vScreen2.x, vScreen2.y, { 255, 255, 255, 255 });
		}
	}
}
