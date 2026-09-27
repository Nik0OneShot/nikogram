#include "Blockbot.h"
#include "Hazards.h"
#include "../Configs/Configs.h"
#include "../ImGui/Menu/Menu.h"
#include "../Players/PlayerUtils.h"
#include "../Ticks/Ticks.h"
#include "../Simulation/ProjectileSimulation/ProjectileSimulation.h"
#include <chrono>
#include <fstream>

namespace
{
    using BlockbotModel::Point;
    Point P(Vec3 v) { return {v.x,v.y,v.z}; }
    Vec3 V(Point p) { return {p.x,p.y,p.z}; }
    std::vector<BlockbotHazards::Box> mapHazards;
    bool dropMapReady=false;
    std::string mapDiagnostic="not loaded";
    // Diagnostic build: local-only, no player names/accounts or server addresses.
    // Bounded to 8 MiB per loaded DLL; one controller sample per second.
    void Diagnostic(const std::string& message)
    {
        static std::ofstream file;
        static bool attempted=false;
        static size_t bytes=0;
        if(bytes>=8*1024*1024)return;
        if(!attempted)
        {
            if(F::Configs.m_sCorePath.empty())return;
            attempted=true;
            const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
            file.open(F::Configs.m_sCorePath+std::format("blockbot-hole-diagnostic-{}-{}.log",GetCurrentProcessId(),stamp));
            if(file)file<<"Blockbot hole diagnostic v1; times are steady-clock seconds; local positions only\n";
        }
        if(!file)return;
        file<<message<<'\n';file.flush();bytes+=message.size()+1;
    }
    bool diagnosticSample=false;
    void LoadDropHazards()
    {
        mapHazards.clear(); dropMapReady=false;mapDiagnostic="missing map entity data";
        const char* raw=I::EngineClient->GetMapEntitiesString();
        if (!raw) return;
        const size_t length=strnlen_s(raw,4*1024*1024);
        if (!length || length>=4*1024*1024) {mapDiagnostic="invalid map entity data length";return;}
        std::vector<BlockbotHazards::Entity> entities;
        if (!BlockbotHazards::Parse(std::string_view(raw,length),entities)) {mapDiagnostic="map entity parse failure";return;}
        mapDiagnostic.clear();
        bool complete=true;
        for (const auto& entity:entities)
        {
            if (!BlockbotHazards::Dangerous(entity)) continue;
            if (entity.contains("parentname")) { complete=false;mapDiagnostic+="parented hazard; "; continue; }
            const auto model=entity.find("model");
            if (model==entity.end() || !model->second.starts_with("*")) { complete=false;mapDiagnostic+="missing hazard brush; "; continue; }
            const int index=I::ModelInfoClient->GetModelIndex(model->second.c_str());
            const auto brush=index>=0 ? I::ModelInfoClient->GetModel(index) : nullptr;
            if (!brush) { complete=false;mapDiagnostic+="unresolved brush "+model->second+"; "; continue; }
            Vec3 lo,hi,origin={},angles={}; I::ModelInfoClient->GetModelBounds(brush,lo,hi);
            auto read=[&](const char* key,Vec3& out)
            {
                auto it=entity.find(key); if (it==entity.end()) return true;
                return sscanf_s(it->second.c_str(),"%f %f %f",&out.x,&out.y,&out.z)==3 && BlockbotModel::Finite(P(out));
            };
            if (!read("origin",origin)||!read("angles",angles)||!BlockbotModel::Finite(P(lo))||!BlockbotModel::Finite(P(hi))) {complete=false;mapDiagnostic+="invalid brush bounds; ";continue;}
            Vec3 lower={FLT_MAX,FLT_MAX,FLT_MAX},upper={-FLT_MAX,-FLT_MAX,-FLT_MAX};
            for(float x:{lo.x,hi.x})for(float y:{lo.y,hi.y})for(float z:{lo.z,hi.z})
            {
                const Vec3 point=Math::RotatePoint({x,y,z},{},angles)+origin;
                lower.x=std::min(lower.x,point.x);lower.y=std::min(lower.y,point.y);lower.z=std::min(lower.z,point.z);
                upper.x=std::max(upper.x,point.x);upper.y=std::max(upper.y,point.y);upper.z=std::max(upper.z,point.z);
            }
            mapHazards.push_back({P(lower),P(upper)});
        }
        dropMapReady=complete;
        if(complete)mapDiagnostic="ready";
    }
    bool HazardFree(Vec3 a,Vec3 b,Vec3 mins,Vec3 maxs)
    {
        for(const auto& box:mapHazards) if(BlockbotHazards::Intersects(P(a),P(b),box,P(mins),P(maxs)))return false;
        return true;
    }
    double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    std::string Session()
    {
        const auto net=I::EngineClient->GetNetChannelInfo();
        return net && I::EngineClient->IsInGame() ? std::string(net->GetAddress())+"|"+I::EngineClient->GetLevelName() : "";
    }
    struct Filter : CTraceFilterCollideable
    {
        CTFPlayer* platform = nullptr;
        bool ShouldHitEntity(IHandleEntity* entity, int mask) override
        {
            if (platform && entity == reinterpret_cast<IHandleEntity*>(platform)) return false;
            return CTraceFilterCollideable::ShouldHitEntity(entity,mask);
        }
    };
    // Deliberately local navigation. Never accept a route just because its endpoint
    // is clear: check the hull and support at intervals along the entire route.
    class WalkSpace
    {
        Filter filter;
        Vec3 mins,maxs;
        int budget=1600;
    public:
        const char* floorReason="not checked";
        WalkSpace(CTFPlayer* local, CTFPlayer* platform=nullptr,int traceBudget=1600)
        {
            budget=traceBudget;
            filter.pSkip=local; filter.platform=platform; filter.iPlayer=PLAYER_ALL;
            filter.iType=SKIP_CHECK; // Movement collision must not depend on our weapon.
            filter.iTeam=local->m_iTeamNum();
            mins=local->m_vecMins(); maxs=local->m_vecMaxs();
        }
        bool Trace(Vec3 a,Vec3 b,Vec3 lo,Vec3 hi,CGameTrace& t)
        {
            if (--budget < 0 || !BlockbotModel::Finite(P(a)) || !BlockbotModel::Finite(P(b))) return false;
            SDK::TraceHull(a,b,lo,hi,MASK_PLAYERSOLID,&filter,&t);
            return !t.startsolid && !t.allsolid;
        }
        bool Clear(Vec3 a,Vec3 b)
        {
            if (Vars::Misc::Blockbot::ContinueFollowing.Value && !HazardFree(a,b,mins,maxs)) return false;
            CGameTrace t={};
            return Trace(a+Vec3(0,0,1),b+Vec3(0,0,1),mins,maxs,t) && t.fraction >= .999f;
        }
        bool Floor(Vec3 p,Vec3& floor,float up=18.f,float down=18.f)
        {
            CGameTrace t={};
            floorReason="solid start or exhausted trace budget";
            if (!Trace(p+Vec3(0,0,up),p-Vec3(0,0,down),mins,maxs,t))return false;
            floorReason="no floor within depth";if(t.fraction>=1)return false;
            floorReason="steep floor";if(t.plane.normal.z<.7f)return false;
            floorReason="player obstructs landing";
            if (t.m_pEnt && t.m_pEnt->GetClassID()==ETFClassID::CTFPlayer) return false;
            floor={p.x,p.y,t.endpos.z};
            floorReason="water or slime landing";
            if (I::EngineTrace->GetPointContents(floor+Vec3(0,0,4)) & (CONTENTS_WATER|CONTENTS_SLIME)) return false;
            // Keep the whole footprint supported, not merely its center.
            floorReason="unsupported footprint/rim";
            for (float x : {mins.x+2,maxs.x-2}) for (float y : {mins.y+2,maxs.y-2})
            {
                CGameTrace edge={};
                Vec3 q=floor+Vec3(x,y,0);
                q.z=BlockbotModel::SupportHeight(P(t.plane.normal),t.plane.dist,q.x,q.y);
                if (!Trace(q+Vec3(0,0,18),q-Vec3(0,0,18),{}, {},edge) || edge.fraction >= 1 || edge.plane.normal.z < .7f ||
                    (edge.m_pEnt && edge.m_pEnt->GetClassID()==ETFClassID::CTFPlayer)) return false;
            }
            floorReason="supported";return true;
        }
        bool Segment(Point a,Point b)
        {
            return BlockbotModel::WalkSegment(a,b,
                [&](Point p,Point& out) { Vec3 result; if (!Floor(V(p),result)) return false; out=P(result); return true; },
                [&](Point from,Point to) { return Clear(V(from),V(to)); });
        }
        bool ProjectFloor(Vec3 point,Vec3& result)
        { return Floor(point,result) || Floor(point,result,128.f,128.f); }
        bool DropStep(Vec3 here,Vec3 target,Vec3& next)
        {
            int blocked=0,noFloor=0,upperFloor=0,descent=0,hazard=0;
            Point result;
            const bool found=dropMapReady && BlockbotModel::DropEntry(P(here),P(target),[&](Point p)
            {
                const Vec3 entry=V(p);Vec3 landing;
                if(!Clear(here,entry)){++blocked;return false;}
                if(!Floor(entry,landing,1.f,1024.f)){++noFloor;return false;}
                if(landing.z>=here.z-18.f){++upperFloor;return false;}
                if(!Clear(entry,landing)){++descent;return false;}
                // Validate the actual horizontal-then-vertical path, not a diagonal.
                if(!HazardFree(here,entry,mins,maxs)||!HazardFree(entry,landing,mins,maxs)){++hazard;return false;}
                return true;
            },result);
            if(found)next=V(result);
            if(diagnosticSample)Diagnostic(std::format("drop found={} mapReady={} hazards={} blocked={} no_supported_floor={} upper_rim={} descent_blocked={} hazard={} budget={} entry=({:.1f},{:.1f},{:.1f})",
                found,dropMapReady,mapHazards.size(),blocked,noFloor,upperFloor,descent,hazard,budget,result.x,result.y,result.z)
                +" last_floor="+floorReason+" map="+mapDiagnostic.substr(0,1024));
            return found;
        }
        bool RecoveryStep(Vec3 here,Vec3 destination,Vec3 velocity,bool ignoreDanger,bool followDrops,Vec3& next,BlockbotModel::Terrain& terrain)
        {
            using BlockbotModel::Terrain;
            Vec3 support;
            const bool stable=Floor(here,support) && Floor(here+velocity.To2D()*.15f,support);
            const float initial=(destination-here).Length2D();
            Vec3 direction=(destination-here).To2D();
            if (direction.Normalize()<.01f) direction={1,0,0};
            float best=std::numeric_limits<float>::max(); bool found=false;
            // Re-evaluated every command. Favor supported routes/landings, but do
            // not confuse missing floor with a solid obstacle or exhausted trace budget.
            for (float length : {32.f,64.f,96.f}) for (float degrees : {0.f,45.f,-45.f,90.f,-90.f,135.f,-135.f,180.f})
            {
                const float angle=degrees*.01745329252f, distance=std::min(length,std::max(initial,8.f));
                Vec3 candidate=here+Vec3(direction.x*std::cos(angle)-direction.y*std::sin(angle),
                    direction.x*std::sin(angle)+direction.y*std::cos(angle),0)*distance;
                Vec3 landed;
                Terrain kind=Terrain::Blocked;
                if (ProjectFloor(candidate,landed) && Segment(P(here),P(landed))) { candidate=landed; kind=Terrain::Safe; }
                else if ((ignoreDanger || followDrops) && Clear(here,candidate))
                {
                    // Horizontal air steering does not request flight or a jump.
                    // A lower landing remains a risk, even when its footprint is supported.
                    const bool landing=Floor(candidate,landed,18.f,followDrops ? 1024.f : 128.f) && Clear(candidate,landed);
                    if(followDrops)
                    {
                        // A map-checked landing is mandatory even with Ignore danger.
                        if(!BlockbotModel::PermitDrop(dropMapReady,landing,HazardFree(here,candidate,mins,maxs)&&HazardFree(candidate,landed,mins,maxs)))continue;
                    }
                    kind=landing ? Terrain::Recoverable : Terrain::Dangerous;
                }
                if (!BlockbotModel::CanTraverse(kind,ignoreDanger || (followDrops && kind==Terrain::Recoverable))) continue;
                const float remaining=(destination-candidate).Length2D();
                if (stable && remaining>=initial-1.f) continue; // normal pursuit must make progress
                const float score=BlockbotModel::RecoveryScore(remaining,kind);
                if (score<best) { best=score; next=candidate; terrain=kind; found=true; }
            }
            return found;
        }
    };
    bool AllyGoal(CTFPlayer* local,CTFPlayer* target,WalkSpace& space,Vec3& goal,std::string& mode)
    {
        Vec3 forward; Math::AngleVectors(target->GetEyeAngles(),&forward);
        const Vec3 eye=target->GetEyePosition();
        Vec3 intercept=eye+forward*80.f;
        mode="Teammate aiming line (best effort)";
        auto weapon=target->m_hActiveWeapon().Get();
        if (weapon && BlockbotModel::UseProjectileIntercept(Vars::Misc::Blockbot::TeammateBehavior.Value))
        {
            auto gun=weapon->As<CTFWeaponBase>();
            const int id=gun->GetWeaponID();
            const bool rocket=id==TF_WEAPON_ROCKETLAUNCHER || id==TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT;
            const bool grenade=id==TF_WEAPON_GRENADELAUNCHER || id==TF_WEAPON_PIPEBOMBLAUNCHER || id==TF_WEAPON_CANNON;
            ProjectileInfo info;
            if ((rocket || grenade) && F::ProjSim.GetInfo(target,gun,target->GetEyeAngles(),info,ProjSimEnum::Interp|ProjSimEnum::NoRandomAngles))
            {
                Vec3 f,u; Math::AngleVectors(info.m_vAng,&f,nullptr,&u);
                const Vec3 velocity=f*info.m_flVelocity+(grenade ? u*200.f : Vec3{});
                // Source's standard friendly-projectile delay is 0.25 seconds.
                // Include half our hull + margin so first contact is after the delay.
                const float time=.25f+(local->m_vecMaxs().x+12.f)/std::max(info.m_flVelocity,1.f);
                CTraceFilterWorldAndPropsOnly filter; filter.pSkip=target;
                filter.iTeam=target->m_iTeamNum();
                Vec3 previous=eye;
                for (int step=0;step<=8;++step)
                {
                    const float t=time*step/8.f;
                    intercept=info.m_vPos+velocity*t-Vec3(0,0,.5f*info.m_flGravity*t*t);
                    CGameTrace trace={};
                    SDK::TraceHull(previous,intercept,-info.m_vHull,info.m_vHull,MASK_SOLID,&filter,&trace);
                    if (trace.startsolid || trace.allsolid || trace.fraction<.999f) return false;
                    previous=intercept;
                }
                mode=grenade ? "Teammate grenade path (estimate)" : "Teammate rocket path (estimate)";
            }
        }
        if (!space.Floor(intercept,goal,18.f,128.f)) return false;
        // Do not walk somewhere that cannot physically intersect the aiming line.
        return intercept.z>=goal.z+4 && intercept.z<=goal.z+local->m_vecMaxs().z-2;
    }
}

void CBlockbot::Reset()
{
    std::lock_guard lock(m_mutex);
    m_controlling=false; m_velocity={}; m_routeValid=false; m_nextPlan=0;
    m_navValid=false; m_navUntil=0;
    m_manualUser=0; m_manualAccount=0; m_manualName.clear(); m_manualSession.clear(); m_session.clear();
    m_followUser=0; m_followAccount=0; m_followMode=-1;
    mapHazards.clear(); dropMapReady=false;
    m_override={}; m_status="Waiting for a live player";
}
void CBlockbot::SetManual(int user,uint32_t account,const std::string& name)
{
    std::lock_guard lock(m_mutex);
    m_manualUser=user; m_manualAccount=account; m_manualName=name; m_manualSession=Session();
    m_followUser=0;
    m_navValid=false; m_navUntil=0;
    m_routeValid=false; m_nextPlan=0;
}
std::string CBlockbot::Status() const { std::lock_guard lock(m_mutex); return m_status; }
std::string CBlockbot::ManualName() const { std::lock_guard lock(m_mutex); return m_manualUser ? m_manualName : "None"; }

bool CBlockbot::Run(CTFPlayer* local,CUserCmd* cmd)
{
    std::lock_guard lock(m_mutex);
    m_controlling=false; m_velocity={};
    const double now=Now();
    static double nextDiagnostic=0;
    diagnosticSample=Vars::Misc::Blockbot::Enabled.Value && now>=nextDiagnostic;
    if(diagnosticSample)nextDiagnostic=now+1.;
    // Runs on every return path, including paused/no-target paths.
    struct SampleEnd
    {
        CBlockbot* self; CTFPlayer* player; double time;
        ~SampleEnd()
        {
            if(!diagnosticSample)return;
            const auto p=player->m_vecOrigin(),v=player->m_vecVelocity();
            auto status=self->m_status;
            if(const auto colon=status.find(':');colon!=std::string::npos)status.resize(colon);
            Diagnostic(std::format("t={:.2f} status={} following={} ignoreDanger={} grounded={} swimming={} pos=({:.1f},{:.1f},{:.1f}) velocity=({:.1f},{:.1f},{:.1f})",
                time,status,Vars::Misc::Blockbot::ContinueFollowing.Value,Vars::Misc::Blockbot::IgnoreDanger.Value,
                bool(player->m_hGroundEntity()),player->IsSwimming(),p.x,p.y,p.z,v.x,v.y,v.z));
        }
    } sampleEnd{this,local,now};
    const auto session=Session();
    if (session!=m_session)
    {
        m_session=session; m_routeValid=false; m_nextPlan=0;
        m_followUser=0;
        m_navValid=false; m_navUntil=0;
        LoadDropHazards();
        if (m_manualSession!=session) { m_manualUser=0; m_manualName.clear(); }
    }
    const auto& raw=G::OriginalCmd;
    const bool movement=raw.forwardmove!=0 || raw.sidemove!=0 || raw.upmove!=0 ||
        (raw.buttons & (IN_FORWARD|IN_BACK|IN_MOVELEFT|IN_MOVERIGHT));
    const bool manual=BlockbotModel::ManualOverride(movement,raw.buttons & IN_JUMP,
        raw.buttons & IN_DUCK,Vars::Misc::Blockbot::WhileCrouching.Value);
    const float configuredDelay=Vars::Misc::Blockbot::ResumeDelay.Value;
    const double delay=std::isfinite(configuredDelay) ? std::max(0.f,configuredDelay) : .5;
    const bool paused=m_override.Paused(now,manual,delay);
    auto stop=[&](const char* message) { m_status=message; m_routeValid=false; m_nextPlan=0; return false; };
    if (!Vars::Misc::Blockbot::Enabled.Value) { m_followUser=0; return stop("Off"); }
    if (paused)
    {
        m_status=std::format("Manual movement - resume delay {:g}s",delay);
        m_routeValid=false; m_nextPlan=0; return false;
    }
    if (session.empty() || I::EngineClient->IsPlayingDemo() || !local->IsAlive() || local->IsAGhost()) return stop("Waiting for a live player");
    if (BlockbotModel::UIBlocked(F::Menu.m_bIsOpen,Vars::Misc::Blockbot::WhileMenuOpen.Value,
        I::EngineVGui->IsGameUIVisible(),I::MatSystemSurface->IsCursorVisible(),SDK::IsGameWindowInFocus()))
    {
        if (!m_status.starts_with("UI paused. Last: ")) m_status="UI paused. Last: "+m_status;
        m_routeValid=false; m_nextPlan=0; return false;
    }
    if (local->m_MoveType()!=MOVETYPE_WALK || local->IsSwimming() || local->IsTaunting() ||
        local->InCond(TF_COND_SHIELD_CHARGE) || local->InCond(TF_COND_HALLOWEEN_KART)) return stop("Current movement state is unsupported");
    if (Vars::AutoPeek::Enabled.Value || Vars::Misc::Movement::MovementLock.Value || Vars::Misc::Movement::AutoRocketJump.Value)
        return stop("Paused for Auto peek / Movement lock / Auto rocket jump");
    if (F::Ticks.m_bWarp || F::Ticks.m_bSpeedhack || F::Ticks.m_bAntiWarp || F::Ticks.m_bDoubletap)
        return stop("Paused during tick-shift movement");
    const bool ignoreDanger=Vars::Misc::Blockbot::IgnoreDanger.Value;
    const bool following=Vars::Misc::Blockbot::ContinueFollowing.Value;
    const bool airborne=!local->m_hGroundEntity();
    if (airborne && !ignoreDanger && !following) return stop("Waiting for supported ground or a player's head");

    CTFPlayer* target=nullptr;
    player_info_t chosen={};
    float best=std::numeric_limits<float>::max(); int bestPriority=std::numeric_limits<int>::min();
    const int selection=Vars::Misc::Blockbot::Target.Value;
    const float range=std::clamp(Vars::Misc::Blockbot::Range.Value,100.f,1500.f);
    bool chosenRetained=false;
    for (int i=1;i<=I::EngineClient->GetMaxClients();++i)
    {
        auto entity=I::ClientEntityList->GetClientEntity(i);
        if (!entity || entity->GetClassID()!=ETFClassID::CTFPlayer || entity==local || entity->IsDormant()) continue;
        auto player=entity->As<CTFPlayer>(); player_info_t info={};
        if (!player->IsAlive() || player->IsAGhost() || !I::EngineClient->GetPlayerInfo(i,&info) || info.ishltv ||
            !BlockbotModel::TeamAllowed(Vars::Misc::Blockbot::Team.Value,local->m_iTeamNum(),player->m_iTeamNum())) continue;
        if (selection==2 && (!BlockbotModel::ManualMatches(m_manualUser,m_manualAccount,info.userID,info.friendsID) || m_manualSession!=session)) continue;
        const float distance=(player->m_vecOrigin()-local->m_vecOrigin()).Length();
        const int priority=info.friendsID ? F::PlayerUtils.GetPriority(info.friendsID,false) : 0;
        const bool retained=BlockbotModel::RetainTarget(following,selection,m_followMode,info.userID,info.friendsID,m_followUser,m_followAccount);
        if (!BlockbotModel::Eligible(selection,priority,distance,retained ? std::numeric_limits<float>::max() : range)) continue;
        if (!target || (retained && !chosenRetained) || (!chosenRetained && BlockbotModel::Better(selection,priority,distance,bestPriority,best)))
        { target=player; chosen=info; best=distance; bestPriority=priority; chosenRetained=retained; }
    }
    if (!target) { m_followUser=0; return stop(selection==2 ? "Manual target unavailable / filtered / out of range" : "No eligible target in range"); }
    m_followUser=following ? chosen.userID : 0; m_followAccount=chosen.friendsID; m_followMode=selection;
    const Vec3 here=local->m_vecOrigin(), origin=target->m_vecOrigin();
    const bool enemy=target->m_iTeamNum()!=local->m_iTeamNum();
    const bool head=enemy && (local->m_hGroundEntity().Get()==target ||
        BlockbotModel::OnHead(P(here),P(origin),P(target->m_vecMins()),P(target->m_vecMaxs())));
    const int behavior=Vars::Misc::Blockbot::Behavior.Value;
    Vec3 goal={},feed={}; std::string mode;
    auto response=[&](Vec3 arrival)
    {
        return V(BlockbotModel::Response(BlockbotModel::PursuitVelocity(P(here),P(goal),P(arrival),P(feed),local->m_flMaxspeed()),
            P(local->m_vecVelocity().To2D()),Vars::Misc::Blockbot::Acceleration.Value,Vars::Misc::Blockbot::Deceleration.Value));
    };
    auto navigate=[&](Vec3 destination,Vec3& next)
    {
        if(now>=m_navUntil || m_navUser!=chosen.userID || (destination-m_navGoal).Length()>64.f ||
            (m_navValid&&(m_navWaypoint-here).Length2D()<16.f))
        {
            WalkSpace navigation(local,nullptr,12000);
            Point point;
            m_navValid=BlockbotModel::Navigate(P(here),P(destination),
                [&](Point p,Point& out){Vec3 grounded;if(!navigation.ProjectFloor(V(p),grounded))return false;out=P(grounded);return true;},
                [&](Point a,Point b){return navigation.Segment(a,b);},point);
            m_navWaypoint=V(point);m_navGoal=destination;m_navUser=chosen.userID;m_navUntil=now+.35;
        }
        if(!m_navValid)return false;
        WalkSpace check(local);
        if(!check.Segment(P(here),P(m_navWaypoint))){m_navValid=false;return false;}
        next=m_navWaypoint;return true;
    };
    auto fallback=[&](Vec3 destination,const char* reason,bool followTarget=false)
    {
        if ((!following && !ignoreDanger) || (followTarget && !following && !airborne)) return stop(reason);
        const bool followDrops=following && origin.z<here.z-18.f;
        if(followDrops)destination=origin; // Align with the opening, not beside it.
        else if (followTarget || (destination-here).Length()>range)
        {
            // Follow a point beside the target, not inside its collision hull.
            Vec3 approach=(here-origin).To2D();
            if (approach.Normalize()<.01f) approach={1,0,0};
            destination=origin+approach*(BlockbotModel::HullSpacing(P(approach),P(local->m_vecMaxs()+target->m_vecMaxs()))+12.f);
        }
        WalkSpace recovery(local,head ? target : nullptr);
        Vec3 next; BlockbotModel::Terrain terrain=BlockbotModel::Terrain::Blocked;
        m_routeValid=false; m_nextPlan=0;
        WalkSpace dropSpace(local,nullptr,2400);
        const bool drop=followDrops && dropSpace.DropStep(here,origin,next);
        if(diagnosticSample)Diagnostic(std::format("fallback reason={} lowerTarget={} target_delta=({:.1f},{:.1f},{:.1f})",reason,followDrops,origin.x-here.x,origin.y-here.y,origin.z-here.z));
        const bool hallway=!drop && !airborne && navigate(destination,next);
        if(drop)terrain=BlockbotModel::Terrain::Recoverable;
        if(hallway)terrain=BlockbotModel::Terrain::Safe;
        if (drop || hallway || recovery.RecoveryStep(here,destination,local->m_vecVelocity(),ignoreDanger,followDrops,next,terrain))
        {
            goal=next; feed={};
            m_velocity=response(drop ? next : destination);
            const char* label=terrain==BlockbotModel::Terrain::Safe ? "Following - supported route" :
                terrain==BlockbotModel::Terrain::Recoverable ? "Danger detected - steering toward supported landing" : "Danger detected - risky pursuit / recovery";
            m_status=std::string(label)+": "+chosen.name;
        }
        else
        {
            m_velocity=-local->m_vecVelocity().To2D();
            m_status=followDrops && !dropMapReady ? "Drop refused - map hazard information unavailable" : std::string("No clear recovery / follow step - braking: ")+reason;
        }
        m_controlling=true;
        const auto command=BlockbotModel::Command(P(m_velocity),cmd->viewangles.y);
        cmd->forwardmove=std::clamp(command.x,-450.f,450.f); cmd->sidemove=std::clamp(command.y,-450.f,450.f);
        return true;
    };
    if (airborne && !head)
    {
        if (enemy && behavior!=1 && BlockbotModel::RecoverHead(P(here),P(origin),target->m_vecMaxs().z))
        {
            WalkSpace headSpace(local,target);
            goal={origin.x,origin.y,here.z}; feed=target->m_vecVelocity().To2D();
            if (headSpace.Clear(here,goal))
            {
                m_velocity=response(goal); m_controlling=true;
                m_routeValid=false; m_nextPlan=0;
                m_status=std::string("Danger detected - trying to regain head contact: ")+chosen.name;
                const auto command=BlockbotModel::Command(P(m_velocity),cmd->viewangles.y);
                cmd->forwardmove=std::clamp(command.x,-450.f,450.f); cmd->sidemove=std::clamp(command.y,-450.f,450.f);
                return true;
            }
        }
        return fallback(origin,"Airborne recovery",true);
    }
    if (enemy && behavior==2 && !head) return fallback(origin,"Head riding: get onto the selected enemy first",true);
    if(following && !head && origin.z<here.z-18.f)
        return fallback(origin,"Following target below",true);
    WalkSpace space(local,head && behavior!=1 ? target : nullptr);
    if (head && behavior!=1)
    {
        feed=target->m_vecVelocity().To2D();
        goal=origin+feed*.08f; goal.z=here.z;
        // Center tracking only; never deliberately walk beyond the head platform.
        goal.x=std::clamp(goal.x,origin.x+target->m_vecMins().x+8,origin.x+target->m_vecMaxs().x-8);
        goal.y=std::clamp(goal.y,origin.y+target->m_vecMins().y+8,origin.y+target->m_vecMaxs().y-8);
        if (!space.Clear(here,goal)) return fallback(goal,"Head riding: obstruction above / beside target");
        mode="Enemy head riding (best effort)";
        m_routeValid=false;
    }
    else
    {
        if (head) return stop("Ground mode: waiting to return to the ground");
        if (enemy)
        {
            Vec3 direction=target->m_vecVelocity().To2D();
            if (direction.Length2D()<20.f) { Math::AngleVectors(target->GetEyeAngles(),&direction); direction.z=0; }
            if (direction.Normalize()<.01f) direction={1,0,0};
            const float spacing=BlockbotModel::HullSpacing(P(direction),P(local->m_vecMaxs()+target->m_vecMaxs()));
            goal=origin+direction*spacing;
            if (!space.ProjectFloor(goal,goal)) return fallback(goal,"No supported ground ahead of target");
            feed=target->m_vecVelocity().To2D(); mode="Enemy ground obstruction (best effort)";
        }
        else if (!AllyGoal(local,target,space,goal,mode)) return fallback(origin,"Aiming line / projectile path has no reachable intercept",true);
        if ((goal-here).Length()>range) return fallback(origin,"Intercept is outside acquisition range",true);
        if (now>=m_nextPlan || m_plannedUser!=chosen.userID || (goal-m_plannedGoal).Length()>20.f ||
            (m_routeValid && (m_waypoint-here).Length2D()<16.f && (m_waypoint-goal).Length2D()>16.f))
        {
            Point waypoint;
            m_routeValid=BlockbotModel::Route(P(here),P(goal),[&](Point a,Point b){return space.Segment(a,b);},waypoint);
            m_waypoint=V(waypoint); m_plannedGoal=goal; m_plannedUser=chosen.userID; m_nextPlan=now+.12;
        }
        if (!m_routeValid)
        {
            if(navigate(goal,m_waypoint)) {m_routeValid=true;m_plannedGoal=goal;}
        }
        if (!m_routeValid)
        {
            if (following || ignoreDanger) return fallback(goal,"Blocking route unavailable",following);
            // Brake existing inertia rather than continuing towards an unsafe route.
            m_status="No safe local route - holding position";
            m_velocity=-local->m_vecVelocity().To2D(); m_controlling=true;
            auto command=BlockbotModel::Command(P(m_velocity),cmd->viewangles.y);
            cmd->forwardmove=std::clamp(command.x,-450.f,450.f); cmd->sidemove=std::clamp(command.y,-450.f,450.f);
            return true;
        }
        const Vec3 arrivalGoal=goal;
        if ((m_waypoint-m_plannedGoal).Length2D()>1.f) { goal=m_waypoint; feed={}; }
        const Vec3 delta=goal-here;
        Vec3 look=here+delta.Normalized()*std::min(delta.Length(),32.f);
        if (!space.Segment(P(here),P(look)) || !space.Segment(P(here),P(here+local->m_vecVelocity().To2D()*.15f)))
        {
            if (following || ignoreDanger) return fallback(goal,"Unsafe blocking approach");
            m_routeValid=false; m_nextPlan=0; m_velocity=-local->m_vecVelocity().To2D(); mode="Unsafe approach - braking";
        }
        else m_velocity=response(arrivalGoal);
    }
    if (head && behavior!=1) m_velocity=response(goal);
    m_controlling=true; m_status=mode+": "+chosen.name;
    const auto command=BlockbotModel::Command(P(m_velocity),cmd->viewangles.y);
    cmd->forwardmove=std::clamp(command.x,-450.f,450.f); cmd->sidemove=std::clamp(command.y,-450.f,450.f);
    return true;
}
void CBlockbot::Apply(CUserCmd* cmd)
{
    if (!m_controlling) return;
    // Later aim features can change yaw. Preserve our world-space movement without
    // changing their angles, attack buttons, or any other command fields.
    const auto command=BlockbotModel::Command(P(m_velocity),cmd->viewangles.y);
    cmd->forwardmove=std::clamp(command.x,-450.f,450.f); cmd->sidemove=std::clamp(command.y,-450.f,450.f);
}
