#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

// Schema3 contract incoming-v1. Raw received geometry + kinematic estimates only.
// Never pass a simulated target position, predicted impulse or later outcome here.
namespace movement::incoming {
constexpr int Count=64, Slots=2, SlotWidth=24, SlotStart=12;
constexpr float MaxAge=.1f, Horizon=1.f;
using V=std::array<float,3>;using Block=std::array<float,Count>;
struct Projectile {
 uint64_t handle=0,owner=0,receipt=0;double time=0;V position{},velocity{};
 int type=0,team=0;bool velocityKnown=false,ownerKnown=false,deflectionKnown=false,deflected=false;
 float radius=0;bool radiusKnown=false;
};
struct Target {uint64_t handle=0,receipt=0;double time=0;V position{},velocity{};int team=0;};
inline bool Finite(V v){for(float x:v)if(!std::isfinite(x))return false;return true;}
inline V Sub(V a,V b){for(int i=0;i<3;++i)a[i]-=b[i];return a;}
inline float Dot(V a,V b){float v=0;for(int i=0;i<3;++i)v+=a[i]*b[i];return v;}
inline bool Binary(float x){return x==0.f||x==1.f;}
inline bool Valid(const Block& b){
 for(float x:b)if(!std::isfinite(x)||std::abs(x)>6)return false;
 for(int i:{0,1,2,4,5,6,7,8,9,10})if(!Binary(b[i]))return false;
 if(b[3]<0||b[3]>1||std::floor(b[3]*32)!=b[3]*32||b[11]<0||b[11]>6)return false;
 if(!b[0]){for(int i=1;i<4;++i)if(b[i])return false;for(int i=12;i<60;++i)if(b[i])return false;}
 if(!b[4])for(int i=5;i<12;++i)if(b[i])return false;
 if(b[1]&&b[2])return false;
 for(int slot=0;slot<Slots;++slot){int k=SlotStart+slot*SlotWidth;
  for(int j:{0,9,10,11,12,13,14,15,19,21,22})if(!Binary(b[k+j]))return false;
  if(!b[k]){for(int j=1;j<SlotWidth;++j)if(b[k+j])return false;continue;}
  if(b[k+7]<0||b[k+7]>1||b[k+8]<0||b[k+8]>1||b[k+16]<0||b[k+16]>1||b[k+17]<0||b[k+17]>6||b[k+18]<0||b[k+18]>1||b[k+20]<0||b[k+20]>1)return false;
  if(!b[k+9]&&b[k+8])return false;if(b[k+9]&&(b[k+8]<.25f||std::floor(b[k+8]*4)!=b[k+8]*4))return false;
  if(!b[k+10]&&(b[k+11]||b[k+12]||b[k+13]))return false;
  if(b[k+11]+b[k+12]+b[k+13]!=b[k+10])return false;
  if(!b[k+14]&&(b[k+4]||b[k+5]||b[k+6]))return false;
  if(!b[k+15]&&(b[k+16]||b[k+17]||b[k+18]))return false;
  if(b[k+15]&&!b[k+14])return false;
  if(!b[k+19]&&b[k+20])return false;if(!b[k+21]&&b[k+22])return false;
  if(b[k+23])return false;
 }
 for(int i=60;i<64;++i)if(b[i])return false;
 if(b[36]&&!b[12])return false;
 if(std::min(b[3]*32,float(Slots))!=b[12]+b[36]||(b[3]*32>Slots&&!b[2]))return false;
 return true;
}
inline Block Build(const Target&t,std::span<const Projectile> input,bool available,bool complete,bool overflow){
 Block b{};if(!available||!t.handle||!t.receipt||!std::isfinite(t.time)||!Finite(t.position)||!Finite(t.velocity))return b;
 b[0]=1;b[1]=complete&&!overflow;b[2]=overflow;
 struct Choice{Projectile p;V relative,velocity;float age,eta=0,closestDistance=0,confidence=0;bool estimate=false;};
 std::array<Choice,Slots> selected{};size_t selectedCount=0,eligible=0;
 auto less=[](const Choice&a,const Choice&c){float ar=a.estimate?a.closestDistance:std::sqrt(Dot(a.relative,a.relative)),cr=c.estimate?c.closestDistance:std::sqrt(Dot(c.relative,c.relative));return ar!=cr?ar<cr:a.p.handle<c.p.handle;};
 for(const auto&p:input){
  const double age=t.time-p.time;
  if(!p.handle||!p.receipt||p.receipt>t.receipt||!std::isfinite(p.time)||!std::isfinite(age)||age<0||age>MaxAge||!Finite(p.position)) {b[1]=0;continue;}
  auto relative=Sub(p.position,t.position);if(std::sqrt(Dot(relative,relative))>8192)continue;
  Choice q{p,relative,{},float(age)};q.p.velocityKnown=p.velocityKnown&&Finite(p.velocity)&&std::sqrt(Dot(p.velocity,p.velocity))<=12000;
  if(q.p.velocityKnown){q.velocity=Sub(p.velocity,t.velocity);if(std::sqrt(Dot(q.velocity,q.velocity))>24000){q.p.velocityKnown=false;q.velocity={};}}
  if(q.p.velocityKnown){auto aligned=relative;for(int i=0;i<3;++i)aligned[i]+=p.velocity[i]*float(age);float speed2=Dot(q.velocity,q.velocity);q.eta=speed2>1?std::clamp(-Dot(aligned,q.velocity)/speed2,0.f,Horizon):0.f;auto closest=aligned;for(int i=0;i<3;++i)closest[i]+=q.velocity[i]*q.eta;q.closestDistance=std::sqrt(Dot(closest,closest));q.confidence=std::clamp(1.f-float(age)/MaxAge,0.f,1.f);q.estimate=true;}
  ++eligible;size_t at=0;while(at<selectedCount&&!less(q,selected[at]))++at;if(at>=Slots)continue;
  if(selectedCount<Slots)++selectedCount;for(size_t j=selectedCount-1;j>at;--j)selected[j]=selected[j-1];selected[at]=q;
 }
 b[3]=float(std::min(eligible,size_t(32)))/32.f;if(eligible>Slots)b[2]=1,b[1]=0;
 for(size_t slot=0;slot<selectedCount;++slot){const auto&q=selected[slot];const auto&p=q.p;int k=SlotStart+int(slot)*SlotWidth;b[k]=1;
  for(int i=0;i<3;++i){b[k+1+i]=q.relative[i]/4096.f;b[k+4+i]=q.velocity[i]/4000.f;}
  b[k+7]=q.age/MaxAge;b[k+9]=p.type>=1&&p.type<=4;b[k+8]=b[k+9]?float(p.type)/4.f:0.f;
  b[k+10]=p.ownerKnown&&p.owner&&p.team>=2&&p.team<=3&&t.team>=2&&t.team<=3;
  if(b[k+10]){b[k+11]=p.owner==t.handle;b[k+12]=p.owner!=t.handle&&p.team==t.team;b[k+13]=p.owner!=t.handle&&p.team!=t.team;}
  b[k+14]=p.velocityKnown;b[k+15]=q.estimate;b[k+16]=q.eta/Horizon;b[k+17]=q.closestDistance/4096.f;b[k+18]=q.confidence;
  b[k+19]=p.radiusKnown&&std::isfinite(p.radius)&&p.radius>=0&&p.radius<=512;b[k+20]=b[k+19]?p.radius/512.f:0.f;
  b[k+21]=p.deflectionKnown;b[k+22]=p.deflectionKnown&&p.deflected;
 }
 return Valid(b)?b:Block{};
}
}
