#pragma once
#include "KnockbackPolicy.h"
#include <optional>
namespace ReceivedFlyMotion {
// Two actual network snapshots estimate current ballistic velocity. First
// receipt is not launch time. Deflection/owner/launcher/level changes reset it.
struct Identity {unsigned handle=0,owner=0,launcher=0;int deflections=0;bool operator==(const Identity&)const=default;};
struct Sample {Identity id;float time=0,gravity=0;Vec3 origin,velocity;bool present=false,estimated=false;};
inline std::optional<Vec3> Estimate(const Identity& id,float time,const Vec3& origin,float gravity){
    struct Cache {int server=-1;void* channel=nullptr;std::array<Sample,256> entries{};};static Cache cache;
    auto* channel=I::EngineClient->GetNetChannelInfo();
    if(cache.server!=I::ClientState->m_nServerCount||cache.channel!=channel){cache={};cache.server=I::ClientState->m_nServerCount;cache.channel=channel;}
    auto& old=cache.entries[id.handle%cache.entries.size()];
    const auto finite=[](const Vec3& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
    if(!std::isfinite(time)||!std::isfinite(gravity)||gravity<0||gravity>4000||!finite(origin))return {};
    if(old.present&&old.id==id&&old.time==time&&old.origin==origin&&old.gravity==gravity)
        return old.estimated?std::optional<Vec3>(old.velocity):std::nullopt;
    const float dt=time-old.time;std::optional<Vec3> result;
    if(old.present&&old.id==id&&old.gravity==gravity&&dt>0&&dt<=.1f){
        auto velocity=(origin-old.origin)/dt;velocity.z-=gravity*dt*.5f;
        if(finite(velocity)&&velocity.Length()<=10000){
            auto expected=old.velocity;expected.z-=gravity*dt;
            if(!old.estimated||(velocity-expected).Length()<=128.f+velocity.Length()*.05f)result=velocity;
        }
    }
    old={id,time,gravity,origin,result.value_or(Vec3{}),true,result.has_value()};return result;
}
}
