#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
// Cosmetic angular presentation only. Never read by a target or firing solver.
namespace ProjectileDisplayPolicy {
struct Key {
    uint32_t target=0,weapon=0;int server=0,spawn=0;float fov=0,charge=0;uint32_t owner=0;int targetSpawn=0,camera=0;
    bool operator==(const Key&)const=default;
};
struct State {
    Key key{};bool valid=false;int frame=-1;float time=0,pitch=0,yaw=0;
    void Reset(){*this={};}
    bool Update(const Key& next,int renderFrame,float now,float rawPitch,float rawYaw,bool available,bool shot=false) {
        if(!available||!std::isfinite(now)||!std::isfinite(rawPitch)||!std::isfinite(rawYaw)) {Reset();return false;}
        const bool same=valid&&key==next;
        const float dt=now-time,dy=std::remainder(rawYaw-yaw,360.f),dp=rawPitch-pitch;
        // A new solution/zoom/charge/identity, large angular jump or actual shot
        // is exact immediately. Repeated hooks in one frame do not advance twice.
        if(!same||shot||dt<0||dt>.05f||std::hypot(dp,dy)>8.f) {pitch=rawPitch;yaw=rawYaw;}
        else if(frame==renderFrame)return true;
        else if(dt>0) {
            const float alpha=-std::expm1(-dt/.018f);
            pitch+=dp*alpha;yaw=std::remainder(yaw+dy*alpha,360.f);
        }
        key=next;valid=true;frame=renderFrame;time=now;return true;
    }
};
}
