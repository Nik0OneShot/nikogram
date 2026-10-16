#pragma once
#include "KnockbackPolicy.h"
namespace PipeContactPolicy {
using V=KnockbackPolicy::V;
// Native CTFWeaponBaseGrenadeProj::VPhysicsUpdate's manual player-ray bounce.
// This is not the rigid-body world collision formula.
inline V PlayerBounce(V velocity,V normal,bool enemy){
    float dot=0;for(int i=0;i<3;++i)dot+=velocity[i]*normal[i];
    for(int i=0;i<3;++i)velocity[i]=(velocity[i]-2*dot*normal[i])*.45f*(enemy?.5f:1.f);
    return velocity;
}
inline V EntryNormal(V start,V end,V mins,V maxs,float entry){
    V normal{};float best=-1e30f;
    for(int i=0;i<3;++i){const float d=end[i]-start[i];if(std::abs(d)<1e-6f)continue;
        const float t=((d>0?mins[i]:maxs[i])-start[i])/d;
        if(t<=entry+1e-4f&&t>best){best=t;normal={};normal[i]=d>0?-1.f:1.f;}}
    return normal;
}
inline bool Inside(V position,V mins,V maxs){for(int i=0;i<3;++i)if(position[i]<mins[i]||position[i]>maxs[i])return false;return true;}
}
