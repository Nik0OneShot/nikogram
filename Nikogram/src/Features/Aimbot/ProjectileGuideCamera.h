#pragma once
#include "ProjectileGuidePolicy.h"

// Main view data only, captured after OverrideView's visual adjustments. No
// entity pointers are retained and CameraWindow's secondary view cannot write it.
namespace ProjectileGuideCamera
{
    struct Snapshot
    {
        Vec3 eyeOffset,angleOffset;
        unsigned owner=0;
        int spawn=-1,server=-1;
        float realtime=-1;
    };
    inline Snapshot state;
    inline void Capture(CTFPlayer* local,const CViewSetup* view)
    {
        state={};
        if(!local||!view||!local->IsAlive()||!I::EngineClient->IsInGame()||G::Unload)return;
        const Vec3 eye=local->GetEyePosition(),angles=I::EngineClient->GetViewAngles();
        const Vec3 offset=view->origin-eye,delta=view->angles-angles;
        if(!std::isfinite(offset.x)||!std::isfinite(offset.y)||!std::isfinite(offset.z)
            || !std::isfinite(delta.x)||!std::isfinite(delta.y))return;
        state.eyeOffset=offset;state.angleOffset=delta;
        state.owner=local->GetRefEHandle().ToInt();state.spawn=local->m_iSpawnCounter();
        state.server=I::ClientState->m_nServerCount;state.realtime=I::GlobalVars->realtime;
    }
    inline void Ray(CTFPlayer* local,Vec3& eye,Vec3& angles)
    {
        eye=local->GetEyePosition();angles=I::EngineClient->GetViewAngles();
        if(state.owner!=unsigned(local->GetRefEHandle().ToInt())||state.spawn!=local->m_iSpawnCounter()
            ||state.server!=I::ClientState->m_nServerCount
            ||!ProjectileGuidePolicy::FreshTime(I::GlobalVars->realtime,state.realtime))return;
        eye+=state.eyeOffset;angles+=state.angleOffset;
    }
}
