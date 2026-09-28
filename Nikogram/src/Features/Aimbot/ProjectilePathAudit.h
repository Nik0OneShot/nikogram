#pragma once
#include "../Simulation/ProjectileSimulation/ProjectileSimulation.h"
#include "../Simulation/MovementSimulation/PredictionPolicy.h"
#include "ObservationPolicy.h"
#include <chrono>

namespace ProjectilePathAudit
{
    using Clock=std::chrono::steady_clock;
    struct Batch {
        CProjectileSimulation simulation;
        Clock::time_point started=Clock::now();
        Batch()=default;
        Batch(const Batch&)=delete;
        Batch& operator=(const Batch&)=delete;
        ~Batch(){simulation.ReleaseDiagnostic();}
        double Elapsed() const {return std::chrono::duration<double,std::milli>(Clock::now()-started).count();}
        bool Expired() const {return ObservationPolicy::AuditBudgetExpired(Elapsed());}
    };
    struct Result { bool feasible=false,budgetExceeded=false; int trials=0,blocked=0,setupFailed=0,missingPath=0; float hitTime=-1; Vec3 angle={}; double setupMs=0,physicsMs=0,traceMs=0; };
    // Nominal direct-grenade feasibility, not the live solver or a shot command.
    // Other players, spread, damage safety and live angle gates are deliberately not modelled.
    template<class Sample> Result Check(Batch& batch,CTFPlayer* local,CTFWeaponBase* weapon,CTFPlayer* target,
        const Vec3& seed,float startTime,float flight,bool search,Sample sample)
    {
        Result result;
        if(!local || !weapon || !target || weapon->GetWeaponID()!=TF_WEAPON_GRENADELAUNCHER
            || !std::isfinite(flight) || flight<=0 || flight>1.5f || Vars::Visuals::Trajectory::Override.Value) return result;
        auto& simulation=batch.simulation;
        const auto dummy=G::DummyCmd;const float currentTime=I::GlobalVars->curtime;
        PredictionPolicy::ScopeExit restore([&]{simulation.m_pCurrent=nullptr;G::DummyCmd=dummy;I::GlobalVars->curtime=currentTime;});
        const Vec3 offsets[]={{0,0,0},{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{-2,0,0},{2,0,0},{0,-2,0},{0,2,0}};
        const auto mins=target->m_vecMins(),maxs=target->m_vecMaxs();
        for(int candidate=0;candidate<(search?9:1);++candidate) {
            if(batch.Expired()){result.budgetExceeded=true;return result;}
            ++result.trials;const auto angle=seed+offsets[candidate];
            ProjectileInfo info;
            const auto setupStart=Clock::now();
            const bool setup=simulation.GetInfo(local,weapon,angle,info,ProjSimEnum::Redirect|ProjSimEnum::InitCheck|ProjSimEnum::NoRandomAngles|ProjSimEnum::DiagnosticDeterministic)
                && simulation.Initialize(info);
            result.setupMs+=std::chrono::duration<double,std::milli>(Clock::now()-setupStart).count();
            if(!setup) {++result.setupFailed;continue;}
            Vec3 previous=simulation.GetOrigin();
            CTraceFilterWorldAndPropsOnly filter={};filter.pSkip=local;
            const int ticks=std::min(128,int(std::ceil(flight/TICK_INTERVAL)));
            for(int tick=1;tick<=ticks;++tick) {
                if(batch.Expired()){result.budgetExceeded=true;return result;}
                Vec3 targetBefore,targetAfter;
                const float beforeTime=startTime+(tick-1)*TICK_INTERVAL;
                const float afterTime=std::min(startTime+tick*TICK_INTERVAL,startTime+flight);
                if(!sample(beforeTime,targetBefore) || !sample(afterTime,targetAfter)) {++result.missingPath;break;}
                const auto physicsStart=Clock::now();
                simulation.RunTick(info,false);const Vec3 fullNext=simulation.GetOrigin();
                result.physicsMs+=std::chrono::duration<double,std::milli>(Clock::now()-physicsStart).count();
                const float part=(afterTime-beforeTime)/TICK_INTERVAL;
                const Vec3 next=previous+(fullNext-previous)*part;
                const auto traceStart=Clock::now();
                CGameTrace world={};SDK::TraceHull(previous,next,-info.m_vHull,info.m_vHull,MASK_SOLID,&filter,&world);
                result.traceMs+=std::chrono::duration<double,std::milli>(Clock::now()-traceStart).count();
                const auto a=previous-targetBefore,b=next-targetAfter;
                const auto low=mins-info.m_vHull,high=maxs+info.m_vHull;
                const float contact=ObservationPolicy::SegmentBox({a.x,a.y,a.z},{b.x,b.y,b.z},{low.x,low.y,low.z},{high.x,high.y,high.z});
                if(!world.startsolid && !world.allsolid && contact>=0 && (!world.DidHit() || contact<world.fraction)) {
                    result.feasible=true;result.angle=angle;result.hitTime=(tick-1+part*contact)*TICK_INTERVAL;return result;
                }
                if(world.DidHit() || world.startsolid || world.allsolid) {++result.blocked;break;}
                previous=fullNext;
            }
        }
        return result;
    }
}
