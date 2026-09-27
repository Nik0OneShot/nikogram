#pragma once
#include <cstdint>
#include <string>
class IGameEvent;
namespace PrivateLearning::ProjectileReplay {struct Input;}
namespace PrivateLearning::CollisionParity {struct Result;}
namespace PrivateLearning::FullPathParity {struct Result;}
namespace PrivateLearning
{
#ifdef NIKOGRAM_PRIVATE_LEARNING
    void Pulse();
    void Observe();
    void Aim(int entity, bool firingIntent);
    bool WantsAimObservation();
    void Event(IGameEvent* event);
    void Draw();
    void Select(uint32_t account,const std::string& name);
    void Shutdown();
    bool EngineBegin(const void* key,int entity) noexcept;
    void EngineTick(const void* key,float sim,float x,float y,float nx,float ny) noexcept;
    void EngineEnd(const void* key) noexcept;
    void EngineSolution(const void* key,int result,float total,float latency,int weapon) noexcept;
    bool EngineIntercept(const void* key,float sim,float networkSim,bool ground,bool supported,float flight,float latency,float& dx,float& dy) noexcept;
    void EngineGeometry(const void* key,const char* reason,double micros) noexcept;
    void EnginePathAudit(const void* key,size_t generated,size_t tested,bool splash,float minSim,float maxSim) noexcept;
    bool EngineReplayWanted(const void* key) noexcept;
    void EngineSelectedPath(const void* key,const ProjectileReplay::Input& input) noexcept;
    bool EngineCollisionReserve(const void* key) noexcept;
    void EngineCollisionResult(const void* key,const CollisionParity::Result& result) noexcept;
    void EngineFullPathResult(const void* key,const FullPathParity::Result& result) noexcept;
#else
    inline void Pulse() {}
    inline void Observe() {}
    inline void Aim(int, bool) {}
    inline bool WantsAimObservation() { return false; }
    inline void Event(IGameEvent*) {}
    inline void Draw() {}
    inline void Select(uint32_t,const std::string&) {}
    inline void Shutdown() {}
    inline bool EngineBegin(const void*,int) noexcept {return false;}
    inline void EngineTick(const void*,float,float,float,float,float) noexcept {}
    inline void EngineEnd(const void*) noexcept {}
    inline void EngineSolution(const void*,int,float,float,int) noexcept {}
#endif
    struct EngineScope
    {
        const void* key;bool active;
        EngineScope(const void* storage,int entity) noexcept:key(storage),active(EngineBegin(storage,entity)){}
        ~EngineScope(){if(active)EngineEnd(key);}
        EngineScope(const EngineScope&)=delete;EngineScope& operator=(const EngineScope&)=delete;
    };
}
