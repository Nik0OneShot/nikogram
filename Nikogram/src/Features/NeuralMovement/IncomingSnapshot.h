#pragma once
#include "../../NeuralCore/incoming_cues.h"

namespace NeuralIncoming {
struct Snapshot {
 std::array<movement::incoming::Projectile,64> projectiles{};size_t count=0;
 uint64_t receipt=0;bool available=false,complete=false,overflow=false;
 static movement::incoming::V V(const Vec3&v){return {v.x,v.y,v.z};}
 Snapshot(){
  if(!I::ClientEntityList||!I::EngineClient->IsInGame()||!I::EngineClient->GetNetChannelInfo())return;
  static uint64_t serial=0;receipt=++serial;available=true;
  const int first=std::max(1,I::EngineClient->GetMaxClients()+1),last=I::ClientEntityList->GetHighestEntityIndex();
  static int cursor=first;if(cursor<first||cursor>last)cursor=first;
  const int total=std::max(0,last-first+1);int scanned=0;const auto start=std::chrono::steady_clock::now();
  // H::Entities is cleared at this callback. Inspect received entity interfaces
  // once per batch, before resolver/interpolation/movement mutation.
  for(;scanned<total&&scanned<2048;++scanned){
   if(scanned&&scanned%32==0&&std::chrono::steady_clock::now()-start>std::chrono::microseconds(150))break;
   if(cursor>last)cursor=first;auto* entity=reinterpret_cast<CBaseEntity*>(I::ClientEntityList->GetClientEntity(cursor++));
   if(!entity)continue;const auto cls=entity->GetClassID();
   const bool pipe=cls==ETFClassID::CTFGrenadePipebombProjectile;
   const bool rocket=cls==ETFClassID::CTFProjectile_Rocket;
   const bool curved=cls==ETFClassID::CTFProjectile_Arrow||cls==ETFClassID::CTFProjectile_Flare;
   if(!pipe&&!rocket&&!curved)continue;
   if(entity->IsDormant()){overflow=true;continue;}
   if(count==projectiles.size()){overflow=true;continue;}
   auto&r=projectiles[count++];r.handle=entity->GetRefEHandle().ToInt();r.time=entity->m_flSimulationTime();r.receipt=receipt;r.team=entity->m_iTeamNum();
   CBaseEntity*owner=nullptr;
   if(pipe){auto*p=entity->As<CTFGrenadePipebombProjectile>();r.position=V(p->m_vecOrigin());r.velocity=V(p->m_vecVelocity());r.velocityKnown=true;
    const int type=p->m_iType();r.type=type==TF_GL_MODE_REGULAR?2:(type==TF_GL_MODE_REMOTE_DETONATE||type==TF_GL_MODE_REMOTE_DETONATE_PRACTICE)?3:4;
    r.deflectionKnown=true;r.deflected=p->m_iDeflected()!=0;owner=r.deflected?p->m_hDeflectOwner().Get():p->m_hThrower().Get();
    r.radius=p->m_DmgRadius();r.radiusKnown=std::isfinite(r.radius)&&r.radius>0&&r.radius<=512;
   }else{auto*p=entity->As<CTFBaseRocket>();r.position=V(p->m_vecOrigin());r.deflectionKnown=true;r.deflected=p->m_iDeflected()!=0;
    r.type=rocket?1:4;r.velocityKnown=rocket&&!r.deflected;if(r.velocityKnown)r.velocity=V(p->m_vInitialVelocity());if(!r.deflected)owner=p->m_hOwnerEntity().Get();
   }
   if(owner){r.owner=owner->GetRefEHandle().ToInt();r.ownerKnown=true;}
  }
  complete=scanned==total&&!overflow;overflow|=scanned<total;
 }
 movement::incoming::Block For(CTFPlayer*p)const{
  movement::incoming::Target t{uint64_t(p->GetRefEHandle().ToInt()),receipt,p->m_flSimulationTime(),V(p->m_vecOrigin()),V(p->m_vecVelocity()),p->m_iTeamNum()};
  auto b=movement::incoming::Build(t,std::span(projectiles.data(),count),available,complete,overflow);
  b[4]=1;b[5]=p->InCond(TF_COND_IMMUNE_TO_PUSHBACK);b[6]=p->InCond(TF_COND_MEGAHEAL);b[7]=p->InCond(TF_COND_AIMING);b[8]=p->InCond(TF_COND_DISGUISED);b[9]=p->IsInvulnerable();b[10]=p->InCond(TF_COND_SHIELD_CHARGE);b[11]=(p->m_vecMaxs().z-p->m_vecMins().z)/128.f;
  return movement::incoming::Valid(b)?b:movement::incoming::Block{};
 }
};
}
