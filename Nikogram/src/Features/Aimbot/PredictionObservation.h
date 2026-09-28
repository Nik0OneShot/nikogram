#pragma once
#include "SelfDamageDiagnostics.h"
#include "ObservationPolicy.h"
#include <deque>
#include "../Simulation/MovementSimulation/MovementSimulation.h"
#include "../Simulation/MovementSimulation/PredictionPolicy.h"
#include "ProjectileDiagnostics.h"
#include <chrono>
#include "ProjectilePathAudit.h"

// Network-time position comparison, not damage attribution or a hit-rate measurement.
namespace PredictionObservation
{
    inline void SupportAudit(int command,CTFPlayer* player,const char* phase,float time,const Vec3& origin)
    {
        auto* rules=I::TFGameRules();auto* vectors=rules?rules->GetViewVectors():nullptr;
        if(!vectors) return;
        const bool duck=player->m_bDucked();const float scale=player->m_flModelScale();
        const auto mins=(duck?vectors->m_vDuckHullMin:vectors->m_vHullMin)*scale;
        const auto maxs=(duck?vectors->m_vDuckHullMax:vectors->m_vHullMax)*scale;
        const auto entityMins=player->m_vecMins(),entityMaxs=player->m_vecMaxs();
        SelfDamageDiagnostics::Write("landing_support_hull",std::format(
            "cmd={} entity={} phase={} time={} probe_mins={},{},{} probe_maxs={},{},{} entity_mins={},{},{} entity_maxs={},{},{}",
            command,player->entindex(),phase,time,mins.x,mins.y,mins.z,maxs.x,maxs.y,maxs.z,
            entityMins.x,entityMins.y,entityMins.z,entityMaxs.x,entityMaxs.y,entityMaxs.z));
        CTraceFilterWorldAndPropsOnly filter={};filter.pSkip=player;
        const Vec3 offsets[]={{0,0,0},{.5f,0,0},{-.5f,0,0},{0,.5f,0},{0,-.5f,0}};
        for(int i=0;i<5;++i) {
            const auto point=origin+offsets[i];CGameTrace trace={};
            SDK::TraceHull(point+Vec3(0,0,2),point-Vec3(0,0,4),mins,maxs,player->SolidMask(),&filter,&trace);
            SelfDamageDiagnostics::Write("landing_support_audit",std::format(
                "cmd={} entity={} phase={} time={} probe={} duck={} scale={} origin={},{},{} hit={} hit_entity={} fraction={} startsolid={} allsolid={} normal={},{},{} impact={},{},{} note=reconstructed_world_props_hull",
                command,player->entindex(),phase,time,i,duck,scale,point.x,point.y,point.z,trace.DidHit(),trace.m_pEnt?trace.m_pEnt->entindex():-1,trace.fraction,trace.startsolid,trace.allsolid,
                trace.plane.normal.x,trace.plane.normal.y,trace.plane.normal.z,trace.endpos.x,trace.endpos.y,trace.endpos.z));
        }
    }
    struct PathPoint { float time; Vec3 position,velocity; bool grounded; };
    struct CorridorReplay { float ax,ay,target; };
    inline bool Replay(int command,CTFPlayer* player,float end,bool uncorrected,std::vector<PathPoint>& path,bool nominal=false,const CorridorReplay* corridor=nullptr)
    {
        const auto dummy=G::DummyCmd;
        auto* capture=ProjectileDiagnostics::current;
        const bool active=LandingReplay::active,oldMode=LandingReplay::uncorrected;
        const bool oldHull=LandingReplay::nominalHull;const int oldCommand=LandingReplay::command;
        PredictionPolicy::ScopeExit globals([&]{G::DummyCmd=dummy;ProjectileDiagnostics::current=capture;
            LandingReplay::active=active;LandingReplay::uncorrected=oldMode;LandingReplay::nominalHull=oldHull;LandingReplay::command=oldCommand;});
        ProjectileDiagnostics::current=nullptr;
        LandingReplay::active=true;LandingReplay::uncorrected=uncorrected;
        LandingReplay::nominalHull=nominal;LandingReplay::command=command;
        MoveStorage storage;
        PredictionPolicy::ScopeExit restore([&]{F::MoveSim.Restore(storage);});
        // Start at the network snapshot, including every tick rather than skipping choke ticks.
        if(!F::MoveSim.Initialize(player,storage,true,true,false) || storage.m_bFailed) return false;
        path.push_back({storage.m_flSimTime,storage.m_MoveData.m_vecAbsOrigin,storage.m_MoveData.m_vecVelocity,player->IsOnGround()});
        for(int step=0;step<128 && storage.m_flSimTime<end;++step) {
            if(corridor && player->IsOnGround() && storage.m_MoveData.m_flMaxSpeed>0.f) {
                auto& move=storage.m_MoveData;
                storage.m_CounterStrafe.valid=false;storage.m_flAverageYaw=0;
                const float along=move.m_vecVelocity.x*corridor->ax+move.m_vecVelocity.y*corridor->ay;
                const float position=move.m_vecAbsOrigin.x*corridor->ax+move.m_vecAbsOrigin.y*corridor->ay;
                const float desired=std::clamp((corridor->target-position)/std::max(end-storage.m_flSimTime,TICK_INTERVAL),-move.m_flMaxSpeed,move.m_flMaxSpeed);
                const float change=ObservationPolicy::SteeringStep(along,desired,TICK_INTERVAL,move.m_flMaxSpeed);
                move.m_vecVelocity.x+=corridor->ax*change;move.m_vecVelocity.y+=corridor->ay*change;
                move.m_vecViewAngles.y=Math::VectorAngles(move.m_vecVelocity).y;
                move.m_flForwardMove=move.m_vecVelocity.Length2D();move.m_flSideMove=0;
            }
            F::MoveSim.RunTick(storage,false);
            if(storage.m_bFailed || !std::isfinite(storage.m_flSimTime)) return false;
            if(!path.back().grounded && player->IsOnGround())
                SupportAudit(command,player,nominal?"replay_nominal":uncorrected?"replay_live":"replay_alternative",storage.m_flSimTime,storage.m_MoveData.m_vecAbsOrigin);
            path.push_back({storage.m_flSimTime,storage.m_MoveData.m_vecAbsOrigin,storage.m_MoveData.m_vecVelocity,player->IsOnGround()});
        }
        return !path.empty() && path.back().time>=end;
    }
    inline bool PathAt(const std::vector<PathPoint>& path,float time,Vec3& position)
    {
        for(size_t i=1;i<path.size();++i) if(ObservationPolicy::Bracket(path[i-1].time,time,path[i].time)) {
            position=path[i-1].position+(path[i].position-path[i-1].position)*ObservationPolicy::Weight(path[i-1].time,time,path[i].time);
            return true;
        }
        return false;
    }
    struct Pending
    {
        int command,entity; unsigned long handle;
        float time,previousTime,created;
        Vec3 predicted,previous,baseline;
        bool startGround,predictedGround;
        Vec3 previousVelocity={}; bool previousGround=false,completed=false;
        Vec3 initialVelocity={};
        bool corridorAlternative=false,corridorCollisionClear=false; Vec3 corridorPoint={};
        std::vector<PathPoint> correctedPath,uncorrectedPath;
        std::vector<PathPoint> nominalPath;
        bool corridorReplayValid=false; Vec3 corridorReplayPoint={};
    };
    inline std::deque<Pending> pending;
    inline float lastClock=0.f;
    inline void Poll()
    {
        const float now=I::GlobalVars->curtime;
        if(!SelfDamageDiagnostics::Enabled() || now<lastClock) {pending.clear();lastClock=now;return;}
        lastClock=now;
        for(auto it=pending.begin();it!=pending.end();)
        {
            auto* entity=I::ClientEntityList->GetClientEntity(it->entity);
            const char* discard=nullptr;
            if(!entity || entity->GetRefEHandle().ToInt()!=it->handle || !entity->As<CBaseEntity>()->IsPlayer()) discard="entity_changed";
            else if(entity->IsDormant() || !entity->As<CTFPlayer>()->IsAlive()) discard="unavailable";
            else if(now-it->created>5.f) discard="timeout";
            if(discard) {
                SelfDamageDiagnostics::Write("prediction_observation_discard",std::format("cmd={} entity={} reason={} endpoint_completed={}",it->command,it->entity,discard,it->completed));
                it=pending.erase(it);continue;
            }
            auto* player=entity->As<CTFPlayer>();
            const float time=player->m_flSimulationTime();
            const auto position=player->m_vecOrigin();
            const float gap=time-it->previousTime;
            if(!std::isfinite(time) || gap<0.f || gap>.1f || position.DistTo(it->previous)>128.f) {
                SelfDamageDiagnostics::Write("prediction_observation_discard",std::format("cmd={} entity={} reason=discontinuity gap={}",it->command,it->entity,gap));
                it=pending.erase(it);continue;
            }
            if(gap>0.f && !it->correctedPath.empty()) {
                if(!it->previousGround && player->IsOnGround())
                    SupportAudit(it->command,player,"observed",time,position);
                Vec3 corrected,uncorrected;
                const bool paired=PathAt(it->correctedPath,time,corrected) && PathAt(it->uncorrectedPath,time,uncorrected);
                Vec3 nominal;
                if(paired && PathAt(it->nominalPath,time,nominal))
                    SelfDamageDiagnostics::Write("landing_hull_comparison",std::format(
                        "cmd={} entity={} time={} nominal={},{},{} nominal_error={} compressed_error={} nominal_horizontal={} compressed_horizontal={} note=both_paths_preserve_slide",
                        it->command,it->entity,time,nominal.x,nominal.y,nominal.z,position.DistTo(nominal),position.DistTo(uncorrected),
                        (position-nominal).Length2D(),(position-uncorrected).Length2D()));
                const auto velocity=player->m_vecVelocity();
                SelfDamageDiagnostics::Write("landing_observed_step",std::format(
                    "cmd={} entity={} before_time={} time={} before_ground={} ground={} before_position={},{},{} position={},{},{} before_velocity={},{},{} velocity={},{},{} paired={}",
                    it->command,it->entity,it->previousTime,time,it->previousGround,player->IsOnGround(),
                    it->previous.x,it->previous.y,it->previous.z,position.x,position.y,position.z,
                    it->previousVelocity.x,it->previousVelocity.y,it->previousVelocity.z,velocity.x,velocity.y,velocity.z,paired));
                if(paired) SelfDamageDiagnostics::Write("landing_path_comparison",std::format(
                    "cmd={} entity={} time={} corrected={},{},{} uncorrected={},{},{} corrected_error={} uncorrected_error={} corrected_horizontal={} uncorrected_horizontal={} note=replayed_paths_not_hit_rate",
                    it->command,it->entity,time,corrected.x,corrected.y,corrected.z,uncorrected.x,uncorrected.y,uncorrected.z,
                    position.DistTo(corrected),position.DistTo(uncorrected),(position-corrected).Length2D(),(position-uncorrected).Length2D()));
                if(paired) {
                    const auto error=position-uncorrected;
                    const auto directional=ObservationPolicy::RelativeError(it->initialVelocity.x,it->initialVelocity.y,error.x,error.y);
                    const auto motion=ObservationPolicy::RelativeError(it->initialVelocity.x,it->initialVelocity.y,velocity.x,velocity.y);
                    SelfDamageDiagnostics::Write("landing_direction_error",std::format(
                        "cmd={} entity={} time={} axis_valid={} along_error={} across_error={} vertical_error={} observed_along_speed={} observed_across_speed={} reference=initial_horizontal_velocity live_path=uncorrected",
                        it->command,it->entity,time,directional.valid,directional.along,directional.across,error.z,motion.along,motion.across));
                }
            }
            if(!it->completed && ObservationPolicy::Bracket(it->previousTime,it->time,time)) {
                const float blend=ObservationPolicy::Weight(it->previousTime,it->time,time);
                const auto observed=it->previous+(position-it->previous)*blend;
                const auto error=observed-it->predicted;
                const auto baselineError=observed-it->baseline;
                if(it->corridorReplayValid) SelfDamageDiagnostics::Write("reversal_movement_observed",std::format(
                    "cmd={} entity={} expected_time={} selected_horizontal={} simulated_horizontal={} simulated_total={} simulated={},{},{} note=engine_movement_collision_only_projectile_not_validated",
                    it->command,it->entity,it->time,error.Length2D(),(observed-it->corridorReplayPoint).Length2D(),observed.DistTo(it->corridorReplayPoint),
                    it->corridorReplayPoint.x,it->corridorReplayPoint.y,it->corridorReplayPoint.z));
                if(it->corridorAlternative) SelfDamageDiagnostics::Write("reversal_alternative_observed",std::format(
                    "cmd={} entity={} expected_time={} selected_horizontal={} alternative_horizontal={} improvement={} alternative={},{},{} adjustment_clear={} note=endpoint_adjustment_sweep_only_not_full_trajectory_diagnostic_only",
                    it->command,it->entity,it->time,error.Length2D(),(observed-it->corridorPoint).Length2D(),
                    error.Length2D()-(observed-it->corridorPoint).Length2D(),it->corridorPoint.x,it->corridorPoint.y,it->corridorPoint.z,it->corridorCollisionClear));
                SelfDamageDiagnostics::Write("prediction_baseline",std::format(
                    "cmd={} entity={} type=constant_velocity category={} expected_time={} baseline={},{},{} error={},{},{} horizontal_error={} selected_horizontal_error={} horizontal_improvement={} total_error={} selected_total_error={} note=no_gravity_or_collision",
                    it->command,it->entity,it->startGround?"grounded":it->predictedGround?"landing":"airborne",it->time,
                    it->baseline.x,it->baseline.y,it->baseline.z,baselineError.x,baselineError.y,baselineError.z,baselineError.Length2D(),error.Length2D(),
                    ObservationPolicy::Improvement(baselineError.Length2D(),error.Length2D()),baselineError.Length(),error.Length()));
                SelfDamageDiagnostics::Write("prediction_observed",std::format(
                    "cmd={} entity={} category={} expected_time={} before_time={} after_time={} interpolation={} observed_ground_after={} predicted={},{},{} observed={},{},{} error={},{},{} horizontal_error={} total_error={}",
                    it->command,it->entity,it->startGround?"grounded":it->predictedGround?"landing":"airborne",it->time,it->previousTime,time,blend,player->IsOnGround(),
                    it->predicted.x,it->predicted.y,it->predicted.z,observed.x,observed.y,observed.z,error.x,error.y,error.z,error.Length2D(),error.Length()));
                it->completed=true;
            }
            if(it->completed && (it->correctedPath.empty() || time>=it->time+.25f)) {
                if(!it->correctedPath.empty()) SelfDamageDiagnostics::Write("landing_observation_complete",std::format("cmd={} entity={} time={}",it->command,it->entity,time));
                it=pending.erase(it);continue;
            }
            if(gap>0.f){it->previousTime=time;it->previous=position;it->previousVelocity=player->m_vecVelocity();it->previousGround=player->IsOnGround();}
            ++it;
        }
    }
    inline void Queue(int command,int index,unsigned long handle,float time,const Vec3& predicted,bool startGround,bool predictedGround,
        CTFPlayer* local,CTFWeaponBase* weapon,const Vec3& shotAngle,const Vec3& aimPoint,float flight)
    {
        if(!SelfDamageDiagnostics::Enabled()) return;
        auto* entity=I::ClientEntityList->GetClientEntity(index);
        if(!entity || !entity->As<CBaseEntity>()->IsPlayer() || entity->GetRefEHandle().ToInt()!=handle || pending.size()>=32) return;
        auto* player=entity->As<CTFPlayer>();
        const float current=player->m_flSimulationTime();
        if(!std::isfinite(time) || time<=current || time-current>3.f) return;
        const auto origin=player->m_vecOrigin(),velocity=player->m_vecVelocity();
        const float horizon=time-current;
        const Vec3 baseline={ObservationPolicy::Linear(origin.x,velocity.x,horizon),ObservationPolicy::Linear(origin.y,velocity.y,horizon),ObservationPolicy::Linear(origin.z,velocity.z,horizon)};
        pending.push_back({command,index,handle,time,current,I::GlobalVars->curtime,predicted,origin,baseline,startGround,predictedGround});
        auto& item=pending.back();
        item.previousVelocity=velocity;item.previousGround=player->IsOnGround();
        item.initialVelocity=velocity;
        static float lastMovementReplay=-10.f;
        const float movementClock=I::GlobalVars->curtime;
        if(movementClock<lastMovementReplay) lastMovementReplay=movementClock-1.f;
        if(startGround && horizon<=1.5f) {
            const auto audit=F::MoveSim.DiagnosticCounterAudit(player);
            const bool eligible=audit.rejectionMask && !audit.rapid && audit.reversals>=2
                && audit.duration>=.45f && audit.width>=6.f && audit.width<=160.f;
            const float projection=predicted.x*audit.axisX+predicted.y*audit.axisY;
            const float shift=eligible?ObservationPolicy::BoundedCorridorShift(projection,audit.low,audit.high):0.f;
            item.corridorAlternative=eligible && std::abs(shift)>.01f;
            item.corridorPoint=predicted+Vec3(audit.axisX*shift,audit.axisY*shift,0);
            if(item.corridorAlternative && movementClock-lastMovementReplay>=1.f) {
                lastMovementReplay=movementClock;
                const auto started=std::chrono::steady_clock::now();
                const CorridorReplay corridor={audit.axisX,audit.axisY,item.corridorPoint.x*audit.axisX+item.corridorPoint.y*audit.axisY};
                std::vector<PathPoint> alternative,control;
                const bool controlOk=Replay(command,player,time,true,control);
                const bool alternativeOk=controlOk && Replay(command,player,time,true,alternative,false,&corridor);
                Vec3 controlPoint;
                const bool controlMatched=controlOk && PathAt(control,time,controlPoint);
                item.corridorReplayValid=alternativeOk && PathAt(alternative,time,item.corridorReplayPoint);
                if(controlMatched && item.corridorReplayValid) {
                    const auto projectileStart=std::chrono::steady_clock::now();
                    ProjectilePathAudit::Batch projectileBatch;
                    const auto eye=local->GetShootPos();
                    const Vec3 offset=item.corridorReplayPoint-predicted;
                    const auto oldBearing=Math::CalcAngle(eye,aimPoint),newBearing=Math::CalcAngle(eye,aimPoint+offset);
                    Vec3 seed=shotAngle;
                    seed.x+=newBearing.x-oldBearing.x;seed.y+=std::remainder(newBearing.y-oldBearing.y,360.f);
                    const auto controlResult=ProjectilePathAudit::Check(projectileBatch,local,weapon,player,shotAngle,time-flight,flight,false,
                        [&](float t,Vec3& point){return PathAt(control,t,point);});
                    const auto alternativeResult=ProjectilePathAudit::Check(projectileBatch,local,weapon,player,seed,time-flight,flight,true,
                        [&](float t,Vec3& point){return PathAt(alternative,t,point);});
                    const auto cleanupStart=std::chrono::steady_clock::now();
                    projectileBatch.simulation.ReleaseDiagnostic();
                    const double cleanupMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cleanupStart).count();
                    SelfDamageDiagnostics::Write("reversal_projectile_timing",std::format(
                        "cmd={} entity={} control_budget_exceeded={} alternative_budget_exceeded={} control_setup_ms={} alternative_setup_ms={} control_physics_ms={} alternative_physics_ms={} control_trace_ms={} alternative_trace_ms={} cleanup_ms={} budget_ms=3 budget_kind=cooperative_nonpreemptive resource_scope=shared_pair",
                        command,index,controlResult.budgetExceeded,alternativeResult.budgetExceeded,controlResult.setupMs,alternativeResult.setupMs,
                        controlResult.physicsMs,alternativeResult.physicsMs,controlResult.traceMs,alternativeResult.traceMs,cleanupMs));
                    SelfDamageDiagnostics::Write("reversal_projectile_audit",std::format(
                        "cmd={} entity={} control_feasible={} alternative_feasible={} control_trials={} alternative_trials={} control_blocked={} alternative_blocked={} control_setup_failed={} alternative_setup_failed={} control_missing_path={} alternative_missing_path={} hit_time={} angle={},{} elapsed_ms_precise={} note=nominal_direct_world_props_moving_aabb_no_spread_other_players_self_damage_or_live_angle_gates_negative_not_exhaustive",
                        command,index,controlResult.feasible,alternativeResult.feasible,controlResult.trials,alternativeResult.trials,controlResult.blocked,alternativeResult.blocked,
                        controlResult.setupFailed,alternativeResult.setupFailed,controlResult.missingPath,alternativeResult.missingPath,alternativeResult.hitTime,
                        alternativeResult.angle.x,alternativeResult.angle.y,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-projectileStart).count()));
                }
                const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
                SelfDamageDiagnostics::Write("reversal_movement_queued",std::format(
                    "cmd={} entity={} valid={} control_valid={} selected_replay_delta={} points={} elapsed_ms_precise={} note=steered_engine_movement_diagnostic_only_projectile_not_validated",
                    command,index,item.corridorReplayValid,controlMatched,controlMatched?controlPoint.DistTo(predicted):-1.f,alternative.size(),elapsed));
            }
            if(item.corridorAlternative) {
                auto* rules=I::TFGameRules();auto* vectors=rules?rules->GetViewVectors():nullptr;
                if(vectors && predictedGround) {
                    const bool duck=player->m_bDucked();const float scale=player->m_flModelScale();
                    const auto mins=((duck?vectors->m_vDuckHullMin:vectors->m_vHullMin)+PLAYER_ORIGIN_COMPRESSION)*scale;
                    const auto maxs=((duck?vectors->m_vDuckHullMax:vectors->m_vHullMax)-PLAYER_ORIGIN_COMPRESSION)*scale;
                    CTraceFilterWorldAndPropsOnly filter={};filter.pSkip=player;CGameTrace sweep={},support={};
                    SDK::TraceHull(predicted,item.corridorPoint,mins,maxs,player->SolidMask(),&filter,&sweep);
                    SDK::TraceHull(item.corridorPoint+Vec3(0,0,2),item.corridorPoint-Vec3(0,0,4),mins,maxs,player->SolidMask(),&filter,&support);
                    item.corridorCollisionClear=ObservationPolicy::AdjustmentClear(sweep.fraction,sweep.startsolid,sweep.allsolid,
                        support.DidHit() && !support.startsolid && !support.allsolid,support.plane.normal.z);
                    SelfDamageDiagnostics::Write("reversal_adjustment_collision",std::format(
                        "cmd={} entity={} clear={} fraction={} startsolid={} allsolid={} support_hit={} support_startsolid={} support_allsolid={} normal_z={} note=world_props_compressed_hull_endpoint_adjustment_only",
                        command,index,item.corridorCollisionClear,sweep.fraction,sweep.startsolid,sweep.allsolid,support.DidHit(),support.startsolid,support.allsolid,support.plane.normal.z));
                }
            }
            SelfDamageDiagnostics::Write("reversal_alternative_queued",std::format(
                "cmd={} entity={} eligible={} shifted={} reason={} rejection_mask={} reversals={} age={} width={} drift={} axis={},{} low={} high={} shift={} note=bounded_64_units_diagnostic_only_mask_count_1_samples_2_stale_4_narrow_8_wide_16_drift_32",
                command,index,eligible,item.corridorAlternative,audit.reason,audit.rejectionMask,audit.reversals,audit.age,audit.width,audit.drift,audit.axisX,audit.axisY,audit.low,audit.high,shift));
        }
        // Include airborne endpoints: sliding off can precede the selected intercept.
        // At most one paired replay per second, only short airborne-start forecasts.
        static float lastReplay=-10.f;
        const float clock=I::GlobalVars->curtime;
        if(clock<lastReplay) lastReplay=clock-1.f;
        if(ObservationPolicy::LandingReplayEligible(startGround,predictedGround,horizon,clock-lastReplay)) {
            lastReplay=clock;
            const auto replayStarted=std::chrono::steady_clock::now();
            const bool ok=Replay(command,player,time+.25f,false,item.correctedPath)
                && Replay(command,player,time+.25f,true,item.uncorrectedPath);
            if(!ok){item.correctedPath.clear();item.uncorrectedPath.clear();}
            const bool nominalOk=ok && Replay(command,player,time+.25f,true,item.nominalPath,true);
            if(!nominalOk) item.nominalPath.clear();
            SelfDamageDiagnostics::Write("landing_nominal_replay",std::format("cmd={} entity={} valid={} points={} note=nominal_hull_only_no_velocity_correction",command,index,nominalOk,item.nominalPath.size()));
            Vec3 replayPosition;
            const bool matched=ok && PathAt(item.uncorrectedPath,time,replayPosition);
            SelfDamageDiagnostics::Write("landing_replay_queued",std::format(
                "cmd={} entity={} valid={} end_time={} corrected_points={} uncorrected_points={} selected_replay_delta={} elapsed_ms_precise={} live_path=uncorrected alternative=corrected note=only_velocity_correction_enabled_in_alternative",
                command,index,matched,time+.25f,item.correctedPath.size(),item.uncorrectedPath.size(),matched?replayPosition.DistTo(predicted):-1.f,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-replayStarted).count()));
            for(int mode=0;mode<2;++mode) {
                const auto& path=mode?item.uncorrectedPath:item.correctedPath;
                for(size_t i=1;i<path.size();++i) if(path[i-1].grounded!=path[i].grounded) {
                    const auto& before=path[i-1];const auto& after=path[i];
                    SelfDamageDiagnostics::Write("landing_replay_contact",std::format(
                        "cmd={} entity={} uncorrected={} before_time={} time={} before_ground={} ground={} before_position={},{},{} position={},{},{} before_velocity={},{},{} velocity={},{},{}",
                        command,index,bool(mode),before.time,after.time,before.grounded,after.grounded,
                        before.position.x,before.position.y,before.position.z,after.position.x,after.position.y,after.position.z,
                        before.velocity.x,before.velocity.y,before.velocity.z,after.velocity.x,after.velocity.y,after.velocity.z));
                }
            }
        }
        SelfDamageDiagnostics::Write("prediction_observation_queued",std::format("cmd={} entity={} expected_time={} current_time={} horizon={} attack_requested=true",command,index,time,current,time-current));
    }
}
