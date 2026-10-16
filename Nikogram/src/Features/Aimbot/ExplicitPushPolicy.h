#pragma once
#include "KnockbackPolicy.h"
namespace ExplicitPushPolicy {
using V=KnockbackPolicy::V;
inline V Cannon(V attacker,V victim){
    auto direction=KnockbackPolicy::Sub(victim,attacker);const auto length=KnockbackPolicy::Length(direction);
    if(length>0)direction=KnockbackPolicy::Mul(direction,400.f/length);
    direction[2]+=350.f;return direction;
}
inline V Scorch(V velocity,bool burning){
    const auto length=KnockbackPolicy::Length(velocity);auto direction=length>0?KnockbackPolicy::Mul(velocity,1.f/length):V{};
    direction[2]=1.f;return KnockbackPolicy::Mul(direction,burning?400.f:100.f);
}
inline V Generic(V impulse,bool ground,float vulnerability,float vertical){
    impulse=KnockbackPolicy::Mul(impulse,vulnerability);
    if(ground)impulse[2]=std::max(impulse[2],268.3281573f);
    impulse[2]*=vertical;return impulse;
}
// Native TF_STUN_MOVEMENT input scale, including the .2s fade after expiry.
// This applies to an isolated movement hypothesis, not the received player state.
inline float InputScale(float time,float expires){
    if(expires<=0)return 1.f;if(time<=expires)return 0.f;
    return std::clamp((time-expires)/.2f,0.f,1.f);
}
}
