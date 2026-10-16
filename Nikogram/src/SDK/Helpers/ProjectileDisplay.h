#pragma once
#include "../SDK.h"
#include "ProjectileDisplayPolicy.h"
namespace ProjectileDisplay {
inline int serial=0;
inline AimPoint_t committed;
inline Vec3 cameras[2];
inline bool cameraValid[2]{};
inline ProjectileDisplayPolicy::State states[2];
inline float Charge(CTFWeaponBase* weapon) {
    if(!weapon)return 0;
    switch(weapon->GetWeaponID()) {
    case TF_WEAPON_COMPOUND_BOW:return weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime();
    case TF_WEAPON_PIPEBOMBLAUNCHER:return weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime();
    case TF_WEAPON_CANNON:return weapon->As<CTFGrenadeLauncher>()->m_flDetonateTime();
    default:return 0;
    }
}
inline void BeginCommand(){serial=serial==2147483647?1:serial+1;committed={};}
inline AimPoint_t Make(const Vec3& point,CBaseEntity* target,CTFPlayer* local,CTFWeaponBase* weapon,int duration,bool shot=false,const Vec3& launch={}) {
    AimPoint_t result{point,I::GlobalVars->tickcount,duration};
    if(!target||!local||!weapon)return result;
    result.m_bProjectileDisplay=true;result.m_bDisplayShot=shot;result.m_vDisplayLaunchAngle=launch;
    result.m_iDisplayTargetSpawn=target->IsPlayer()?target->As<CTFPlayer>()->m_iSpawnCounter():0;
    result.m_nDisplayTarget=target->As<IHandleEntity>()->GetRefEHandle().ToInt();
    result.m_nDisplayWeapon=weapon->GetRefEHandle().ToInt();result.m_iDisplayServer=I::ClientState->m_nServerCount;
    result.m_iDisplaySpawn=local->m_iSpawnCounter();result.m_iDisplaySerial=serial;result.m_flDisplayCharge=Charge(weapon);return result;
}
// Called only after the outgoing command is sealed. This reads the command and
// writes presentation snapshots; no aim, button, timing or candidate changes.
inline void Commit(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* command,const Vec3& spreadCorrection) {
    auto& point=G::AimPoint;
    if(!local||!weapon||!command||G::PrimaryWeaponType!=EWeaponType::PROJECTILE
        ||!point.m_bProjectileDisplay||point.m_iDisplaySerial!=serial)return;
    const bool shot=G::Attacking==1&&!command->weaponselect;
    point.m_bDisplayShot=false;
    if(!shot)return;
    const auto launch=command->viewangles-spreadCorrection;
    Vec3 actual,expected;Math::AngleVectors(launch,&actual);Math::AngleVectors(point.m_vDisplayLaunchAngle,&expected);
    const Vec3 cross={actual.y*expected.z-actual.z*expected.y,actual.z*expected.x-actual.x*expected.z,actual.x*expected.y-actual.y*expected.x};
    const float dot=actual.x*expected.x+actual.y*expected.y+actual.z*expected.z;
    const float difference=std::atan2(cross.Length(),dot)*180.f/3.14159265358979323846f;
    if(!std::isfinite(difference)||difference>.001f) {
        // Later command edits changed the shot. An old solved impact/path is no
        // longer truthful; hide this prospective display, never delay the shot.
        point.m_iDisplaySerial=-1;
        G::CooldownAimPoint={};G::CooldownBoxStorage.clear();
        for(auto& path:G::PathStorage)if(path.m_iProjectileDisplaySerial==serial){path.m_iProjectileDisplaySerial=-1;path.m_bProjectileDisplayShot=false;}
        for(auto& box:G::BoxStorage)if(box.m_iProjectileDisplaySerial==serial){box.m_iProjectileDisplaySerial=-1;box.m_bProjectileDisplayShot=false;}
        return;
    }
    point.m_bDisplayShot=true;point.m_vDisplayLaunchAngle=Math::VectorAngles(actual);
    committed=point;G::CooldownAimPoint={};G::CooldownBoxStorage.clear();
    for(auto& path:G::PathStorage)if(path.m_iProjectileDisplaySerial==serial)path.m_bProjectileDisplayShot=true;
    for(auto& box:G::BoxStorage)if(box.m_iProjectileDisplaySerial==serial)box.m_bProjectileDisplayShot=true;
}
inline bool Resolve(bool cooldown,bool enabled,int channel,const Vec3& eye,Vec3& angles,float* distance=nullptr) {
    auto& state=states[channel];
    auto* local=H::Entities.GetLocal();auto* weapon=H::Entities.GetWeapon();
    const auto* raw=committed.m_bDisplayShot&&committed.m_iDisplaySerial==serial?&committed:G::VisualAimPoint(cooldown,enabled,I::GlobalVars->tickcount);
    if(G::PrimaryWeaponType!=EWeaponType::PROJECTILE) {
        state.Reset();if(!raw||!local||!local->IsAlive())return false;angles=Math::CalcAngle(eye,raw->m_vOrigin);if(distance)*distance=raw->m_vOrigin.DistTo(eye);return true;
    }
    const bool fresh=raw&&raw->m_bProjectileDisplay&&raw->m_iDisplaySerial==serial
        && raw->m_iTickCount==I::GlobalVars->tickcount;
    if(!enabled||!fresh||!local||!local->IsAlive()||!weapon
        || raw->m_nDisplayWeapon!=weapon->GetRefEHandle().ToInt()
        || raw->m_iDisplaySpawn!=local->m_iSpawnCounter()||raw->m_iDisplayServer!=I::ClientState->m_nServerCount
        || (!raw->m_bDisplayShot && raw->m_flDisplayCharge!=Charge(weapon))) {state.Reset();return false;}
    auto* client=I::ClientEntityList->GetClientEntityFromHandle(CBaseHandle(raw->m_nDisplayTarget));
    auto* target=reinterpret_cast<CBaseEntity*>(client);
    if(!target||target->IsDormant()||(target->IsPlayer()&&(!target->As<CTFPlayer>()->IsAlive()||target->As<CTFPlayer>()->m_iSpawnCounter()!=raw->m_iDisplayTargetSpawn))){state.Reset();return false;}
    const auto camera=I::EngineClient->GetViewAngles();
    if(cameraValid[channel] && std::hypot(camera.x-cameras[channel].x,std::remainder(camera.y-cameras[channel].y,360.f))>30.f)state.Reset();
    cameras[channel]=camera;cameraValid[channel]=true;
    const Vec3 exact=channel==1?raw->m_vDisplayLaunchAngle:Math::CalcAngle(eye,raw->m_vOrigin);
    if(distance)*distance=raw->m_vOrigin.DistTo(eye);
    const ProjectileDisplayPolicy::Key key{raw->m_nDisplayTarget,raw->m_nDisplayWeapon,raw->m_iDisplayServer,raw->m_iDisplaySpawn,G::FOV,raw->m_flDisplayCharge,uint32_t(local->GetRefEHandle().ToInt()),target->IsPlayer()?target->As<CTFPlayer>()->m_iSpawnCounter():0,int(I::Input->CAM_IsThirdPerson())};
    if(!state.Update(key,I::GlobalVars->framecount,I::GlobalVars->realtime,exact.x,exact.y,true,raw->m_bDisplayShot))return false;
    angles={state.pitch,state.yaw,0};return true;
}
}
