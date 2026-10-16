#pragma once
#include "IncomingPhysicsABI.h"
#include "ProjectilePerformancePolicy.h"
#include "KnockbackPolicy.h"

namespace IncomingPhysicsWorld {
inline bool Finite(const Vec3& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
// Own callbacks record geometry and damp only the owned body. They never call game Touch/Explode or
// install a handler belonging to the live physics environment.
struct Recorder final:IPhysicsCollisionEvent {
    IPhysicsObject* projectile=nullptr;unsigned contacts=0;bool invalid=false,touched=false,noBounce=false;
    Vec3 point,normal;
    void PreCollision(vcollisionevent_t*)override{}
    void PostCollision(vcollisionevent_t* e)override{
        if(!e||!e->isCollision)return;
        int index=e->pObjects[0]==projectile?0:e->pObjects[1]==projectile?1:-1;if(index<0)return;
        // Owned clone tags distinguish map walls from props whose game callback
        // could detonate a pipe. Never inspect opaque live prop gameData here.
        auto* other=e->pObjects[1-index];if(!other||other->GetGameIndex()!=1){invalid=true;return;}
        if(!e->pInternalData||++contacts>2){invalid=true;return;}
        e->pInternalData->GetContactPoint(point);e->pInternalData->GetSurfaceNormal(normal);if(index==0)normal*=-1;
        invalid|=!Finite(point)||!Finite(normal);
        if(!touched&&noBounce){Vec3 velocity;projectile->GetVelocity(&velocity,nullptr);velocity*=.1f;projectile->SetVelocity(&velocity,nullptr);}
        touched=true;
    }
    void Friction(IPhysicsObject*,float,int,int,IPhysicsCollisionData*)override{}
    void StartTouch(IPhysicsObject*,IPhysicsObject*,IPhysicsCollisionData*)override{}
    void EndTouch(IPhysicsObject*,IPhysicsObject*,IPhysicsCollisionData*)override{}
    void FluidStartTouch(IPhysicsObject*,IPhysicsFluidController*)override{invalid=true;}
    void FluidEndTouch(IPhysicsObject*,IPhysicsFluidController*)override{}
    void PostSimulationFrame()override{}
};
// A fresh native environment starts with an inert collision listener, and
// CPhysicsEnvironment::Simulate calls PostSimulationFrame on it without a null
// check (vphysics.dll+0x15531..0x15538 in the current TF2 build). Handing the
// environment nullptr therefore crashes on its next Simulate. Always restore
// this process-lifetime inert handler instead.
struct InertCollisionEvent final:IPhysicsCollisionEvent {
    void PreCollision(vcollisionevent_t*)override{}
    void PostCollision(vcollisionevent_t*)override{}
    void Friction(IPhysicsObject*,float,int,int,IPhysicsCollisionData*)override{}
    void StartTouch(IPhysicsObject*,IPhysicsObject*,IPhysicsCollisionData*)override{}
    void EndTouch(IPhysicsObject*,IPhysicsObject*,IPhysicsCollisionData*)override{}
    void FluidStartTouch(IPhysicsObject*,IPhysicsFluidController*)override{}
    void FluidEndTouch(IPhysicsObject*,IPhysicsFluidController*)override{}
    void PostSimulationFrame()override{}
};
inline IPhysicsCollisionEvent* InertHandler(){static InertCollisionEvent handler;return &handler;}
struct Scene {
    IPhysicsEnvironment* owned=nullptr;std::array<IPhysicsObject*,128> clones{};size_t count=0;
    struct Gap{Vec3 mins,maxs;};std::array<Gap,64> gaps{};size_t gapCount=0;
    Recorder recorder;bool ready=false;float maxSpeed=0,gravityMagnitude=0;
    Scene()=default;Scene(const Scene&)=delete;Scene& operator=(const Scene&)=delete;
    void Clear(){
        if(owned){owned->SetCollisionEventHandler(InertHandler());for(size_t i=0;i<count;++i)owned->DestroyObject(clones[i]);}
        owned=nullptr;count=0;ready=false;recorder.projectile=nullptr;
    }
    ~Scene(){Clear();}
    bool CrossesGap(const Vec3& a,const Vec3& b)const{
        for(size_t i=0;i<gapCount;++i)if(KnockbackPolicy::Entry({a.x,a.y,a.z},{b.x,b.y,b.z},{gaps[i].mins.x,gaps[i].mins.y,gaps[i].mins.z},{gaps[i].maxs.x,gaps[i].maxs.y,gaps[i].maxs.z})>=0)return true;
        return false;
    }
    bool Build(CProjectileSimulation& sim,const model_t* model,const Vec3& origin,const Vec3& angles,const Vec3& velocity,const Vec3& spin){
        if(owned||ready)return false;
        struct Rollback{Scene& scene;~Rollback(){if(!scene.ready)scene.Clear();}} rollback{*this};
        if(!I::Physics||!I::PhysicsCollision||!I::MoveHelper||!sim.m_pEnv||!model)return false;
        auto* surfaces=I::MoveHelper->GetSurfaceProps();if(!surfaces)return false;
        auto* world=I::ModelInfoClient->GetVCollide(1);auto* shape=I::ModelInfoClient->GetVCollide(model);
        if(!world||!world->solidCount||!world->solids||!world->solids[0]||!shape||!shape->solidCount||!shape->solids||!shape->pKeyValues)return false;
        // Establish provenance by finding the real map collision shape in an
        // existing environment. Never assume diagnostic env0 is the game env.
        auto* worldEntity=I::ClientEntityList->GetClientEntity(0);if(!worldEntity)return false;
        IPhysicsEnvironment* source=nullptr;const IPhysicsObject** objects=nullptr;int objectCount=0;unsigned scanned=0;
        for(int e=0;e<8&&!source;++e){auto* env=I::Physics->GetActiveEnvironmentByIndex(e);if(!env)break;if(env==sim.m_pEnv||env->IsInSimulation())continue;
            int n=0;auto list=env->GetObjectList(&n);if(!list||n<1||n>2048)continue;
            for(int i=0;i<n;++i){if(++scanned>2048)return false;if((scanned%32)==0&&!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Prediction))return false;
                if(list[i]&&list[i]->IsStatic()&&list[i]->GetCollide()==world->solids[0]
                    &&list[i]->GetGameData()==worldEntity){source=env;objects=list;objectCount=n;break;}}
        }
        if(!source)return false;
        solid_t solid{};solid.params=NativeParseReview::Defaults;NativeParseReview::SolidDefaults defaults;
        if(strnlen_s(shape->pKeyValues,65536)>=65536)return false;
        auto* parser=I::PhysicsCollision->VPhysicsKeyParserCreate(shape->pKeyValues);if(!parser)return false;
        struct FreeParser{IVPhysicsKeyParser* p;~FreeParser(){I::PhysicsCollision->VPhysicsKeyParserDestroy(p);}} parserOwner{parser};
        bool parsed=false;
        for(unsigned block=0;block<128&&!parser->Finished();++block){
            if(!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Prediction))return false;
            const auto* name=parser->GetCurrentBlockName();if(name&&!_stricmp(name,"solid")){parser->ParseSolid(&solid,&defaults);parsed=true;break;}parser->SkipBlock();
        }
        if(!parsed||solid.index<0||solid.index>=shape->solidCount||!shape->solids[solid.index]
            ||(solid.params.massCenterOverride&&solid.params.massCenterOverride!=&solid.massCenterOverride)
            ||!std::isfinite(solid.params.mass)||solid.params.mass<=0)return false;
        // Virtual meshes have a valid CPhysCollide but no compact surface.
        // Native CollideGetAABB dereferences that absent surface. The moving
        // projectile replacement must be compact-backed; static virtual world
        // meshes below can be cloned without making the unsupported AABB query.
        if(!shape->solids[solid.index]->GetCompactSurface())return false;
        const int material=solid.surfaceprop[0]?surfaces->GetSurfaceIndex(solid.surfaceprop):-1;if(material<0)return false;
        owned=sim.m_pEnv;const float reach=velocity.Length()*.9f+500.f;
        for(int i=0;i<objectCount;++i){
            if((i%32)==0&&!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Prediction))return false;
            const auto* object=objects[i];if(!object)continue;
            if(object->IsFluid())return false; // no silent dry-world treatment
            if(!object->IsCollisionEnabled()||object->IsTrigger())continue;
            if(!object->IsStatic()||object->IsFluid()){
                // Native player contact is evaluated separately; its shadow
                // object must not be treated as a static world obstruction.
                bool player=false;
                if(const auto* data=object->GetGameData())for(int client=1;client<=I::GlobalVars->maxClients;++client){
                    auto* candidate=I::ClientEntityList->GetClientEntity(client);
                    if(candidate&&data==candidate){player=true;break;}
                }
                if(player)continue;
                return false; // a current AABB cannot bound future dynamic motion
            }
            auto* collide=object->GetCollide();if(!collide)return false;
            Vec3 position,rotation,mins,maxs;object->GetPosition(&position,&rotation);if(!Finite(position)||!Finite(rotation))return false;
            if(collide->GetCompactSurface()){
                I::PhysicsCollision->CollideGetAABB(&mins,&maxs,collide,position,rotation);if(!Finite(mins)||!Finite(maxs))return false;
                if(KnockbackPolicy::HullDistance({origin.x,origin.y,origin.z},{0,0,0},{mins.x,mins.y,mins.z},{maxs.x,maxs.y,maxs.z})>reach)continue;
            }
            // Noncompact static meshes have no supported AABB here. Retain
            // their real collision geometry conservatively, without inventing
            // a bound or omitting a wall. Count/work limits still apply.
            if(count==clones.size()||!ProjectilePerformancePolicy::SearchStep(ProjectilePerformancePolicy::SearchWork::Geometry))return false;
            auto params=NativeParseReview::Defaults;params.pGameData=nullptr;params.pName="incoming-static";
            auto* clone=owned->CreatePolyObjectStatic(collide,object->GetMaterialIndex(),position,rotation,&params);if(!clone)return false;
            clone->SetGameIndex(object->GetGameData()==worldEntity?1:2);
            clone->SetContents(object->GetContents());clones[count++]=clone;
        }
        Vec3 gravity;source->GetGravity(&gravity);const float density=source->GetAirDensity();if(!Finite(gravity)||!std::isfinite(density)||density<0)return false;
        physics_performanceparams_t limits{};source->GetPerformanceSettings(&limits);
        if(!std::isfinite(limits.maxVelocity)||limits.maxVelocity<=0||limits.maxVelocity>10000)return false;
        maxSpeed=limits.maxVelocity;gravityMagnitude=gravity.Length();
        solid.params.pGameData=nullptr;solid.params.pName="incoming-projectile";solid.params.enableCollisions=true;
        auto* body=owned->CreatePolyObject(shape->solids[solid.index],material,origin,angles,&solid.params);if(!body)return false;
        owned->SetPerformanceSettings(&limits);
        owned->DestroyObject(sim.m_pObj);sim.m_pObj=body;sim.m_bPhysics=true;
        body->SetCallbackFlags(body->GetCallbackFlags()|0x0001|0x0020|0x0100);
        body->SetContents(defaults.contents);body->SetVelocity(&velocity,&spin);body->Wake();
        owned->SetGravity(gravity);owned->SetAirDensity(density);owned->SetSimulationTimestep(TICK_INTERVAL);owned->ResetSimulationClock();
        recorder.projectile=body;owned->SetCollisionEventHandler(&recorder);ready=true;return true;
    }
};
}
