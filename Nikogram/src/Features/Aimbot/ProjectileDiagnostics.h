#pragma once
#include "SelfDamageDiagnostics.h"
#include <array>
#include <chrono>

// Sample whole commands, including commands where no attack was generated.
// Counters do not perform extra traces or alter the simulation.
namespace ProjectileDiagnostics
{
    enum Counter { Tests, SetupFailed, Obstructed, NoTicks, VisibilityFailed, SafetyRejected, Hit, Miss, AngleRejected, Count };
    struct Capture { int command=0, candidates=0, traces=0, angles=0,movement=0,steps=0,weaponReviews=0,headReviews=0,arcCandidates=0,targetReviews=0; int grenades[2]={},points[2]={},transitions[2]={},angleDetails[2]={},blockedDetails[2]={}; int ledge[3]={}; int splashDetails=0,visibilityDetails=0; bool preview=false,adaptive=false; std::array<int,Count> counts{}; };
    enum ProfileStage { MovementInit, MovementTick, SplashSetup, DirectSearch, SplashSearch, SplashGeometry, SplashSampling, SplashSelection, SplashAngles, SplashValidation, MovementReuseCapture, MovementReuseReplay, ProfileCount };
    struct ReuseCounts { int hits=0,captures=0,invalidations=0,validations=0,mismatches=0; size_t bytes=0; };
    inline thread_local ReuseCounts reuse{};
    struct SplashCounts { int faces=0, generated=0, considered=0, radiusRejected=0, angleSolves=0, shortlisted=0, validations=0, setupTraces=0, angleFiltered=0; int facePoints=0,chosenSource=-1; int traceWorkerSubmitted=0,traceWorkerReady=0,traceWorkerFallback=0; int poolRegenerated=0,regenerated=0; int edgeSamples=0,edgePoints=0,unreachable=0,corners=0,cornerTraces=0,refineMisses=0,refineValidations=0,refineHits=0; float edgeStep=0.f; };
    inline thread_local SplashCounts splash{};
    struct ProfileData { std::array<long long,ProfileCount> microseconds{}; std::array<int,ProfileCount> calls{}; };
    inline thread_local ProfileData profile{};
    inline thread_local Capture* current=nullptr;
    struct Profile
    {
        Capture* owner=current; ProfileStage stage;
        std::chrono::steady_clock::time_point start{};
        explicit Profile(ProfileStage value):stage(value)
        {if(owner) start=std::chrono::steady_clock::now();}
        ~Profile()
        {
            if(!owner || current!=owner) return;
            profile.microseconds[stage]+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
            ++profile.calls[stage];
        }
    };
    inline bool TransitionSample()
    {
        // Independent of early movement/probe budgets, split actual and preview.
        if(!current || !SelfDamageDiagnostics::Enabled()) return false;
        return current->transitions[current->preview?1:0]++<8;
    }
    inline bool MovementSample(bool step=false)
    {
        if(!current || !SelfDamageDiagnostics::Enabled()) return false;
        return step ? current->steps++<12 : current->movement++<24;
    }
    // Separate budgets keep support probes from suppressing transition records.
    inline bool LedgeSample(int category)
    {
        if(!current || !SelfDamageDiagnostics::Enabled()) return false;
        constexpr int limits[]={6,12,8};
        return category>=0 && category<3 && current->ledge[category]++<limits[category];
    }
    inline void Ledge(const char* event,const std::string& detail)
    {
        if(current) SelfDamageDiagnostics::Write(event,std::format("cmd={} preview={} adaptive={} {}",current->command,current->preview,current->adaptive,detail));
    }
    inline void Add(Counter counter) { if(current) ++current->counts[counter]; }
    // 0.7.0.39: per-command detail on splash candidates (bounded).
    inline void SplashCandidate(int source,const Vec3& point,const Vec3& normal)
    {
        if(!current || current->splashDetails++>=16) return;
        Ledge("splash_candidate",std::format("source={} point={},{},{} normal={},{},{}",source,point.x,point.y,point.z,normal.x,normal.y,normal.z));
    }
    inline void SplashVisibility(const Vec3& aim,const Vec3& impact,const Vec3& normal,const Vec3& start,const Vec3& eye,const CGameTrace& trace)
    {
        if(!current || current->visibilityDetails++>=6) return;
        Ledge("splash_visibility_failed",std::format("aim={},{},{} impact={},{},{} impact_normal={},{},{} start={},{},{} eye={},{},{} end={},{},{} fraction={} hit_entity={} hit_class={} contents={} surface={} startsolid={}",
            aim.x,aim.y,aim.z,impact.x,impact.y,impact.z,normal.x,normal.y,normal.z,start.x,start.y,start.z,eye.x,eye.y,eye.z,
            trace.endpos.x,trace.endpos.y,trace.endpos.z,trace.fraction,trace.m_pEnt?trace.m_pEnt->entindex():-1,trace.m_pEnt?int(trace.m_pEnt->GetClassID()):-1,trace.contents,trace.surface.name?trace.surface.name:"",trace.startsolid));
    }
    inline void AngleRejection(int target,int type,const Vec3& angle,const Vec3& point,const Vec3& origin,const char* phase)
    {
        if(!current || current->angleDetails[current->preview?1:0]++>=3) return;
        const float fov=Math::CalcFov(I::EngineClient->GetViewAngles(),angle);
        Ledge("projectile_point_angle_rejected",std::format(
            "phase={} target={} type={} lead_mode={} shot_fov={} limit={} applied_angle={},{},{} aim_point={},{},{} predicted_origin={},{},{} note=no_collision_test_performed",
            phase,target,type,Vars::Aimbot::General::LeadAndRestrict.Value,fov,Vars::Aimbot::Projectile::AimFOV.Value,
            angle.x,angle.y,angle.z,point.x,point.y,point.z,origin.x,origin.y,origin.z));
    }
    inline void BlockedPoint(int target,int type,const Vec3& muzzle,const Vec3& point,const Vec3& origin,const Vec3& angle,const CGameTrace& trace)
    {
        if(!current || current->blockedDetails[current->preview?1:0]++>=3) return;
        Ledge("projectile_point_blocked",std::format(
            "phase=straight_pretrace target={} type={} shot_fov={} limit={} muzzle={},{},{} aim_point={},{},{} predicted_origin={},{},{} hit_entity={} fraction={} corrected_fraction={} startsolid={} allsolid={} obstruction={},{},{} normal={},{},{} remaining_distance={}",
            target,type,Math::CalcFov(I::EngineClient->GetViewAngles(),angle),Vars::Aimbot::Projectile::AimFOV.Value,
            muzzle.x,muzzle.y,muzzle.z,point.x,point.y,point.z,origin.x,origin.y,origin.z,
            trace.m_pEnt?trace.m_pEnt->entindex():-1,trace.fraction,Math::FullFraction(muzzle,point,trace),
            trace.startsolid,trace.allsolid,trace.endpos.x,trace.endpos.y,trace.endpos.z,
            trace.plane.normal.x,trace.plane.normal.y,trace.plane.normal.z,trace.endpos.DistTo(point)));
    }
    struct Command
    {
        Capture data{}; Capture* previous=current; CUserCmd* cmd;
        Command(CUserCmd* value):cmd(value)
        {
            current=nullptr;
            if(!SelfDamageDiagnostics::Enabled()) return;
            static unsigned long long last=0;
            static bool wasReady=false;
            const auto now=GetTickCount64();
            const bool ready=Vars::Aimbot::General::AimType.Value && G::CanPrimaryAttack && F::AimbotGlobal.ShouldAim();
            const bool readyEdge=ready&&!wasReady; wasReady=ready;
            if(last && now>=last && now-last<(readyEdge?100ULL:500ULL)) return;
            last=now; data.command=value->command_number; current=&data; profile={}; splash={}; reuse={};
            SelfDamageDiagnostics::LastProjectileSampleCommand=data.command;
            SelfDamageDiagnostics::Write("projectile_begin",std::format("cmd={} aim={} should_aim={} can_fire={} autoshoot={} buttons={} original={} self_damage={} protection={}",data.command,Vars::Aimbot::General::AimType.Value,F::AimbotGlobal.ShouldAim(),G::CanPrimaryAttack,Vars::Aimbot::General::AutoShoot.Value,cmd->buttons,G::OriginalCmd.buttons,bool(Vars::Aimbot::Projectile::Modifiers.Value&Vars::Aimbot::Projectile::ModifiersEnum::PreventSelfDamage),Vars::Aimbot::Projectile::SelfDamageProtection.Value));
        }
        ~Command()
        {
            if(current==&data) SelfDamageDiagnostics::Write("movement_reuse_profile",std::format("cmd={} version=136 hits={} captures={} invalidations={} validations={} mismatches={} captured_bytes={} capture_us={} replay_us={} note=included_in_movement_tick",data.command,reuse.hits,reuse.captures,reuse.invalidations,reuse.validations,reuse.mismatches,reuse.bytes,profile.microseconds[MovementReuseCapture],profile.microseconds[MovementReuseReplay]));
            if(current==&data) SelfDamageDiagnostics::Write("splash_profile",std::format("cmd={} version=149 inclusive=1 geometry_us={} sampling_us={} selection_us={} angles_us={} validation_us={} faces={} generated={} considered={} radius_rejected={} angle_solves={} shortlisted={} validations={} setup_traces={} angle_filtered={} face_points={} chosen_source={} trace_worker_submitted={} trace_worker_ready={} trace_worker_fallback={} pool_regenerated={} regenerated={} edge_samples={} edge_points={} edge_step={} unreachable={} corners={} corner_traces={} refine_misses={} refine_validations={} refine_hits={}",data.command,profile.microseconds[SplashGeometry],profile.microseconds[SplashSampling],profile.microseconds[SplashSelection],profile.microseconds[SplashAngles],profile.microseconds[SplashValidation],splash.faces,splash.generated,splash.considered,splash.radiusRejected,splash.angleSolves,splash.shortlisted,splash.validations,splash.setupTraces,splash.angleFiltered,splash.facePoints,splash.chosenSource,splash.traceWorkerSubmitted,splash.traceWorkerReady,splash.traceWorkerFallback,splash.poolRegenerated,splash.regenerated,splash.edgeSamples,splash.edgePoints,splash.edgeStep,splash.unreachable,splash.corners,splash.cornerTraces,splash.refineMisses,splash.refineValidations,splash.refineHits));
            if(current==&data) SelfDamageDiagnostics::Write("projectile_profile",std::format("cmd={} inclusive=1 movement_init_us={} movement_init_calls={} movement_tick_us={} movement_tick_calls={} splash_setup_us={} splash_setup_calls={} direct_search_us={} direct_search_calls={} splash_search_us={} splash_search_calls={}",data.command,profile.microseconds[MovementInit],profile.calls[MovementInit],profile.microseconds[MovementTick],profile.calls[MovementTick],profile.microseconds[SplashSetup],profile.calls[SplashSetup],profile.microseconds[DirectSearch],profile.calls[DirectSearch],profile.microseconds[SplashSearch],profile.calls[SplashSearch]));
            if(current==&data) SelfDamageDiagnostics::Write("projectile_end",std::format("cmd={} candidates={} buttons={} attacking={} can_fire={}",data.command,data.candidates,cmd->buttons,G::Attacking,G::CanPrimaryAttack));
            current=previous;
        }
    };
    inline void Stage(const char* reason,int value=0)
    {
        if(current) SelfDamageDiagnostics::Write("projectile_stage",std::format("cmd={} reason={} value={}",current->command,reason,value));
    }
    inline void Trace(const char* phase,const CGameTrace& trace,int target,int type,int step,int expected,int tolerance)
    {
        if(!current || current->traces++>=4) return;
        SelfDamageDiagnostics::Write("projectile_trace",std::format("cmd={} phase={} target={} type={} step={} expected={} tolerance={} hit_entity={} fraction={} startsolid={} allsolid={} end={},{},{}",current->command,phase,target,type,step,expected,tolerance,trace.m_pEnt?trace.m_pEnt->entindex():-1,trace.fraction,trace.startsolid,trace.allsolid,trace.endpos.x,trace.endpos.y,trace.endpos.z));
    }
    struct Candidate
    {
        int entity,weapon,result=0; float distance; const char* reason="no_solution"; bool preview=false, adaptive=false;
        Candidate(CBaseEntity* target,CTFPlayer* local,CTFWeaponBase* gun):entity(target->entindex()),weapon(gun->GetWeaponID()),distance(target->GetAbsOrigin().DistTo(local->GetAbsOrigin()))
        { if(current) {++current->candidates;current->counts={};} }
        ~Candidate()
        {
            if(!current || current->candidates>8) return;
            const auto& c=current->counts;
            SelfDamageDiagnostics::Write("projectile_candidate",std::format("cmd={} entity={} weapon={} distance={} reason={} result={} tests={} setup_failed={} obstructed={} no_ticks={} visibility_failed={} safety_rejected={} hits={} misses={} angle_rejected={} preview={} adaptive={} lead_mode={} aim_fov={} general_fov={}",current->command,entity,weapon,distance,reason,result,c[Tests],c[SetupFailed],c[Obstructed],c[NoTicks],c[VisibilityFailed],c[SafetyRejected],c[Hit],c[Miss],c[AngleRejected],preview,adaptive,Vars::Aimbot::General::LeadAndRestrict.Value,Vars::Aimbot::Projectile::AimFOV.Value,Vars::Aimbot::General::AimFOV.Value));
        }
    };
}
