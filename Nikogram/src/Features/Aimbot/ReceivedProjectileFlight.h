#pragma once
#include "../Simulation/ProjectileSimulation/ProjectileSimulation.h"
#include "ProjectilePerformancePolicy.h"
#include "IncomingPhysicsWorld.h"
#include <memory>

namespace ReceivedProjectileFlight {
// Copied, command-local trajectory. No simulator or native entity survives here.
// Physics uses the received velocity, never a second launch kick. The angular
// velocity is unobserved, so these paths are hypotheses, never strong evidence.
struct Path {
    struct Node {float time=0;Vec3 position,velocity,angles,spin;bool touched=false;float contactEnvelope=0;};
    std::array<Node,65> nodes{};size_t count=0;
    float end=0;bool complete=false,worldContact=false,terminal=false,nativeWorld=false;
    Vec3 normal;
    Vec3 Position(float time)const{
        if(!count)return {};if(time<=nodes[0].time)return nodes[0].position;
        for(size_t i=1;i<count;++i)if(time<=nodes[i].time){
            const auto& a=nodes[i-1];const auto& b=nodes[i];
            return a.position+(b.position-a.position)*std::clamp((time-a.time)/(b.time-a.time),0.f,1.f);
        }
        return nodes[count-1].position;
    }
    Node State(float time)const{for(size_t i=1;i<count;++i)if(time<nodes[i].time){auto n=nodes[i-1];n.position=Position(time);return n;}return count?nodes[count-1]:Node{};}
};
struct Key {
    unsigned handle=0,owner=0,launcher=0;int deflections=0;float sim=0,gravity=0;
    Vec3 origin,velocity,angles;uint32_t model=0;Vec3 spin;bool useSpin=false,touched=false,noBounce=false,wallShatter=false,terminalWorld=false,analytic=false;float velocityUncertainty=0;
    bool operator==(const Key&)const=default;
};
struct Entry{Key key;std::shared_ptr<const Path> path;};
inline std::shared_ptr<const Path> Pipe(const Key& key,CBaseEntity* entity,CTFPlayer* owner,CTFWeaponBase* weapon){
    struct Cache{int command=-1,server=-1;void* channel=nullptr;std::array<Entry,16> entries{};size_t count=0;};
    static Cache cache;
    const int command=G::CurrentUserCmd?G::CurrentUserCmd->command_number:G::OriginalCmd.command_number;
    auto* channel=I::EngineClient->GetNetChannelInfo();
    if(cache.command!=command||cache.server!=I::ClientState->m_nServerCount||cache.channel!=channel){cache={};cache.command=command;cache.server=I::ClientState->m_nServerCount;cache.channel=channel;}
    for(size_t i=0;i<cache.count;++i)if(cache.entries[i].key==key)return cache.entries[i].path;
    auto path=std::make_shared<Path>();
    if(cache.count==cache.entries.size())return path;
    cache.entries[cache.count++]={key,path};
    if(!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Prediction))return path;
    if(key.analytic){
        path->nodes[path->count++]={0,key.origin,key.velocity,key.angles,{}};
        CTraceFilterWorldAndPropsOnly filter;filter.pSkip=entity;
        for(size_t step=1;step<path->nodes.size();++step){
            if(!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Prediction)
                ||!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Geometry))return path;
            const auto before=path->nodes[path->count-1];auto velocity=before.velocity;
            auto next=before.position+velocity*TICK_INTERVAL;next.z-=key.gravity*TICK_INTERVAL*TICK_INTERVAL*.5f;velocity.z-=key.gravity*TICK_INTERVAL;
            const float pad=1.f+key.velocityUncertainty*float(step)*TICK_INTERVAL;
            CGameTrace trace={};SDK::TraceHull(before.position,next,Vec3(-pad,-pad,-pad),Vec3(pad,pad,pad),MASK_SOLID,&filter,&trace);
            if(trace.startsolid||trace.allsolid)return path;
            const float time=float(step)*TICK_INTERVAL;
            if(trace.DidHit()){
                path->worldContact=true;path->normal=trace.plane.normal;path->end=(float(step)-1+trace.fraction)*TICK_INTERVAL;
                path->nodes[path->count++]={path->end,trace.endpos,velocity,key.angles,{}};
                path->terminal=key.terminalWorld;path->complete=key.terminalWorld;return path;
            }
            path->nodes[path->count++]={time,next,velocity,key.angles,{}};path->end=time;
            if(time>=.9f){path->complete=true;break;}
        }
        return path;
    }
    CProjectileSimulation simulator;
    struct Release{CProjectileSimulation& sim;~Release(){sim.ReleaseDiagnostic();}} release{simulator};
    ProjectileInfo info;info.m_pOwner=owner;info.m_pWeapon=weapon;info.m_uType=key.model;
    info.m_vPos=key.origin;info.m_vAng=Math::VectorAngles(key.velocity);info.m_flVelocity=key.velocity.Length();
    info.m_flGravity=key.gravity;info.m_iFlags=ProjSimEnum::DiagnosticDeterministic;
    if(!simulator.Initialize(info,false,true)||!simulator.m_pObj)return path;
    // Initialize historically advances physics once. Reset that step and replace
    // its reconstructed orientation with the copied network orientation.
    Vec3 spin;simulator.m_pObj->GetVelocity(nullptr,&spin);
    if(key.useSpin)spin=key.spin;
    simulator.m_pObj->SetPosition(key.origin,key.angles,true);
    simulator.m_pObj->SetVelocity(&key.velocity,&spin);
    simulator.m_tObj={key.origin,key.velocity};simulator.m_pEnv->ResetSimulationClock();
    // Scene is destroyed before Release, so callbacks and static clones never
    // outlive the owned simulation. Borrowed geometry is used synchronously.
    IncomingPhysicsWorld::Scene scene;
    path->nativeWorld=!key.terminalWorld&&scene.Build(simulator,entity->GetModel(),key.origin,key.angles,key.velocity,spin);
    bool touched=key.touched;scene.recorder.touched=touched;scene.recorder.noBounce=key.noBounce;
    path->nodes[path->count++]={0,key.origin,key.velocity,key.angles,spin,touched};
    CTraceFilterWorldAndPropsOnly filter;filter.pSkip=entity;
    for(size_t step=1;step<path->nodes.size();++step){
        if(!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Prediction)
            ||!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Geometry))return path;
        scene.recorder.contacts=0;scene.recorder.invalid=false;
        simulator.RunTick(info,false);const auto next=simulator.GetOrigin();const float time=float(step)*TICK_INTERVAL;
        if(!IncomingPhysicsWorld::Finite(next)||!IncomingPhysicsWorld::Finite(simulator.GetVelocity()))return path;
        if(scene.ready&&(scene.recorder.invalid||scene.CrossesGap(path->nodes[path->count-1].position,next)))return path;
        Vec3 angles,angular;simulator.m_pObj->GetPosition(nullptr,&angles);simulator.m_pObj->GetVelocity(nullptr,&angular);
        CGameTrace trace={};SDK::TraceHull(path->nodes[path->count-1].position,next,Vec3(-2,-2,-2),Vec3(2,2,2),MASK_SOLID,&filter,&trace);
        if(trace.startsolid||trace.allsolid)return path;
        if(scene.ready&&scene.recorder.contacts){
            if(trace.DidHit()&&(!trace.m_pEnt||trace.m_pEnt->entindex()!=0||trace.hitbox>0))return path;
            path->worldContact=true;path->normal=scene.recorder.normal;
            // Callback-owned first-contact damping ran at the native contact
            // phase. No substep timestamp is exposed: nearby player contact
            // ordering stays unresolved across this entire interval.
            touched=true;
            const float envelope=std::max(scene.maxSpeed,path->nodes[path->count-1].velocity.Length())*TICK_INTERVAL
                +scene.gravityMagnitude*TICK_INTERVAL*TICK_INTERVAL+16.f;
            path->nodes[path->count++]={time,next,simulator.GetVelocity(),angles,angular,touched,envelope};path->end=time;
            if(key.wallShatter){path->terminal=true;path->complete=true;return path;}
            if(time>=.9f){path->complete=true;break;}
            continue;
        }
        if(trace.DidHit()){
            path->worldContact=true;path->normal=trace.plane.normal;
            path->end=(float(step)-1+trace.fraction)*TICK_INTERVAL;
            path->nodes[path->count++]={path->end,trace.endpos,simulator.GetVelocity(),angles,angular,touched};
            if(key.terminalWorld){path->terminal=true;path->complete=true;return path;}
            // No owned native contact explained this world trace.
            // A boundary here is unresolved, never a fabricated pipe explosion.
            return path;
        }
        path->nodes[path->count++]={time,next,simulator.GetVelocity(),angles,angular,touched};path->end=time;
        if(time>=.9f){path->complete=true;break;}
    }
    return path;
}
}
