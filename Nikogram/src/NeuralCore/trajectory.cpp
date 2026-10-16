#include "trajectory.h"
#include "storage.h"
#include "metadata.h"
#include "neural_input.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <thread>
#include <variant>

namespace movement::trajectory {
namespace {
using Clock=std::chrono::steady_clock;
template<class A>bool Finite(const A&a,float bound=1e7f){for(auto v:a)if(!std::isfinite(v)||std::abs(v)>bound)return false;return true;}
Vec Sub(Vec a,const Vec&b){for(int k=0;k<3;++k)a[k]-=b[k];return a;}
float Length(const Vec&a){return std::sqrt(std::inner_product(a.begin(),a.end(),a.begin(),0.f));}
uint64_t Stamp(){return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
constexpr size_t RetainedAnchors=8192,EventLimit=2048;
bool Valid(const Input&x){incoming::Block b{};std::copy(x.begin()+LegacyFeatures,x.end(),b.begin());return Finite(x,6)&&incoming::Valid(b);}
bool ValidLegacy(const Input&x){for(int k=0;k<LegacyFeatures;++k)if(!std::isfinite(x[k])||std::abs(x[k])>6)return false;return true;}
bool Valid(const Observation&o){return o.track&&o.session&&o.player<=UINT32_MAX&&std::isfinite(o.time)&&o.time>=0&&o.time<=1e9&&Finite(o.position)&&Finite(o.velocity,3000);}
void Replace(const std::filesystem::path&file,const std::string&text){metadata::Replace(file,text,16*1024*1024);}
float Distance(const Sequence&a,const Sequence&b){double sum=0;int n=0;for(int j=0;j<Knots;++j)if(a.horizon[j]>0&&b.horizon[j]>0){for(int k=0;k<3;++k){double d=a.displacement[j][k]/(600*a.horizon[j])-b.displacement[j][k]/(600*b.horizon[j]);sum+=d*d;}++n;}return n?float(sum/n):1e6f;}
unsigned Category(const Sequence&s,const std::array<Sequence,Branches>&centers){unsigned best=0;float d=Distance(s,centers[0]);for(unsigned b=1;b<Branches;++b){float q=Distance(s,centers[b]);if(q<d){d=q;best=b;}}return best;}
std::array<float,Branches> Probabilities(const std::array<float,Network::Outputs>&v){std::array<float,Branches>p{};float peak=-1e20f,sum=0;for(int b=0;b<Branches;++b)peak=std::max(peak,v[b*(Knots*3+1)]);for(int b=0;b<Branches;++b){p[b]=std::exp(std::clamp(v[b*(Knots*3+1)]-peak,-40.f,0.f));sum+=p[b];}for(auto&x:p)x/=sum;return p;}
bool ValidSequence(const Sequence&s){if(!s.anchor||!s.group||s.player>UINT32_MAX||!Valid(s.x))return false;bool any=false;for(int k=0;k<Knots;++k){if(!std::isfinite(s.horizon[k])||!Finite(s.displacement[k]))return false;if(s.horizon[k]==0){if(s.endGroup[k])return false;continue;}if(std::abs(s.horizon[k]-Horizons[k])>1e-5f||s.endGroup[k]<s.group||Length(s.displacement[k])>3000*s.horizon[k]+32)return false;any=true;}return any;}
bool ValidPolicy(const Policy&p){return int(p.selection)>=0&&int(p.selection)<=3&&p.imported<=UINT32_MAX;}
}
bool KnownWeaponContext(const WeaponContext&c)noexcept{
 return c.itemDef>=0&&c.itemDef<=65535&&std::isfinite(c.speed)&&c.speed>0&&c.speed<=100000
  &&std::isfinite(c.gravity)&&c.gravity>=0&&c.gravity<=100000&&std::isfinite(c.radius)&&c.radius>=0&&c.radius<=100000
  &&std::isfinite(c.charge)&&c.charge>=0&&c.charge<=120&&std::isfinite(c.damageScale)&&c.damageScale>=0&&c.damageScale<=1000&&c.state<=63;
}
namespace {
constexpr uint64_t OutcomeByteLimit=16*1024*1024;
ShotPolicy::ContextKey ContextKeyFor(unsigned weapon,const WeaponContext&c,unsigned range,bool air){
 // Invalid contexts never reach a numeric conversion. Unknown uses its own legacy map.
 if(!KnownWeaponContext(c))return {};
 return {weapon,c.itemDef,std::llround(double(c.speed)*100),std::llround(double(c.gravity)*10000),std::llround(double(c.radius)*100),std::llround(double(c.charge)*10000),std::llround(double(c.damageScale)*10000),c.state,range,air};
}
std::string OutcomeRow(const Outcome&o){std::ostringstream row;row.imbue(std::locale::classic());row<<std::setprecision(std::numeric_limits<float>::max_digits10)<<o.shot<<' '<<o.weapon<<' '<<o.rangeBand<<' '<<o.air<<' '<<int(o.chosen)<<' '<<o.known<<' '<<o.damage<<' '<<o.killed<<' '<<o.evidence<<' '<<o.context.itemDef<<' '<<o.context.speed<<' '<<o.context.gravity<<' '<<o.context.radius<<' '<<o.context.charge<<' '<<o.context.state<<' '<<o.context.damageScale<<'\n';return row.str();}
}
std::vector<Outcome> ReadOutcomes(const std::filesystem::path&file){
 std::istringstream f(metadata::Read(file,OutcomeByteLimit));f.imbue(std::locale::classic());std::string line;
 if(!std::getline(f,line))throw std::runtime_error("Missing outcome header");if(!line.empty()&&line.back()=='\r')line.pop_back();const bool v3=line=="MOVEMENT_CHOSEN_OUTCOMES 3";if(!v3&&line!="MOVEMENT_CHOSEN_OUTCOMES 2")throw std::runtime_error("Unsupported chosen-outcome schema");
 ShotPolicy validated;std::vector<Outcome> records;
 while(std::getline(f,line)){if(line.size()>512||records.size()>=65536)throw std::runtime_error("Outcome capacity exceeded");std::istringstream row(line);row.imbue(std::locale::classic());Outcome o;int air,chosen,known,killed;
  if(!(row>>o.shot>>o.weapon>>o.rangeBand>>air>>chosen>>known>>o.damage>>killed)||air<0||air>1||known<0||known>1||killed<0||killed>1)throw std::runtime_error("Invalid outcome row");
  if(v3){if(!(row>>o.evidence>>o.context.itemDef>>o.context.speed>>o.context.gravity>>o.context.radius>>o.context.charge>>o.context.state>>o.context.damageScale))throw std::runtime_error("Incomplete outcome weapon context");}
  else{row>>std::ws;if(!row.eof()&&!(row>>o.evidence))throw std::runtime_error("Invalid outcome evidence tier");}
  row>>std::ws;if(!row.eof())throw std::runtime_error("Unexpected outcome content");o.air=air!=0;o.chosen=Splash(chosen);o.known=known!=0;o.killed=killed!=0;if(!validated.Add(o))throw std::runtime_error("Invalid or duplicate shot outcome");records.push_back(o);
 }
 if(f.bad())throw std::runtime_error("Outcome read failed");return records;
}
void WriteOutcomes(const std::filesystem::path&file,std::span<const Outcome>records){
 ShotPolicy validated;std::string text="MOVEMENT_CHOSEN_OUTCOMES 3\n";for(const auto&o:records){if(!validated.Add(o))throw std::runtime_error("Invalid or duplicate shot outcome");text+=OutcomeRow(o);if(text.size()>OutcomeByteLimit)throw std::runtime_error("Outcome file exceeds limit");}Replace(file,text);
}
bool Features(const History&h,Input&x)noexcept{
 if(h.count<3||h.count>h.points.size())return false;const auto&last=h.points[h.count-1];if(!Valid(last.observation)||!Valid(last.context))return false;
 const auto&a=last.observation;x=last.context;
 for(size_t i=0;i<h.count;++i){const auto&o=h.points[i].observation;if(!Valid(o)||o.session!=a.session||o.track!=a.track||o.player!=a.player)return false;if(i){const auto&prior=h.points[i-1].observation;const double dt=o.time-prior.time;if(dt<.005||dt>.2||Length(Sub(o.position,prior.position))>dt*3000+32)return false;}}
 const double span=a.time-h.points[0].observation.time;if(span<.35)return false;
 std::array<Vec,4>v{};v[0]=a.velocity;constexpr double delays[]{.1,.25,.5};
 for(int j=1;j<4;++j){size_t best=0;double distance=1e9;for(size_t i=0;i<h.count;++i){double d=std::abs(a.time-h.points[i].observation.time-delays[j-1]);if(d<distance){distance=d;best=i;}}v[j]=h.points[best].observation.velocity;}
 for(int j=0;j<4;++j)for(int k=0;k<3;++k)x[j*3+k]=v[j][k]/600;
 for(int k=0;k<3;++k){x[12+k]=(v[0][k]-v[1][k])/600;x[15+k]=(v[0][k]-v[3][k])/600;}
 x[18]=float(std::min(span,1.));x[19]=a.ground?1.f:0.f;
 float peak=0,ground=0;for(size_t i=0;i<h.count;++i){peak=std::max(peak,Length(h.points[i].observation.velocity));ground+=h.points[i].observation.ground?1.f:0.f;}
 x[72]=peak/600;x[73]=ground/float(h.count);float denominator=Length(v[0])*Length(v[3]);x[74]=denominator>1?std::inner_product(v[0].begin(),v[0].end(),v[3].begin(),0.f)/denominator:0;
 return Valid(x);
}
Network::Network(unsigned seed){std::mt19937 rng(seed);std::uniform_real_distribution<float>d(-.07f,.07f);for(auto&v:w1)v=d(rng);for(auto&v:w2)v=d(rng);}
std::array<float,Network::Outputs> Network::Forward(const Input&x)const noexcept{
 std::array<float,HiddenCount>hidden{};for(int j=0;j<HiddenCount;++j){float v=b1[j];for(int k=0;k<activeFeatures;++k)v+=w1[j*FeaturesCount+k]*x[k];hidden[j]=std::tanh(v);}
 auto out=b2;for(int o=0;o<Outputs;++o)for(int j=0;j<HiddenCount;++j)out[o]+=w2[o*HiddenCount+j]*hidden[j];return out;
}
void Network::Learn(const Sequence&s,unsigned branch,float rate,bool sharedNear)noexcept{
 std::array<float,HiddenCount>h{},gradient{};for(int j=0;j<HiddenCount;++j){float v=b1[j];for(int k=0;k<FeaturesCount;++k)v+=w1[j*FeaturesCount+k]*s.x[k];h[j]=std::tanh(v);}
 auto out=b2;for(int o=0;o<Outputs;++o)for(int j=0;j<HiddenCount;++j)out[o]+=w2[o*HiddenCount+j]*h[j];
 auto probabilities=Probabilities(out);std::array<float,Outputs>err{};for(int b=0;b<Branches;++b)err[b*(Knots*3+1)]=(probabilities[b]-(unsigned(b)==branch?1.f:0.f))*.2f;
 int known=0;for(auto horizon:s.horizon)known+=horizon>0;
 // Near-term displacement is trained from the same observed future in every
 // branch; distant branches keep their distinct future-category targets.
 // Divide shared gradients so adding branches does not multiply loss weight.
 for(int t=0;t<Knots;++t)if(s.horizon[t]>0)for(int k=0;k<3;++k)
  for(unsigned b=0;b<Branches;++b)if(b==branch||(sharedNear&&t<2)){
   const int o=int(b)*(Knots*3+1)+1+t*3+k;
   err[o]=std::clamp(out[o]-s.displacement[t][k]/(600*s.horizon[t]),-2.f,2.f)
    /float(std::max(known,1))/float(sharedNear&&t<2?Branches:1);
  }
 for(int j=0;j<HiddenCount;++j){for(int o=0;o<Outputs;++o)gradient[j]+=err[o]*w2[o*HiddenCount+j];gradient[j]*=1-h[j]*h[j];}
 for(int o=0;o<Outputs;++o){for(int j=0;j<HiddenCount;++j)w2[o*HiddenCount+j]-=rate*(err[o]*h[j]+.00001f*w2[o*HiddenCount+j]);b2[o]-=rate*err[o];}
 for(int j=0;j<HiddenCount;++j){for(int k=0;k<FeaturesCount;++k)w1[j*FeaturesCount+k]-=rate*(gradient[j]*s.x[k]+.00001f*w1[j*FeaturesCount+k]);b1[j]-=rate*gradient[j];}
}
namespace {
// Indexes of the schema-4 frame input (see ToFrame).
constexpr int FrameKnown=20,ClassStart=21,ClassKnown=30,MaxSpeed=50,MaxSpeedKnown=51,ShooterStart=52,ShooterKnown=58,CueStart=59;
std::array<float,Branches> FrameProbabilities(const std::array<float,FrameNetwork::Outputs>&v){std::array<float,Branches>p{};float peak=-1e20f,sum=0;for(int b=0;b<Branches;++b)peak=std::max(peak,v[b*(FrameKnots*3+1)]);for(int b=0;b<Branches;++b){p[b]=std::exp(std::clamp(v[b*(FrameKnots*3+1)]-peak,-40.f,0.f));sum+=p[b];}for(auto&x:p)x/=sum;return p;}
bool Binary(float v){return v==0.f||v==1.f;}
}
bool ValidExtra(const Extra&e)noexcept{
 for(float v:e.Values())if(!std::isfinite(v))return false;
 if(!Binary(e.recorded)||!Binary(e.maxSpeedKnown)||!Binary(e.shooterKnown)||!Binary(e.hostile)||!Binary(e.yawKnown))return false;
 if(!e.recorded)return e.Values()==Extra{}.Values();
 if(e.maxSpeed<0||e.maxSpeed>3000||(!e.maxSpeedKnown&&e.maxSpeed))return false;
 if(!e.shooterKnown&&(e.dx||e.dy||e.dz||e.hostile))return false;
 if(std::abs(e.dx)>40000||std::abs(e.dy)>40000||std::abs(e.dz)>40000)return false;
 if(!e.yawKnown&&e.yaw)return false;if(std::abs(e.yaw)>720)return false;
 return true;
}
Frame FrameOf(const Input&x,const Extra&e)noexcept{
 Frame f;for(int j=0;j<4;++j){const float vx=x[j*3]*600,vy=x[j*3+1]*600,speed=std::hypot(vx,vy);if(std::isfinite(speed)&&speed>=20){f.c=vx/speed;f.s=vy/speed;f.known=true;return f;}}
 if(e.shooterKnown){const float d=std::hypot(e.dx,e.dy);if(d>1){f.c=e.dx/d;f.s=e.dy/d;f.known=true;}}
 return f;
}
Vec ToLocal(const Vec&v,const Frame&f,bool mirror)noexcept{const float l=-v[0]*f.s+v[1]*f.c;return {v[0]*f.c+v[1]*f.s,mirror?-l:l,v[2]};}
Vec ToWorld(const Vec&v,const Frame&f)noexcept{return {v[0]*f.c-v[1]*f.s,v[0]*f.s+v[1]*f.c,v[2]};}
FrameInput ToFrame(const Input&x,const Extra&e,const Frame&f,bool mirror)noexcept{
 FrameInput y{};auto put=[&](int to,int from){auto v=ToLocal({x[from],x[from+1],x[from+2]},f,mirror);y[to]=v[0];y[to+1]=v[1];y[to+2]=v[2];};
 for(int j=0;j<6;++j)put(j*3,j*3); // 4 velocities + 2 velocity changes, /600 as in v3
 y[18]=x[18];y[19]=x[19];y[FrameKnown]=f.known?1.f:0.f;
 for(int k=0;k<9;++k)y[ClassStart+k]=x[20+k];y[ClassKnown]=x[63];
 for(int k=0;k<8;++k){y[31+k]=x[45+k];y[39+k]=x[53+k];} // conditions, terrain probes (direction-free)
 y[47]=x[72];y[48]=x[73];y[49]=x[74];
 if(e.recorded&&e.maxSpeedKnown){y[MaxSpeed]=std::min(e.maxSpeed/600.f,5.f);y[MaxSpeedKnown]=1;}
 if(e.recorded&&e.shooterKnown){const float d=std::hypot(e.dx,e.dy);const auto dir=ToLocal({e.dx,e.dy,0},f,mirror);if(d>1){y[ShooterStart]=dir[0]/d;y[ShooterStart+1]=dir[1]/d;}
  y[ShooterStart+2]=std::clamp(e.dz/1000.f,-6.f,6.f);y[ShooterStart+3]=std::min(std::hypot(d,e.dz)/3000.f,6.f);
  if(e.yawKnown&&d>1){const float toShooter=std::atan2(e.dy,e.dx),yaw=e.yaw*3.14159265f/180.f;y[ShooterStart+4]=std::cos(yaw-toShooter);}
  y[ShooterStart+5]=e.hostile;y[ShooterKnown]=1;}
 // Incoming cues: copied, with each slot's relative position/velocity rotated into the frame.
 for(int k=0;k<incoming::Count;++k)y[CueStart+k]=x[LegacyFeatures+k];
 for(int slot=0;slot<incoming::Slots;++slot){const int k=CueStart+incoming::SlotStart+slot*incoming::SlotWidth;for(int o:{1,4}){auto v=ToLocal({y[k+o],y[k+o+1],y[k+o+2]},f,mirror);y[k+o]=v[0];y[k+o+1]=v[1];y[k+o+2]=v[2];}}
 return y;
}
FrameNetwork::FrameNetwork(unsigned seed){std::mt19937 rng(seed);std::uniform_real_distribution<float>d(-.07f,.07f);for(auto&v:w1)v=d(rng);for(auto&v:w2)v=d(rng);}
std::array<float,FrameNetwork::Outputs> FrameNetwork::Forward(const FrameInput&x)const noexcept{
 std::array<float,HiddenCount>hidden{};for(int j=0;j<HiddenCount;++j){float v=b1[j];for(int k=0;k<FrameFeatures;++k)v+=w1[j*FrameFeatures+k]*x[k];hidden[j]=std::tanh(v);}
 auto out=b2;for(int o=0;o<Outputs;++o)for(int j=0;j<HiddenCount;++j)out[o]+=w2[o*HiddenCount+j]*hidden[j];return out;
}
void FrameNetwork::Learn(const FrameInput&x,const std::array<Vec,FrameKnots>&y,const std::array<float,FrameKnots>&horizon,unsigned branch,float rate,bool sharedNear)noexcept{
 constexpr int Stride=FrameKnots*3+1;
 std::array<float,HiddenCount>h{},gradient{};for(int j=0;j<HiddenCount;++j){float v=b1[j];for(int k=0;k<FrameFeatures;++k)v+=w1[j*FrameFeatures+k]*x[k];h[j]=std::tanh(v);}
 auto out=b2;for(int o=0;o<Outputs;++o)for(int j=0;j<HiddenCount;++j)out[o]+=w2[o*HiddenCount+j]*h[j];
 auto probabilities=FrameProbabilities(out);std::array<float,Outputs>err{};for(int b=0;b<Branches;++b)err[b*Stride]=(probabilities[b]-(unsigned(b)==branch?1.f:0.f))*.2f;
 int known=0;for(auto value:horizon)known+=value>0;
 for(int t=0;t<FrameKnots;++t)if(horizon[t]>0)for(int k=0;k<3;++k)
  for(unsigned b=0;b<Branches;++b)if(b==branch||(sharedNear&&t<2)){
   const int o=int(b)*Stride+1+t*3+k;
   err[o]=std::clamp(out[o]-y[t][k]/(600*horizon[t]),-2.f,2.f)/float(std::max(known,1))/float(sharedNear&&t<2?Branches:1);
  }
 for(int j=0;j<HiddenCount;++j){for(int o=0;o<Outputs;++o)gradient[j]+=err[o]*w2[o*HiddenCount+j];gradient[j]*=1-h[j]*h[j];}
 for(int o=0;o<Outputs;++o){for(int j=0;j<HiddenCount;++j)w2[o*HiddenCount+j]-=rate*(err[o]*h[j]+.00001f*w2[o*HiddenCount+j]);b2[o]-=rate*err[o];}
 for(int j=0;j<HiddenCount;++j){for(int k=0;k<FrameFeatures;++k)w1[j*FrameFeatures+k]-=rate*(gradient[j]*x[k]+.00001f*w1[j*FrameFeatures+k]);b1[j]-=rate*gradient[j];}
}
namespace {
// Groups the model never saw as known are presented as unknown instead of refusing:
// a model trained on converted v3 rows still runs once new inputs start arriving.
void MaskUnseen(const Model&m,FrameInput&y){
 if(m.high[MaxSpeedKnown]<.5f){y[MaxSpeed]=0;y[MaxSpeedKnown]=0;}
 if(m.high[ShooterKnown]<.5f)for(int k=ShooterStart;k<=ShooterKnown;++k)y[k]=0;
 if(m.high[ClassKnown]<.5f)for(int k=ClassStart;k<=ClassKnown;++k)y[k]=0;
 if(m.high[CueStart]<.5f)for(int k=CueStart;k<FrameFeatures;++k)y[k]=0;
}
Distribution PredictFrame(const Model&m,const Input&x,const Extra&e)noexcept{
 Distribution d;if(!m.trained||!m.revision||!Valid(x)||!ValidExtra(e))return d;
 const auto frame=FrameOf(x,e);auto y=ToFrame(x,e,frame,false);MaskUnseen(m,y);
 for(int k=0;k<FrameFeatures;++k)if(!std::isfinite(y[k])||y[k]<m.low[k]-.15f||y[k]>m.high[k]+.15f){d.reason="motion outside sequence training coverage";return d;}
 const auto output=m.frame.Forward(y);if(!Finite(output,100)){d.reason="invalid sequence model output";return d;}const auto probabilities=FrameProbabilities(output);
 constexpr int Stride=FrameKnots*3+1;bool supported=false;
 for(int b=0;b<Branches;++b){auto&branch=d.branches[b];branch.probability=probabilities[b];
  for(int t=0;t<Knots;++t){branch.radius[t]=-1;branch.supported[t]=false;}
  for(int t=0;t<FrameKnots;++t){branch.radius[t]=m.radius[b][t];Vec local{};for(int k=0;k<3;++k)local[k]=output[b*Stride+1+t*3+k]*600*Horizons[t];branch.displacement[t]=ToWorld(local,frame);
   branch.supported[t]=m.counts[b][t]>=8&&m.radius[b][t]>=0&&Finite(branch.displacement[t])&&Length(branch.displacement[t])<=3000*Horizons[t]+32;supported|=branch.supported[t];}}
 d.valid=supported;d.revision=m.revision;d.reason=supported?"trained frame path distribution (unvalidated)":"insufficient held-out sequence coverage";return d;
}
}
Distribution Predict(const Model&m,const Input&x,const Extra&e)noexcept{
 if(m.schema==FrameSchema)return PredictFrame(m,x,e);
 Distribution d;if(!m.trained||!m.revision||(m.schema!=2&&m.schema!=Schema)||m.network.activeFeatures!=(m.schema==2?LegacyFeatures:FeaturesCount)||(m.schema==2?!ValidLegacy(x):!Valid(x)))return d;
 // Exact mask/categorical context can be new; continuous motion still needs observed coverage.
 for(int k=0;k<m.network.activeFeatures;++k)if(x[k]<m.low[k]-.15f||x[k]>m.high[k]+.15f){d.reason="motion outside sequence training coverage";return d;}
 const auto output=m.network.Forward(x);if(!Finite(output,100)){d.reason="invalid sequence model output";return d;}const auto probabilities=Probabilities(output);
 bool supported=false;for(int b=0;b<Branches;++b){auto&branch=d.branches[b];branch.probability=probabilities[b];for(int t=0;t<Knots;++t){branch.radius[t]=m.radius[b][t];branch.supported[t]=m.counts[b][t]>=8&&m.radius[b][t]>=0;for(int k=0;k<3;++k)branch.displacement[t][k]=output[b*(Knots*3+1)+1+t*3+k]*600*Horizons[t];branch.supported[t]&=Finite(branch.displacement[t])&&Length(branch.displacement[t])<=3000*Horizons[t]+32;supported|=branch.supported[t];}}
 d.valid=supported;d.revision=m.revision;d.reason=supported?"trained joint path distribution (unvalidated)":"insufficient held-out sequence coverage";return d;
}
bool Interpolate(const Branch&b,float h,Vec&out,float&radius)noexcept{
 if(!std::isfinite(h)||h<=0)return false;for(int t=0;t<Knots;++t)if(h<=Horizons[t]){if(!b.supported[t]||(t&&!b.supported[t-1]))return false;float prior=t?Horizons[t-1]:0;float u=(h-prior)/(Horizons[t]-prior);for(int k=0;k<3;++k)out[k]=(t?b.displacement[t-1][k]:0)*(1-u)+b.displacement[t][k]*u;radius=t?std::max(b.radius[t-1],b.radius[t]):b.radius[t];return Finite(out)&&std::isfinite(radius);}return false;
}
float CoveredHorizon(const Branch&branch,float probability,float radius)noexcept{
 if(!std::isfinite(probability)||probability<0||probability>1||!std::isfinite(radius)||radius<0
    ||!std::isfinite(branch.probability)||branch.probability<probability)return 0;
 float horizon=0;for(int k=0;k<Knots;++k){if(!branch.supported[k]||!std::isfinite(branch.radius[k])||branch.radius[k]<0||branch.radius[k]>radius)break;horizon=Horizons[k];}return horizon;
}
int FlightTicks(float lifetime,float limit,float lead,float coverage,float tick)noexcept{
 if(!std::isfinite(lifetime)||lifetime<=0||!std::isfinite(limit)||limit<=0||!std::isfinite(lead)||lead<0
    ||!std::isfinite(coverage)||coverage<=lead||!std::isfinite(tick)||tick<=0)return 0;
 const double ticks=std::floor(double(std::min({lifetime,limit,coverage-lead}))/tick);
 return int(std::clamp(ticks,0.,16384.));
}
std::vector<float> Interceptions(const Branch&branch,Vec target,Vec shot,Vec offset,float speed,float lead,float limit){
 std::vector<float>roots;if(!Finite(target)||!Finite(shot)||!Finite(offset)||!std::isfinite(speed)||speed<=0||!std::isfinite(lead)||lead<0||!std::isfinite(limit)||limit<=0)return roots;
 for(int k=0;k<Knots;++k){if(!branch.supported[k]||(k&&!branch.supported[k-1]))continue;double lo=k?Horizons[k-1]:0,hi=Horizons[k];Vec velocity{},constant{};for(int a=0;a<3;++a){double prior=k?branch.displacement[k-1][a]:0;velocity[a]=float((branch.displacement[k][a]-prior)/(hi-lo));constant[a]=float(target[a]+prior+velocity[a]*(lead-lo)+offset[a]-shot[a]);}double aa=std::inner_product(velocity.begin(),velocity.end(),velocity.begin(),0.)-double(speed)*speed,bb=2*std::inner_product(constant.begin(),constant.end(),velocity.begin(),0.),cc=std::inner_product(constant.begin(),constant.end(),constant.begin(),0.);auto add=[&](double value){if(std::isfinite(value)&&value>0&&value<=limit&&value+lead>=lo-1e-5&&value+lead<=hi+1e-5)roots.push_back(float(value));};if(std::abs(aa)<1e-8){if(std::abs(bb)>1e-8)add(-cc/bb);}else{double disc=bb*bb-4*aa*cc;if(disc>=0){double q=-.5*(bb+std::copysign(std::sqrt(disc),bb));if(std::abs(q)>1e-12){add(q/aa);add(cc/q);}else add(-bb/(2*aa));}}}
 std::sort(roots.begin(),roots.end());roots.erase(std::unique(roots.begin(),roots.end(),[](float a,float b){return std::abs(a-b)<.0001f;}),roots.end());return roots;
}
std::vector<std::array<float,2>> ReachableIntervals(const Branch&branch,Vec target,Vec shot,Vec offset,float speed,float lead,float limit,float padding){
 std::vector<std::array<float,2>> result;
 if(!Finite(target)||!Finite(shot)||!Finite(offset)||!std::isfinite(speed)||speed<=0||!std::isfinite(lead)||lead<0||!std::isfinite(limit)||limit<=0||!std::isfinite(padding)||padding<0)return result;
 for(int k=0;k<Knots;++k){
  if(!branch.supported[k]||(k&&!branch.supported[k-1])||!Finite(branch.displacement[k]))continue;
  const double begin=k?Horizons[k-1]:0,end=Horizons[k],lo=std::max(0.,begin-lead),hi=std::min(double(limit),end-lead);
  if(hi<=lo)continue;std::array<double,3>v{},c{};double vv=0,cv=0,cc=0;
  for(int a=0;a<3;++a){double prior=k?branch.displacement[k-1][a]:0;v[a]=(branch.displacement[k][a]-prior)/(end-begin);c[a]=target[a]+prior+v[a]*(lead-begin)+offset[a]-shot[a];vv+=v[a]*v[a];cv+=c[a]*v[a];cc+=c[a]*c[a];}
  std::vector<double> cuts{lo,hi};auto add=[&](double t){if(std::isfinite(t)&&t>=lo&&t<=hi)cuts.push_back(t);};
  for(double radius:{double(padding),-double(padding)}){
   double aa=vv-double(speed)*speed,bb=2*(cv-speed*radius),ccc=cc-radius*radius;
   if(std::abs(aa)<1e-8){if(std::abs(bb)>1e-8)add(-ccc/bb);}
   else{double disc=bb*bb-4*aa*ccc;if(disc>=0){double q=-.5*(bb+std::copysign(std::sqrt(disc),bb));if(std::abs(q)>1e-12){add(q/aa);add(ccc/q);}else add(-bb/(2*aa));}}
  }
  std::sort(cuts.begin(),cuts.end());auto reachable=[&](double t){double d=0;for(int a=0;a<3;++a)d+=(c[a]+v[a]*t)*(c[a]+v[a]*t);return std::abs(std::sqrt(d)-speed*t)<=padding+1e-4;};
  for(size_t j=0;j<cuts.size();++j){if(reachable(cuts[j]))result.push_back({float(cuts[j]),float(cuts[j])});if(j&&reachable((cuts[j-1]+cuts[j])*.5))result.push_back({float(cuts[j-1]),float(cuts[j])});}
 }
 std::sort(result.begin(),result.end());std::vector<std::array<float,2>> merged;
 for(auto interval:result){if(!merged.empty()&&interval[0]<=merged.back()[1]+1e-5f)merged.back()[1]=std::max(merged.back()[1],interval[1]);else merged.push_back(interval);}return merged;
}
template<class Range> FitResult FitRange(const Range&all,uint64_t owner,const std::function<bool()>&cancel,size_t selection=0){
 FitResult r;r.model.owner=owner;r.model.low.fill(6);r.model.high.fill(-6);for(auto&rad:r.model.radius)rad.fill(-1);
 storage::Scratch scratch;storage::Sort<uint64_t> sort(scratch,cancel);uint64_t eligible=0;for(const auto&s:all)if((!owner||s.player==owner)&&ValidSequence(s)){sort.Add(s.group);++eligible;}storage::Groups groups(scratch,sort);
 if(groups.size()<6){r.evaluation="Need 6 independent sequence time groups";return r;}
 const auto validationRank=groups.size()*6/10,testRank=groups.size()*8/10;
 //64 observations at permitted0.2s gaps can span12.6s. Keep two full
 //8s groups between label endpoints and the next split's input histories.
 constexpr uint64_t embargoGroups=2;
 if(validationRank+embargoGroups>=testRank||testRank+embargoGroups>=groups.size()){
  r.evaluation="Need additional sequence time groups for non-overlapping input histories";return r;
 }
 uint64_t v=groups[validationRank],t=groups[testRank];
 const uint64_t validationBegin=groups[validationRank+embargoGroups],testBegin=groups[testRank+embargoGroups];uint64_t ordinal=0;const uint64_t stride=selection&&eligible>selection?1+(eligible-1)/selection:1;
 storage::Array<Sequence>train(scratch,cancel),val(scratch,cancel),test(scratch,cancel);
 for(auto s:all){if((owner&&s.player!=owner)||!ValidSequence(s))continue;if(ordinal++%stride)continue;if((s.group>=v&&s.group<validationBegin)||(s.group>=t&&s.group<testBegin))continue;bool known=false;for(int k=0;k<Knots;++k){if((s.group<v&&s.endGroup[k]>=v)||(s.group>=v&&s.group<t&&s.endGroup[k]>=t))s.horizon[k]=0;known|=s.horizon[k]>0;}if(!known||!Valid(s.x))continue;(s.group<v?train:s.group<t?val:test).push_back(s);}
 train.Seal();val.Seal();test.Seal();
 if(train.size()<512||val.size()<64||test.size()<64){r.evaluation="Need 512 training + 64 validation + 64 test sequences after time-boundary purge";return r;}
 auto&centers=r.model.centers;centers[0]=train[0];for(int b=1;b<Branches;++b){float farthest=-1;for(const auto&s:train){float nearest=1e9;for(int j=0;j<b;++j)nearest=std::min(nearest,Distance(s,centers[j]));if(nearest>farthest){farthest=nearest;centers[b]=s;}}}
 for(int iteration=0;iteration<5;++iteration){if(cancel&&cancel()){r.evaluation="Sequence fit cancelled";return r;}std::array<Sequence,Branches>sums{};std::array<std::array<uint64_t,Knots>,Branches>counts{};for(const auto&s:train){auto b=Category(s,centers);for(int k=0;k<Knots;++k)if(s.horizon[k]>0){++counts[b][k];for(int a=0;a<3;++a)sums[b].displacement[k][a]+=s.displacement[k][a]/s.horizon[k];}}for(int b=0;b<Branches;++b)for(int k=0;k<Knots;++k)if(counts[b][k]){centers[b].horizon[k]=Horizons[k];for(int a=0;a<3;++a)centers[b].displacement[k][a]=sums[b].displacement[k][a]/counts[b][k]*Horizons[k];}}
 for(const auto&s:train){auto b=Category(s,centers);for(int k=0;k<FeaturesCount;++k){r.model.low[k]=std::min(r.model.low[k],s.x[k]);r.model.high[k]=std::max(r.model.high[k],s.x[k]);}for(int k=0;k<Knots;++k)r.model.counts[b][k]+=s.horizon[k]>0;}
 // Train the established loss and the optional shared-near loss together.
 // Both see identical examples; no held-out test residual selects a candidate.
 std::array<Network,2>current,checkpoint;
 std::array<double,2>bestLoss{1e30,1e30};
 for(int epoch=0;epoch<24;++epoch){
  if(cancel&&cancel()){r.evaluation="Sequence fit cancelled";return r;}
  size_t i=0;for(const auto&sample:train){
   if(++i%64==0&&cancel&&cancel()){r.evaluation="Sequence fit cancelled";return r;}
   const auto branch=Category(sample,centers);const float rate=.006f/(1+epoch*.06f);
   for(int candidate=0;candidate<2;++candidate)current[candidate].Learn(sample,branch,rate,candidate==1);
  }
  std::array<double,2>loss{};
  for(const auto&sample:val){
   const auto branch=Category(sample,centers);
   for(int candidate=0;candidate<2;++candidate){
    const auto output=current[candidate].Forward(sample.x);const auto probability=Probabilities(output);
    loss[candidate]-=std::log(std::max(probability[branch],1e-6f))*.2;
    for(int k=0;k<Knots;++k)if(sample.horizon[k]>0)for(int axis=0;axis<3;++axis){
     double delta=output[branch*(Knots*3+1)+1+k*3+axis]-sample.displacement[k][axis]/(600*sample.horizon[k]);loss[candidate]+=delta*delta;
    }
   }
  }
  for(int candidate=0;candidate<2;++candidate)if(loss[candidate]<bestLoss[candidate]){bestLoss[candidate]=loss[candidate];checkpoint[candidate]=current[candidate];}
 }
 // Compare deployed highest-probability-branch geometry on validation only.
 // Require supported comparison horizons and no >5% short-knot RMSE regression.
 constexpr int comparisonKnots=5;
 std::array<std::array<double,comparisonKnots>,2>square{};
 std::array<uint64_t,comparisonKnots>comparisonCount{};
 for(const auto&sample:val){
  for(int k=0;k<comparisonKnots;++k)comparisonCount[k]+=sample.horizon[k]>0;
  for(int candidate=0;candidate<2;++candidate){
   const auto output=checkpoint[candidate].Forward(sample.x);const auto probability=Probabilities(output);
   const auto branch=std::max_element(probability.begin(),probability.end())-probability.begin();
   for(int k=0;k<comparisonKnots;++k)if(sample.horizon[k]>0)for(int axis=0;axis<3;++axis){
    double delta=output[branch*(Knots*3+1)+1+k*3+axis]*600*Horizons[k]-double(sample.displacement[k][axis]);square[candidate][k]+=delta*delta;
   }
  }
 }
 bool sharedNear=true;std::array<double,2>score{};
 for(int k=0;k<comparisonKnots;++k){
  if(comparisonCount[k]<64){sharedNear=false;continue;}
  const double scale=600.*Horizons[k];
  for(int candidate=0;candidate<2;++candidate)score[candidate]+=square[candidate][k]/comparisonCount[k]/(scale*scale)/comparisonKnots;
  if(k<2&&square[1][k]>square[0][k]*1.05*1.05)sharedNear=false;
 }
 sharedNear=sharedNear&&std::isfinite(bestLoss[1])&&bestLoss[1]<1e30
  &&Finite(checkpoint[1].w1,100)&&Finite(checkpoint[1].w2,100)&&Finite(checkpoint[1].b1,100)&&Finite(checkpoint[1].b2,100)
  &&std::isfinite(score[0])&&std::isfinite(score[1])&&score[1]<score[0];
 r.model.network=checkpoint[sharedNear?1:0];double best=bestLoss[sharedNear?1:0];
 if(!std::isfinite(best)||best>=1e30||!Finite(r.model.network.w1,100)||!Finite(r.model.network.w2,100)||!Finite(r.model.network.b1,100)||!Finite(r.model.network.b2,100)){r.evaluation="Sequence fit produced invalid weights";return r;}
 struct Residual{unsigned cell;float error;};struct ResidualLess{bool operator()(const Residual&a,const Residual&b)const{return a.cell!=b.cell?a.cell<b.cell:a.error<b.error;}};storage::Sort<Residual,ResidualLess>errors(scratch,cancel);std::array<std::array<uint64_t,Knots>,Branches>vc{},tc{};double brier=0,logloss=0;size_t n=0;
 for(int split=0;split<2;++split)for(const auto&s:(split?test:val)){auto b=Category(s,centers);auto out=r.model.network.Forward(s.x);auto probability=Probabilities(out);if(split){for(int j=0;j<Branches;++j){double e=probability[j]-(unsigned(j)==b?1.:0.);brier+=e*e;}logloss-=std::log(std::max(probability[b],1e-6f));++n;}for(int k=0;k<Knots;++k)if(s.horizon[k]>0){Vec error{};for(int a=0;a<3;++a)error[a]=out[b*(Knots*3+1)+1+k*3+a]*600*s.horizon[k]-s.displacement[k][a];if(!split)errors.Add({unsigned(b*Knots+k),Length(error)});++(split?tc:vc)[b][k];}}
 std::array<uint64_t,Branches*Knots>ranks{};errors.Each([&](const Residual&e){const auto b=e.cell/Knots,k=e.cell%Knots;const auto rank=++ranks[e.cell];if(vc[b][k]>=8&&tc[b][k]>=8&&r.model.counts[b][k]>=8&&rank==(vc[b][k]-vc[b][k]/20))r.model.radius[b][k]=e.error;});
 std::ostringstream report;report<<"training_loss="<<(sharedNear?"shared_near":"original")<<" validation_top1_score_original="<<score[0]<<" validation_top1_score_shared="<<score[1]<<" ";report<<"snapshot_eligible="<<eligible<<" selection="<<(selection?std::to_string(selection):"all")<<" stride="<<stride<<" ";report<<"Joint sequence candidate, unvalidated against engine baseline. train="<<train.size()<<" validation="<<val.size()<<" test="<<test.size()<<" test_Brier="<<brier/std::max(size_t(1),n)<<" test_logloss="<<logloss/std::max(size_t(1),n)<<'\n';
 for(int b=0;b<Branches;++b)for(int k=0;k<Knots;++k){report<<"branch="<<b<<" horizon="<<Horizons[k]<<" fit="<<r.model.counts[b][k]<<" validation="<<vc[b][k]<<" test="<<tc[b][k]<<" empirical_p95_radius="<<r.model.radius[b][k]<<'\n';}

 struct Metric{uint64_t n=0,covered=0;double square=0,velocitySquare=0,logloss=0;};std::map<std::string,Metric>metrics;
 for(const auto&s:test){const auto b=Category(s,centers);const auto out=r.model.network.Forward(s.x);const auto prob=Probabilities(out);std::vector<std::string>categories{"all",s.x[19]>.5f?"ground":"air",s.x[74]<-.25f?"reversal":"non-reversal",s.x[59]>.5f&&s.x[55]<.99f?"near-obstacle":"other-terrain"};categories.push_back(!s.x[80]?"cues-missing":s.x[92]?"cues-projectile":s.x[81]?"cues-observed-empty":"cues-partial-empty");for(int c=0;c<9;++c)if(s.x[20+c]>.5f)categories.push_back("class-"+std::to_string(c+1));
  for(int k=0;k<Knots;++k)if(s.horizon[k]>0&&r.model.radius[b][k]>=0){double square=0,baseline=0;for(int a=0;a<3;++a){double e=out[b*(Knots*3+1)+1+k*3+a]*600*Horizons[k]-s.displacement[k][a];square+=e*e;double velocityError=s.x[a]*600*Horizons[k]-s.displacement[k][a];baseline+=velocityError*velocityError;}
   for(const auto&name:categories){auto&m=metrics[name+" horizon="+std::to_string(Horizons[k])];++m.n;m.covered+=std::sqrt(square)<=r.model.radius[b][k];m.square+=square;m.velocitySquare+=baseline;m.logloss-=std::log(std::max(prob[b],1e-6f));}}
 }
 report<<"Calibration radii use validation only; test residuals never set weights/radii; test counts gate availability. Labels interpolate adjacent received snapshots (<=0.2s), not movement-simulator truth. Oracle category uses the actual future trajectory; its RMSE is not deployed-choice accuracy or hit rate.\n";
 for(const auto&[category,m]:metrics)report<<"category="<<category<<" n="<<m.n<<" oracle_category_RMSE="<<std::sqrt(m.square/m.n)<<" constant_velocity_RMSE="<<std::sqrt(m.velocitySquare/m.n)<<" test_p95_coverage="<<double(m.covered)/m.n<<" branch_logloss="<<m.logloss/m.n<<'\n';
 r.model.rows=train.size();r.model.revision=Stamp();r.model.trained=true;r.fitted=true;r.evaluation=report.str();return r;
}

namespace {
uint64_t Mix(uint64_t x){x+=0x9e3779b97f4a7c15ull;x=(x^(x>>30))*0xbf58476d1ce4e5b9ull;x=(x^(x>>27))*0x94d049bb133111ebull;return x^(x>>31);}
struct Local{std::array<Vec,FrameKnots>y{};std::array<float,FrameKnots>h{};};
Local LocalOf(const Sequence&s,bool mirror){Local l;const auto f=FrameOf(s.x,s.extra);for(int k=0;k<FrameKnots;++k){l.h[k]=s.horizon[k];if(s.horizon[k]>0)l.y[k]=ToLocal(s.displacement[k],f,mirror);}return l;}
float LocalDistance(const Local&a,const Local&b){double sum=0;int n=0;for(int j=0;j<FrameKnots;++j)if(a.h[j]>0&&b.h[j]>0){for(int k=0;k<3;++k){double d=a.y[j][k]/(600*a.h[j])-b.y[j][k]/(600*b.h[j]);sum+=d*d;}++n;}return n?float(sum/n):1e6f;}
unsigned LocalCategory(const Local&s,const std::array<Local,Branches>&c){unsigned best=0;float d=LocalDistance(s,c[0]);for(unsigned b=1;b<Branches;++b){float q=LocalDistance(s,c[b]);if(q<d){d=q;best=b;}}return best;}
bool KnownFrameLabel(const Sequence&s){for(int k=0;k<FrameKnots;++k)if(s.horizon[k]>0)return true;return false;}
}
template<class Range> FitResult FitFrameRange(const Range&all,uint64_t owner,const std::function<bool()>&cancel,size_t selection,const TrainingPolicy&policy){
 FitResult r;r.model.schema=FrameSchema;r.model.network.activeFeatures=FrameFeatures;r.model.owner=owner;r.model.low.fill(6);r.model.high.fill(-6);for(auto&rad:r.model.radius)rad.fill(-1);
 constexpr int Stride=FrameKnots*3+1;
 auto wanted=[&](const Sequence&s){return owner?s.player==owner:(policy.includeBots||s.player!=0);};
 auto usable=[&](const Sequence&s){return wanted(s)&&ValidSequence(s)&&ValidExtra(s.extra)&&KnownFrameLabel(s);};
 storage::Scratch scratch;storage::Sort<uint64_t> sort(scratch,cancel);uint64_t eligible=0,bots=0;std::map<uint64_t,uint64_t>perPlayer;
 for(const auto&s:all){if(!owner&&!s.player)++bots;if(usable(s)){sort.Add(s.group);++eligible;++perPlayer[s.player];}}storage::Groups groups(scratch,sort);
 if(groups.size()<6){r.evaluation="Need 6 independent sequence time groups (bots are excluded unless Include bots is on)";return r;}
 const auto validationRank=groups.size()*6/10,testRank=groups.size()*8/10;constexpr uint64_t embargoGroups=2;
 if(validationRank+embargoGroups>=testRank||testRank+embargoGroups>=groups.size()){r.evaluation="Need additional sequence time groups for non-overlapping input histories";return r;}
 const uint64_t v=groups[validationRank],t=groups[testRank],validationBegin=groups[validationRank+embargoGroups],testBegin=groups[testRank+embargoGroups];
 const uint64_t stride=selection&&eligible>selection?1+(eligible-1)/selection:1;
 // Balance (global model only): no single player supplies more than playerShare of the selection.
 const double cap=owner?1e300:std::max(256.,double(eligible)/double(stride)*std::max(policy.playerShare,2./double(std::max<size_t>(perPlayer.size(),1))));uint64_t capped=0;
 struct Keyed{uint64_t key;Sequence s;};struct KeyLess{bool operator()(const Keyed&a,const Keyed&b)const{return a.key<b.key;}};
 storage::Sort<Keyed,KeyLess>shuffled(scratch,cancel);storage::Array<Sequence>val(scratch,cancel),test(scratch,cancel);uint64_t trainN=0,ordinal=0;
 for(auto s:all){if(!usable(s))continue;if(ordinal++%stride)continue;
  const double share=double(perPlayer[s.player])/double(stride);if(share>cap&&double(Mix(s.anchor)%1000000)>=cap/share*1e6){++capped;continue;}
  if((s.group>=v&&s.group<validationBegin)||(s.group>=t&&s.group<testBegin))continue;
  for(int k=0;k<Knots;++k)if((s.group<v&&s.endGroup[k]>=v)||(s.group>=v&&s.group<t&&s.endGroup[k]>=t))s.horizon[k]=0;
  if(!KnownFrameLabel(s))continue;const auto f=FrameOf(s.x,s.extra);if(!Finite(ToFrame(s.x,s.extra,f,false),6))continue;
  if(s.group<v){shuffled.Add({Mix(s.anchor^0x7a11u),s});++trainN;}else if(s.group<t)val.push_back(s);else test.push_back(s);}
 val.Seal();test.Seal();
 if(trainN<512||val.size()<64||test.size()<64){r.evaluation="Need 512 training + 64 validation + 64 test sequences after time-boundary purge";return r;}
 // Shuffled once globally (chronological order never reaches SGD), then per-epoch windows.
 storage::Array<Sequence>train(scratch,cancel);shuffled.Each([&](const Keyed&k){train.push_back(k.s);});train.Seal();
 auto mirrorOf=[](const Sequence&s){return (Mix(s.anchor)&1)!=0;};
 std::array<Local,Branches>centers{};centers[0]=LocalOf(train[0],mirrorOf(train[0]));
 for(int b=1;b<Branches;++b){float farthest=-1;for(const auto&s:train){const auto l=LocalOf(s,mirrorOf(s));float nearest=1e9;for(int j=0;j<b;++j)nearest=std::min(nearest,LocalDistance(l,centers[j]));if(nearest>farthest){farthest=nearest;centers[b]=l;}}}
 for(int iteration=0;iteration<5;++iteration){if(cancel&&cancel()){r.evaluation="Sequence fit cancelled";return r;}std::array<Local,Branches>sums{};std::array<std::array<uint64_t,FrameKnots>,Branches>counts{};
  for(const auto&s:train){const auto l=LocalOf(s,mirrorOf(s));auto b=LocalCategory(l,centers);for(int k=0;k<FrameKnots;++k)if(l.h[k]>0){++counts[b][k];for(int a=0;a<3;++a)sums[b].y[k][a]+=l.y[k][a]/l.h[k];}}
  for(int b=0;b<Branches;++b)for(int k=0;k<FrameKnots;++k)if(counts[b][k]){centers[b].h[k]=Horizons[k];for(int a=0;a<3;++a)centers[b].y[k][a]=float(sums[b].y[k][a]/double(counts[b][k])*Horizons[k]);}}
 for(const auto&s:train){const auto f=FrameOf(s.x,s.extra);for(bool m:{false,true}){const auto y=ToFrame(s.x,s.extra,f,m);for(int k=0;k<FrameFeatures;++k){r.model.low[k]=std::min(r.model.low[k],y[k]);r.model.high[k]=std::max(r.model.high[k],y[k]);}}
  const auto l=LocalOf(s,mirrorOf(s));auto b=LocalCategory(l,centers);for(int k=0;k<FrameKnots;++k)r.model.counts[b][k]+=l.h[k]>0;}
 std::array<FrameNetwork,2>current,checkpoint;std::array<double,2>bestLoss{1e30,1e30};
 for(int epoch=0;epoch<24;++epoch){
  if(cancel&&cancel()){r.evaluation="Sequence fit cancelled";return r;}
  std::mt19937 rng(7321u+unsigned(epoch));std::bernoulli_distribution flip(.5);std::vector<Sequence>window;window.reserve(256);const float rate=.006f/(1+epoch*.06f);
  auto learn=[&](const Sequence&s){const bool m=flip(rng);const auto f=FrameOf(s.x,s.extra);const auto y=ToFrame(s.x,s.extra,f,m);const auto l=LocalOf(s,m);const auto b=LocalCategory(l,centers);for(int c=0;c<2;++c)current[c].Learn(y,l.y,l.h,b,rate,c==1);};
  auto drain=[&](size_t keep){while(window.size()>keep){std::uniform_int_distribution<size_t>pick(0,window.size()-1);auto i=pick(rng);std::swap(window[i],window.back());learn(window.back());window.pop_back();}};
  size_t i=0;for(const auto&sample:train){if(++i%64==0&&cancel&&cancel()){r.evaluation="Sequence fit cancelled";return r;}window.push_back(sample);if(window.size()==256)drain(128);}drain(0);
  std::array<double,2>loss{};
  for(const auto&sample:val){const auto f=FrameOf(sample.x,sample.extra);const auto y=ToFrame(sample.x,sample.extra,f,false);const auto l=LocalOf(sample,false);const auto branch=LocalCategory(l,centers);
   for(int c=0;c<2;++c){const auto output=current[c].Forward(y);const auto probability=FrameProbabilities(output);loss[c]-=std::log(std::max(probability[branch],1e-6f))*.2;
    for(int k=0;k<FrameKnots;++k)if(l.h[k]>0)for(int a=0;a<3;++a){double delta=output[branch*Stride+1+k*3+a]-l.y[k][a]/(600*l.h[k]);loss[c]+=delta*delta;}}}
  for(int c=0;c<2;++c)if(loss[c]<bestLoss[c]){bestLoss[c]=loss[c];checkpoint[c]=current[c];}
 }
 // Deployed highest-probability-branch geometry on validation decides the loss variant.
 std::array<std::array<double,FrameKnots>,2>square{};std::array<uint64_t,FrameKnots>comparisonCount{};
 for(const auto&sample:val){const auto f=FrameOf(sample.x,sample.extra);const auto y=ToFrame(sample.x,sample.extra,f,false);const auto l=LocalOf(sample,false);for(int k=0;k<FrameKnots;++k)comparisonCount[k]+=l.h[k]>0;
  for(int c=0;c<2;++c){const auto output=checkpoint[c].Forward(y);const auto probability=FrameProbabilities(output);const auto branch=std::max_element(probability.begin(),probability.end())-probability.begin();
   for(int k=0;k<FrameKnots;++k)if(l.h[k]>0)for(int a=0;a<3;++a){double delta=output[branch*Stride+1+k*3+a]*600*Horizons[k]-double(l.y[k][a]);square[c][k]+=delta*delta;}}}
 bool sharedNear=true;std::array<double,2>score{};
 for(int k=0;k<FrameKnots;++k){if(comparisonCount[k]<64){sharedNear=false;continue;}const double scale=600.*Horizons[k];for(int c=0;c<2;++c)score[c]+=square[c][k]/double(comparisonCount[k])/(scale*scale)/FrameKnots;if(k<2&&square[1][k]>square[0][k]*1.05*1.05)sharedNear=false;}
 sharedNear=sharedNear&&std::isfinite(bestLoss[1])&&bestLoss[1]<1e30&&Finite(checkpoint[1].w1,100)&&Finite(checkpoint[1].w2,100)&&Finite(checkpoint[1].b1,100)&&Finite(checkpoint[1].b2,100)&&std::isfinite(score[0])&&std::isfinite(score[1])&&score[1]<score[0];
 r.model.frame=checkpoint[sharedNear?1:0];const double best=bestLoss[sharedNear?1:0];
 if(!std::isfinite(best)||best>=1e30||!Finite(r.model.frame.w1,100)||!Finite(r.model.frame.w2,100)||!Finite(r.model.frame.b1,100)||!Finite(r.model.frame.b2,100)){r.evaluation="Sequence fit produced invalid weights";return r;}
 // Uncertainty radius = 95th-percentile error of using branch b whenever it is eligible at aim
 // time (probability >= 20%), whatever the true future was. Not the oracle category.
 constexpr float Eligible=.2f;
 struct Residual{unsigned cell;float error;};struct ResidualLess{bool operator()(const Residual&a,const Residual&b)const{return a.cell!=b.cell?a.cell<b.cell:a.error<b.error;}};storage::Sort<Residual,ResidualLess>errors(scratch,cancel);
 std::array<std::array<uint64_t,FrameKnots>,Branches>vc{},tc{};double brier=0,logloss=0;size_t n=0;
 for(int split=0;split<2;++split)for(const auto&sample:(split?test:val)){const auto f=FrameOf(sample.x,sample.extra);const auto y=ToFrame(sample.x,sample.extra,f,false);const auto l=LocalOf(sample,false);const auto truth=LocalCategory(l,centers);
  const auto out=r.model.frame.Forward(y);const auto probability=FrameProbabilities(out);
  if(split){for(int j=0;j<Branches;++j){double e=probability[j]-(unsigned(j)==truth?1.:0.);brier+=e*e;}logloss-=std::log(std::max(probability[truth],1e-6f));++n;}
  for(int b=0;b<Branches;++b)if(probability[b]>=Eligible)for(int k=0;k<FrameKnots;++k)if(l.h[k]>0){Vec error{};for(int a=0;a<3;++a)error[a]=out[b*Stride+1+k*3+a]*600*Horizons[k]-l.y[k][a];if(!split)errors.Add({unsigned(b*FrameKnots+k),Length(error)});++(split?tc:vc)[b][k];}}
 std::array<uint64_t,Branches*FrameKnots>ranks{};errors.Each([&](const Residual&e){const auto b=e.cell/FrameKnots,k=e.cell%FrameKnots;const auto rank=++ranks[e.cell];if(vc[b][k]>=8&&tc[b][k]>=8&&r.model.counts[b][k]>=8&&rank==(vc[b][k]-vc[b][k]/20))r.model.radius[b][k]=e.error;});
 std::ostringstream report;report.imbue(std::locale::classic());
 report<<"Frame sequence model (schema 4, movement-relative, mirrored training, 4 knots). training_loss="<<(sharedNear?"shared_near":"original")<<" eligible="<<eligible<<" selection="<<(selection?std::to_string(selection):"all")<<" stride="<<stride<<" balance_capped="<<capped<<" bots="<<(owner?"n/a":policy.includeBots?"included":(std::to_string(bots)+" excluded"))
  <<" train="<<train.size()<<" validation="<<val.size()<<" test="<<test.size()<<" test_Brier="<<brier/double(std::max(size_t(1),n))<<" test_logloss="<<logloss/double(std::max(size_t(1),n))<<'\n';
 // Test: deployed branch accuracy, honest coverage, constant velocity and movesim comparisons.
 struct Metric{uint64_t n=0,covered=0,eligible=0,paired=0;double square=0,velocity=0,model=0,movesim=0;};std::array<Metric,FrameKnots>m{};
 for(const auto&sample:test){const auto f=FrameOf(sample.x,sample.extra);const auto y=ToFrame(sample.x,sample.extra,f,false);const auto l=LocalOf(sample,false);const auto out=r.model.frame.Forward(y);const auto probability=FrameProbabilities(out);
  const auto top=int(std::max_element(probability.begin(),probability.end())-probability.begin());
  for(int k=0;k<FrameKnots;++k)if(l.h[k]>0){auto&q=m[k];double se=0,ve=0;for(int a=0;a<3;++a){double e=out[top*Stride+1+k*3+a]*600*Horizons[k]-l.y[k][a];se+=e*e;double c=y[a]*600*Horizons[k]-l.y[k][a];ve+=c*c;}++q.n;q.square+=se;q.velocity+=ve;
   for(int b=0;b<Branches;++b)if(probability[b]>=Eligible&&r.model.radius[b][k]>=0){++q.eligible;double e2=0;for(int a=0;a<3;++a){double e=out[b*Stride+1+k*3+a]*600*Horizons[k]-l.y[k][a];e2+=e*e;}q.covered+=std::sqrt(e2)<=r.model.radius[b][k];}
   if(sample.paired[k]){const auto ms=ToLocal(sample.movesim[k],f,false);double me=0;for(int a=0;a<3;++a){double e=ms[a]-l.y[k][a];me+=e*e;}++q.paired;q.model+=se;q.movesim+=me;}}}
 for(int k=0;k<FrameKnots;++k){const auto&q=m[k];report<<"horizon="<<Horizons[k]<<" n="<<q.n<<" deployed_RMSE="<<(q.n?std::sqrt(q.square/q.n):0)<<" constant_velocity_RMSE="<<(q.n?std::sqrt(q.velocity/q.n):0)
  <<" p95_coverage_when_eligible="<<(q.eligible?double(q.covered)/q.eligible:0)<<" movesim_pairs="<<q.paired;
  if(q.paired>=16){const double a=std::sqrt(q.model/q.paired),b=std::sqrt(q.movesim/q.paired);report<<" model_RMSE="<<a<<" movesim_RMSE="<<b<<" verdict="<<(a<=b*.97?"beats movesim":"does not beat movesim");}else report<<" verdict=not enough movesim pairs yet";report<<'\n';}
 for(int b=0;b<Branches;++b)for(int k=0;k<FrameKnots;++k)report<<"branch="<<b<<" horizon="<<Horizons[k]<<" fit="<<r.model.counts[b][k]<<" validation_eligible="<<vc[b][k]<<" test_eligible="<<tc[b][k]<<" p95_radius="<<r.model.radius[b][k]<<'\n';
 report<<"Radii: validation only, error of a branch whenever it is eligible (p>=20%), not the oracle category. Movesim pairs exist only for anchors the aimbot simulated while collecting. Labels interpolate adjacent received snapshots (<=0.2s).\n";
 r.model.rows=train.size();r.model.revision=Stamp();r.model.trained=true;r.fitted=true;r.evaluation=report.str();return r;
}
FitResult Fit(std::span<const Sequence>all,uint64_t owner,const std::function<bool()>&cancel){try{return FitFrameRange(all,owner,cancel,0,TrainingPolicy{});}catch(const std::exception&e){FitResult r;r.evaluation=e.what();return r;}}
namespace {
struct Record {Sequence s{};uint64_t ordinal=0;int knot=-1;int movesim=-1;};
constexpr const char* HeaderV2="MOVEMENT_SEQUENCE_DATA 2 80 4 10";
constexpr const char* HeaderV3="MOVEMENT_SEQUENCE_DATA 3 144 4 10 incoming-v1";
constexpr const char* HeaderV4="MOVEMENT_SEQUENCE_DATA 4 144 10 4 10 frame-v1";
int HeaderSchema(std::string h){if(!h.empty()&&h.back()=='\r')h.pop_back();return h==HeaderV2?2:h==HeaderV3?3:h==HeaderV4?FrameSchema:0;}
using StrafeSink=std::function<void(const strafe::Row&)>;
struct RecordLess{bool operator()(const Record&a,const Record&b)const{return a.s.anchor!=b.s.anchor?a.s.anchor<b.s.anchor:a.ordinal<b.ordinal;}};
template<class F>void Records(const std::filesystem::path&file,F fn,const storage::Cancel&cancel={},const StrafeSink&strafeRows={}){
 neuralio::Input f(file,archive::DefaultLimit,cancel);if(!f)storage::Fail(file,"Cannot open sequence data");f.imbue(std::locale::classic());std::array<char,8193>buffer{};
 if(f.peek()==std::char_traits<char>::eof())return; // empty corpus: no rows
 if(!f.getline(buffer.data(),64))storage::Fail(file,"Invalid sequence header");const int schema=HeaderSchema(buffer.data());if(!schema)storage::Fail(file,"Unsupported sequence header");
 uint64_t n=0;while(f.getline(buffer.data(),std::streamsize(buffer.size()))){if(!(n%256))storage::Check(cancel);Record r;r.s.schema=schema;r.ordinal=++n;std::istringstream row(buffer.data());row.imbue(std::locale::classic());char kind;if(!(row>>kind>>r.s.anchor)||!r.s.anchor)storage::Fail(file,"Invalid sequence record");
 if(kind=='A'){if(!(row>>r.s.player>>r.s.group)||r.s.player>UINT32_MAX||!r.s.group)storage::Fail(file,"Invalid sequence anchor");for(int k=0;k<(schema==2?LegacyFeatures:FeaturesCount);++k)if(!(row>>r.s.x[k]))storage::Fail(file,"Invalid sequence features");if(!Valid(r.s.x))storage::Fail(file,"Nonfinite/out-of-range sequence features");
  if(schema==FrameSchema){std::array<float,ExtraCount>e{};for(auto&v:e)if(!(row>>v))storage::Fail(file,"Invalid sequence extra context");r.s.extra=Extra::From(e);if(!ValidExtra(r.s.extra))storage::Fail(file,"Invalid sequence extra context");}}
 else if(kind=='B'&&schema==FrameSchema){int k;Vec v;if(!(row>>k>>v[0]>>v[1]>>v[2])||k<0||k>=FrameKnots||!Finite(v)||Length(v)>3000*Horizons[k]+32)storage::Fail(file,"Invalid movesim baseline record");r.movesim=k;r.s.movesim[k]=v;}
 else if(kind=='R'&&schema==FrameSchema){std::string rest;std::getline(row,rest);strafe::Row q;q.id=r.s.anchor;if(!strafe::Decode(std::to_string(r.s.anchor)+rest,q))storage::Fail(file,"Invalid strafe timing record");if(strafeRows)strafeRows(q);continue;}
 else if(kind=='L'){uint64_t end;int k;float h;Vec v;if(!(row>>end>>k>>h>>v[0]>>v[1]>>v[2])||k<0||k>=Knots||!end||!std::isfinite(h)||std::abs(h-Horizons[k])>1e-5f||!Finite(v)||Length(v)>3000*h+32)storage::Fail(file,"Invalid sequence label");r.knot=k;r.s.endGroup[k]=end;r.s.horizon[k]=h;r.s.displacement[k]=v;}
 else storage::Fail(file,"Unknown sequence record");row>>std::ws;if(!row.eof())storage::Fail(file,"Unexpected sequence record content");fn(r);}
 if(f.bad()||(!f.eof()&&f.fail()))storage::Fail(file,"Sequence read failed or record exceeds 8192 bytes");
}
storage::Array<Sequence> Joined(const std::filesystem::path&file,storage::Scratch&scratch,const storage::Cancel&cancel,const StrafeSink&strafeRows={}){
 storage::Sort<Record,RecordLess>sort(scratch,cancel);Records(file,[&](const Record&r){sort.Add(r);},cancel,strafeRows);storage::Array<Sequence>all(scratch,cancel);Sequence current{};bool anchor=false;
 auto flush=[&]{if(anchor)all.push_back(current);};
 sort.Each([&](const Record&r){if(r.s.anchor!=current.anchor){flush();current={};current.anchor=r.s.anchor;anchor=false;}if(r.movesim>=0){if(!anchor)storage::Fail(file,"Movesim baseline without its anchor");if(!current.paired[r.movesim]){current.paired[r.movesim]=1;current.movesim[r.movesim]=r.s.movesim[r.movesim];}return;}if(r.knot<0){if(anchor)storage::Fail(file,"Duplicate sequence anchor");current=r.s;anchor=true;}else{const int k=r.knot;if(!anchor||current.horizon[k]||r.s.endGroup[k]<current.group)storage::Fail(file,"Missing anchor, duplicate label or invalid label time group");current.horizon[k]=r.s.horizon[k];current.endGroup[k]=r.s.endGroup[k];current.displacement[k]=r.s.displacement[k];}});flush();all.Seal();return all;
}
}
void ValidateDataset(const std::filesystem::path&file,const std::function<bool()>&cancel){storage::Scratch scratch;auto all=Joined(file,scratch,cancel);}
FitResult FitDataset(const std::filesystem::path&file,uint64_t owner,size_t selection,const std::function<bool()>&cancel){try{storage::Scratch scratch;auto all=Joined(file,scratch,cancel);auto result=FitFrameRange(all,owner,cancel,selection,TrainingPolicy{});result.evaluation="snapshot_anchors="+std::to_string(all.size())+" "+result.evaluation;return result;}catch(const std::exception&e){FitResult r;r.evaluation=e.what();return r;}}

namespace {
constexpr const char* FrameModelHeader="MOVEMENT_SEQUENCE_MODEL 4 123 24 4 4 frame-v1";
void SaveFrameModel(const std::filesystem::path&path,const Model&m){
 if(m.schema!=FrameSchema||!m.trained||!m.revision||m.rows<512)throw std::runtime_error("Refusing untrained/incompatible frame sequence weights");
 std::ostringstream f;f.imbue(std::locale::classic());f<<std::setprecision(9)<<FrameModelHeader<<'\n'<<m.owner<<' '<<m.revision<<' '<<m.rows<<'\n';
 auto write=[&](const auto&a){for(auto v:a)f<<v<<' ';f<<'\n';};
 for(const auto* domain:{&m.low,&m.high}){for(int k=0;k<FrameFeatures;++k)f<<(*domain)[k]<<' ';f<<'\n';}
 for(auto&a:m.counts)write(a);for(auto&a:m.radius)write(a);write(m.frame.w1);write(m.frame.b1);write(m.frame.w2);write(m.frame.b2);metadata::Replace(path,f.str(),1024*1024);
}
Model LoadFrameModel(std::istringstream&f){
 Model m;m.schema=FrameSchema;m.network.activeFeatures=FrameFeatures;m.low.fill(0);m.high.fill(0);
 if(!(f>>m.owner>>m.revision>>m.rows)||m.owner>UINT32_MAX||!m.revision||m.rows<512)throw std::runtime_error("Invalid sequence model provenance");
 auto read=[&](auto&a){for(auto&v:a)if(!(f>>v))throw std::runtime_error("Truncated frame sequence model");};
 for(auto* domain:{&m.low,&m.high})for(int k=0;k<FrameFeatures;++k)if(!(f>>(*domain)[k]))throw std::runtime_error("Truncated frame sequence domain");
 for(auto&a:m.counts)read(a);for(auto&a:m.radius)read(a);read(m.frame.w1);read(m.frame.b1);read(m.frame.w2);read(m.frame.b2);f>>std::ws;
 if(!f.eof()||!Finite(m.low,6)||!Finite(m.high,6)||!Finite(m.frame.w1,100)||!Finite(m.frame.b1,100)||!Finite(m.frame.w2,100)||!Finite(m.frame.b2,100))throw std::runtime_error("Invalid frame sequence model weights");
 for(int k=0;k<FrameFeatures;++k)if(m.low[k]>m.high[k])throw std::runtime_error("Invalid frame sequence domain");
 for(int b=0;b<Branches;++b)for(int k=0;k<Knots;++k)if(m.counts[b][k]>m.rows||!std::isfinite(m.radius[b][k])||m.radius[b][k]<-1||m.radius[b][k]>1e7||(k>=FrameKnots&&m.radius[b][k]!=-1))throw std::runtime_error("Invalid frame sequence uncertainty");
 m.trained=true;return m;
}
}
void SaveModel(const std::filesystem::path&path,const Model&m){
 if(m.schema==FrameSchema){SaveFrameModel(path,m);return;}
 const int count=m.schema==2?LegacyFeatures:FeaturesCount;
 if((m.schema!=2&&m.schema!=Schema)||m.network.activeFeatures!=count||!m.trained||!m.revision||m.rows<512)throw std::runtime_error("Refusing untrained/incompatible sequence weights");
 std::ostringstream f;f.imbue(std::locale::classic());f<<std::setprecision(9)<<(m.schema==2?"MOVEMENT_SEQUENCE_MODEL 2 80 24 4 10\n":"MOVEMENT_SEQUENCE_MODEL 3 144 24 4 10 incoming-v1\n")<<m.owner<<' '<<m.revision<<' '<<m.rows<<'\n';
 auto write=[&](const auto&a){for(auto v:a)f<<v<<' ';f<<'\n';};
 for(const auto* domain:{&m.low,&m.high}){for(int k=0;k<count;++k)f<<(*domain)[k]<<' ';f<<'\n';}
 for(auto&a:m.counts)write(a);for(auto&a:m.radius)write(a);
 for(int j=0;j<HiddenCount;++j)for(int k=0;k<count;++k)f<<m.network.w1[j*FeaturesCount+k]<<' ';f<<'\n';
 write(m.network.b1);write(m.network.w2);write(m.network.b2);metadata::Replace(path,f.str(),1024*1024);
}
Model LoadModel(const std::filesystem::path&path){
 std::istringstream f(metadata::Read(path,1024*1024));f.imbue(std::locale::classic());std::string header;std::getline(f,header);if(!header.empty()&&header.back()=='\r')header.pop_back();
 if(header==FrameModelHeader)return LoadFrameModel(f);
 const int schema=header=="MOVEMENT_SEQUENCE_MODEL 2 80 24 4 10"?2:header=="MOVEMENT_SEQUENCE_MODEL 3 144 24 4 10 incoming-v1"?3:0;
 if(!schema)throw std::runtime_error("Unsupported sequence model schema");Model m;m.schema=schema;m.network.activeFeatures=schema==2?LegacyFeatures:FeaturesCount;m.network.w1.fill(0);
 if(!(f>>m.owner>>m.revision>>m.rows)||m.owner>UINT32_MAX||!m.revision||m.rows<512)throw std::runtime_error("Invalid sequence model provenance");
 auto read=[&](auto&a){for(auto&v:a)if(!(f>>v))throw std::runtime_error("Truncated sequence model");};
 for(auto* domain:{&m.low,&m.high})for(int k=0;k<m.network.activeFeatures;++k)if(!(f>>(*domain)[k]))throw std::runtime_error("Truncated sequence domain");
 for(auto&a:m.counts)read(a);for(auto&a:m.radius)read(a);
 for(int j=0;j<HiddenCount;++j)for(int k=0;k<m.network.activeFeatures;++k)if(!(f>>m.network.w1[j*FeaturesCount+k]))throw std::runtime_error("Truncated sequence weights");
 read(m.network.b1);read(m.network.w2);read(m.network.b2);f>>std::ws;
 if(!f.eof()||!Finite(m.low,6)||!Finite(m.high,6)||!Finite(m.network.w1,100)||!Finite(m.network.b1,100)||!Finite(m.network.w2,100)||!Finite(m.network.b2,100))throw std::runtime_error("Invalid sequence model weights");
 for(int k=0;k<FeaturesCount;++k)if(m.low[k]>m.high[k])throw std::runtime_error("Invalid sequence domain");for(int b=0;b<Branches;++b)for(int k=0;k<Knots;++k)if(m.counts[b][k]>m.rows||!std::isfinite(m.radius[b][k])||m.radius[b][k]<-1||m.radius[b][k]>1e7)throw std::runtime_error("Invalid sequence uncertainty");m.trained=true;return m;
}
bool EmptyDataset(const std::filesystem::path&file){if(!std::filesystem::exists(file))return false;neuralio::Input f(file);return f.peek()==std::char_traits<char>::eof();}
int DatasetSchema(const std::filesystem::path&file){neuralio::Input f(file);std::array<char,64>buffer{};if(!f.getline(buffer.data(),buffer.size()))throw std::runtime_error("Missing or oversized sequence header");if(const int schema=HeaderSchema(buffer.data()))return schema;throw std::runtime_error("Unsupported sequence header");}
FitResult FitDatasets(std::span<const std::filesystem::path>files,uint64_t owner,size_t selection,const std::function<bool()>&cancel,const TrainingPolicy&policy){try{
 // v2/v3 rows convert in memory (Extra unknown); their files are only read, never rewritten.
 storage::Scratch scratch;struct Less{bool operator()(const Sequence&a,const Sequence&b)const{return a.anchor<b.anchor;}};storage::Sort<Sequence,Less>sort(scratch,cancel);std::array<uint64_t,5>rows{};uint64_t unknown=0,empty=0,threat=0,paired=0,extra=0;
 for(const auto&file:files){auto joined=Joined(file,scratch,cancel);for(const auto&row:joined){sort.Add(row);++rows[std::clamp(row.schema,0,4)];if(!row.x[80])++unknown;else if(row.x[92])++threat;else ++empty;extra+=row.extra.recorded>0;for(auto p:row.paired)paired+=p;}}
 storage::Array<Sequence>all(scratch,cancel);uint64_t prior=0;sort.Each([&](const Sequence&row){if(row.anchor==prior)throw std::runtime_error("Overlapping rich corpus anchor IDs; originals retained, explicit provenance merge required");prior=row.anchor;all.push_back(row);});all.Seal();auto result=FitFrameRange(all,owner,cancel,selection,policy);
 result.evaluation="v2_anchors="+std::to_string(rows[2])+" v3_anchors="+std::to_string(rows[3])+" v4_anchors="+std::to_string(rows[4])+" with_shooter_and_speed="+std::to_string(extra)+" movesim_pairs="+std::to_string(paired)+" cues_unknown="+std::to_string(unknown)+" received_empty="+std::to_string(empty)+" received_projectile="+std::to_string(threat)+"\n"+result.evaluation;return result;
 }catch(const std::exception&e){FitResult r;r.evaluation=e.what();return r;}}
strafe::FitResult FitStrafe(std::span<const std::filesystem::path>files,const TrainingPolicy&policy,const std::function<bool()>&cancel){
 strafe::Policy p;p.includeBots=policy.includeBots;p.playerShare=policy.playerShare;
 try{return strafe::Fit([&](const std::function<void(const strafe::Row&)>&sink){for(const auto&file:files)if(!EmptyDataset(file)&&DatasetSchema(file)==FrameSchema)Records(file,[](const Record&){},cancel,sink);},p,cancel);}
 catch(const std::exception&e){strafe::FitResult r;r.evaluation=e.what();return r;}
}
const Model* Select(const Models&m,const Registry&r,uint64_t id,bool unvalidated)noexcept{
 Policy p;if(id){auto it=r.players.find(id);if(it!=r.players.end())p=it->second;}if(!unvalidated||!p.unvalidated)return nullptr;
 if(id&&p.selection==Selection::Imported){auto it=m.imported.find(p.imported);if(it!=m.imported.end()&&it->second.trained)return &it->second;return nullptr;}
 if(id&&p.selection!=Selection::Global&&p.selection!=Selection::Imported){auto it=m.local.find(id);if(it!=m.local.end()&&it->second.trained)return &it->second;}
 auto g=m.local.find(0);return g!=m.local.end()&&g->second.trained?&g->second:nullptr;
}
strafe::Context StrafeContext(const Received&r,const strafe::Stats&stats)noexcept{
 strafe::Context c;const auto&x=r.context;for(int k=0;k<9;++k)if(x[20+k]>.5f)c.cls=k+1;c.ground=r.observation.ground;
 const auto blend=strafe::Blend(stats,r.observation.player);c.playerMean=blend[0];c.playerConfidence=blend[1];
 const auto&e=r.extra;if(e.recorded&&e.shooterKnown&&e.hostile){c.shooterKnown=true;c.shooterDx=e.dx;c.shooterDy=e.dy;c.shooterDistance=std::sqrt(e.dx*e.dx+e.dy*e.dy+e.dz*e.dz);}
 // Received projectiles expected to pass within (blast radius + 48) soon; own/teammate shots ignored.
 for(int slot=0;slot<incoming::Slots;++slot){const int k=LegacyFeatures+incoming::SlotStart+slot*incoming::SlotWidth;if(!x[k]||!x[k+15])continue;if(x[k+10]&&!x[k+13])continue;
  const float closest=x[k+17]*4096.f,eta=x[k+16]*incoming::Horizon,radius=x[k+19]?x[k+20]*512.f:146.f;c.threat=std::max(c.threat,std::clamp(1.f-closest/(radius+48.f),0.f,1.f)*(eta<=.6f?1.f:.5f));}
 return c;
}
struct Service::Impl{
 struct TrainCommand{uint64_t owner,ticket;};struct Set{uint64_t id;Policy policy;};struct ImportCommand{uint64_t id;};struct ImportShotCommand{};using Event=std::variant<Received,TrainCommand,Set,ImportCommand,Outcome,ImportShotCommand,Baseline>;struct Queued{Event event;uint64_t generation;};
 std::filesystem::path root,data;mutable std::mutex mutex,dataMutex;std::condition_variable wake;std::deque<Queued>queue;std::atomic<bool>stop=false,collect=false,includeBots=false;std::atomic<uint64_t>generation=0,dropped=0;
 std::atomic<std::shared_ptr<const Registry>>registry;std::atomic<std::shared_ptr<const Models>>models;std::atomic<std::shared_ptr<const ShotPolicy>>shots;Status status;std::thread worker,trainer;
 struct FitJob{std::array<std::shared_ptr<storage::Snapshot>,3>snapshots{};uint64_t owner,ticket;size_t selection=0;bool includeBots=false;};std::atomic<size_t>sampleSelection=8192;std::mutex fitMutex,publishMutex;std::condition_variable fitWake;std::deque<FitJob>fitQueue;std::atomic<uint64_t>fitGeneration=0;
 std::deque<uint64_t>bootstrap;uint64_t bootstrapTicket=0;
 std::map<uint64_t,History>histories;struct Pending{Received anchor;uint64_t id;unsigned done=0;};std::deque<Pending>pending;
 std::map<uint64_t,Sequence>sequences;std::map<uint64_t,double>lastAnchor;std::map<std::pair<uint64_t,int64_t>,uint64_t>groups;uint64_t serial=Stamp(),epoch=0,newLabels=0,lastFit=0,bytes=0,loadedLabels=0;bool writable=true,needsSeparator=false;std::unique_ptr<appendlog::Writer>appendWriter,outcomeWriter;std::map<uint64_t,uint64_t>personalTrained;
 // Strafe timing: one tracker + collector per received track; per-player leg statistics.
 struct StrafeTrack{strafe::LegTracker tracker;strafe::Collector collector;uint64_t session=0;};std::map<uint64_t,StrafeTrack>strafeTracks;
 strafe::Stats legStats;bool statsDirty=false;Clock::time_point statsPublished{},statsSaved{};
 std::atomic<std::shared_ptr<const strafe::Model>>strafeModel;std::atomic<std::shared_ptr<const strafe::Stats>>strafeStats;
 // Recent sequence anchors per track, for pairing with the movesim path the aimbot used.
 struct RecentAnchor{uint64_t id=0;double time=0;unsigned paired=0;};std::map<uint64_t,std::deque<RecentAnchor>>recentAnchors;
 std::filesystem::path frozenV3;
 explicit Impl(std::filesystem::path p):root(std::move(p)),data(root/"sequence-observations-v4.txt"),frozenV3(root/"sequence-observations-v3.txt"){strafeModel.store(std::make_shared<const strafe::Model>());strafeStats.store(std::make_shared<const strafe::Stats>());registry.store(std::make_shared<const Registry>());models.store(std::make_shared<const Models>());shots.store(std::make_shared<const ShotPolicy>());worker=std::thread([this]{Run();});trainer=std::thread([this]{FitRun();});}
 bool Push(Event e)noexcept{try{std::unique_lock lock(mutex,std::try_to_lock);if(!lock||stop||queue.size()>=EventLimit){++dropped;return false;}queue.push_back({std::move(e),generation.load()});wake.notify_one();return true;}catch(...){++dropped;return false;}}
 void State(std::string operation,std::string error={}){std::lock_guard lock(mutex);status.operation=std::move(operation);status.error=std::move(error);}
 uint64_t Group(const Observation&o){auto key=std::make_pair(o.session,int64_t(o.time/8));auto[it,added]=groups.emplace(key,serial+1);if(added)++serial;uint64_t value=it->second;if(groups.size()>4096)groups.erase(groups.begin());return value;}
 void FlushData(bool force){try{for(auto*p:{appendWriter.get(),outcomeWriter.get()})if(p){if(force)p->Flush();else p->MaybeFlush();}}catch(...){writable=false;throw;}}
 appendlog::Writer& Writer(){if(!appendWriter){if(std::filesystem::exists(data)&&!EmptyDataset(data)&&DatasetSchema(data)!=FrameSchema)throw std::runtime_error("Cue corpus namespace/schema mismatch; no append performed");if(!appendlog::IsAppend(data))neuralio::EnsureAppend(data);appendWriter=std::make_unique<appendlog::Writer>(data);}return *appendWriter;}
 void Append(const std::string&row){if(!writable)storage::Fail(data,"Sequence writes paused after IO error");try{auto&out=Writer();const std::string prefix=!out.LogicalBytes()?std::string(HeaderV4)+"\n":out.LastByte()!='\n'?"\n":"";out.Append(prefix+row);bytes=std::filesystem::file_size(data);for(auto old:{root/"sequence-observations-v2.txt",frozenV3})if(std::filesystem::exists(old))bytes+=std::filesystem::file_size(old);{std::lock_guard lock(mutex);status.diskBytes=bytes;}}catch(...){writable=false;throw;}}
 void ReadData(const std::filesystem::path&file){if(!std::filesystem::exists(file)||EmptyDataset(file))return;if(DatasetSchema(file)!=(file==data?FrameSchema:file==frozenV3?Schema:2))throw std::runtime_error("Rich corpus namespace/schema mismatch; originals retained");bytes+=std::filesystem::file_size(file);auto cancel=[&]{return bool(stop);};storage::Scratch scratch;uint64_t strafeRows=0,pairs=0;auto joined=Joined(file,scratch,cancel,[&](const strafe::Row&row){++strafeRows;serial=std::max(serial,row.id);});uint64_t anchors=0;
 for(const auto&a:joined){++anchors;for(auto q:a.paired)pairs+=q;serial=std::max({serial,a.anchor,a.group});for(int k=0;k<Knots;++k){loadedLabels+=a.horizon[k]>0;serial=std::max(serial,a.endGroup[k]);}sequences[a.anchor]=a;if(sequences.size()>RetainedAnchors)sequences.erase(sequences.begin());}
 {std::lock_guard lock(mutex);status.anchors+=anchors;status.diskBytes=bytes;status.strafeRows+=strafeRows;status.paired+=pairs;}
 }

 void ReadPolicies(){auto file=root/"sequence-players-v2.txt";if(!std::filesystem::exists(file))return;std::istringstream f(metadata::Read(file,512*1024));std::string line;std::getline(f,line);if(!line.empty()&&line.back()=='\r')line.pop_back();if(line!="MOVEMENT_PLAYERS 2")throw std::runtime_error("Unsupported player policies");Registry r;uint64_t id;while(f>>id){int train,force,fallback,unvalidated,selection;Policy p;if(!(f>>train>>force>>fallback>>unvalidated>>selection>>p.imported)||!id||id>UINT32_MAX||train<0||train>1||force<0||force>1||fallback<0||fallback>1||unvalidated<0||unvalidated>1||r.players.contains(id)||r.players.size()>=1024)throw std::runtime_error("Invalid player policy");p.train=train!=0;p.force=force!=0;p.allowMovesim=fallback!=0;p.unvalidated=unvalidated!=0;p.selection=Selection(selection);if(!ValidPolicy(p))throw std::runtime_error("Invalid player model selection");r.players[id]=p;}if(!f.eof())throw std::runtime_error("Truncated player policy");registry.store(std::make_shared<const Registry>(std::move(r)));}
 void SavePolicies(const Registry&r){std::ostringstream out;out<<"MOVEMENT_PLAYERS 2\n";for(auto[id,p]:r.players)out<<id<<' '<<p.train<<' '<<p.force<<' '<<p.allowMovesim<<' '<<p.unvalidated<<' '<<int(p.selection)<<' '<<p.imported<<'\n';metadata::Replace(root/"sequence-players-v2.txt",out.str(),512*1024);}
 void ObservationEvent(const Received&r){const auto&o=r.observation;auto policies=registry.load();auto policy=policies->players.find(o.player);if(!collect&&(!o.player||policy==policies->players.end()||!policy->second.train))return;if(!Valid(o)||!Valid(r.context)||!ValidExtra(r.extra))return;{std::lock_guard lock(mutex);++status.observations;}
 if(histories.size()>=128&&!histories.contains(o.track)){histories.clear();pending.clear();lastAnchor.clear();strafeTracks.clear();recentAnchors.clear();}auto&h=histories[o.track];bool discontinuity=false;if(h.count){const auto&last=h.points[h.count-1].observation;const double dt=o.time-last.time;if(dt==0&&last.session==o.session)return;discontinuity=last.session!=o.session||last.player!=o.player||dt<.005||dt>.2||Length(Sub(o.position,last.position))>dt*3000+32;}
 if(discontinuity){h={};lastAnchor.erase(o.track);std::erase_if(pending,[&](const Pending&p){return p.anchor.observation.track==o.track;});}uint64_t endGroup=Group(o);
 for(auto&p:pending){const auto&a=p.anchor.observation;if(a.track!=o.track||a.session!=o.session)continue;const double elapsed=o.time-a.time;for(int k=0;k<Knots;++k){const double knot=a.time+double(Horizons[k]);if(p.done&(1u<<k)||o.time<knot)continue;p.done|=1u<<k;if(!h.count)continue;const auto&prior=h.points[h.count-1].observation;const double dt=o.time-prior.time;
 if(dt<=0||dt>.2||prior.time>knot||o.time<knot)continue;
 const float alpha=float((knot-prior.time)/dt);Vec observed{};for(int axis=0;axis<3;++axis)observed[axis]=prior.position[axis]+alpha*(o.position[axis]-prior.position[axis]);
 const float nominal=Horizons[k];auto found=sequences.find(p.id);if(found==sequences.end())continue;auto displacement=Sub(observed,a.position);if(!Finite(displacement)||Length(displacement)>3000*elapsed+32)continue;std::ostringstream row;row.imbue(std::locale::classic());row<<std::setprecision(9)<<"L "<<p.id<<' '<<endGroup<<' '<<k<<' '<<nominal<<' '<<displacement[0]<<' '<<displacement[1]<<' '<<displacement[2]<<'\n';Append(row.str());auto&s=found->second;s.horizon[k]=nominal;s.endGroup[k]=endGroup;s.displacement[k]=displacement;++newLabels;std::lock_guard lock(mutex);++status.examples;}}
 std::erase_if(pending,[&](const Pending&p){return p.done==((1u<<Knots)-1)||(p.anchor.observation.session==o.session&&o.time-p.anchor.observation.time>Horizons.back()+.2);});
 if(h.count==h.points.size()){std::move(h.points.begin()+1,h.points.end(),h.points.begin());--h.count;}h.points[h.count++]=r;Input x;if((!lastAnchor.contains(o.track)||o.time-lastAnchor[o.track]>=std::max(.5,80.*double(histories.size())/4000.))&&Features(h,x)){Sequence s;s.anchor=++serial;s.player=o.player;s.group=endGroup;s.x=x;std::ostringstream row;row.imbue(std::locale::classic());s.extra=r.extra;row<<std::setprecision(9)<<"A "<<s.anchor<<' '<<s.player<<' '<<s.group;for(auto v:x)row<<' '<<v;for(auto v:r.extra.Values())row<<' '<<v;row<<'\n';Append(row.str());
  {auto&recent=recentAnchors[o.track];recent.push_back({s.anchor,o.time,0});if(recent.size()>8)recent.pop_front();}{std::lock_guard lock(mutex);++status.anchors;}sequences[s.anchor]=s;if(sequences.size()>RetainedAnchors)sequences.erase(sequences.begin());if(pending.size()>=4096){pending.pop_front();++dropped;}pending.push_back({r,s.anchor});lastAnchor[o.track]=o.time;}
 StrafeEvent(r,endGroup);
 }
 // Lateral legs from the same received stream; rows are written only once resolved.
 void StrafeEvent(const Received&r,uint64_t group){
  const auto&o=r.observation;auto&st=strafeTracks[o.track];if(st.session!=o.session){st=StrafeTrack{};st.session=o.session;}
  const auto reversal=st.tracker.Update({o.time,o.position[0],o.position[1],o.velocity[0],o.velocity[1],o.ground});
  if(reversal.happened&&o.player&&reversal.duration>=strafe::MinLeg&&reversal.duration<=strafe::MaxLeg&&(legStats.players.size()<65536||legStats.players.contains(o.player))){
   const double d=reversal.duration;auto&p=legStats.players[o.player];++p.n;p.sum+=d;p.square+=d*d;++legStats.global.n;legStats.global.sum+=d;legStats.global.square+=d*d;statsDirty=true;}
  const auto context=StrafeContext(r,legStats);
  st.collector.Step(st.tracker,reversal,context,o.time,[&](const strafe::Features&x,float remaining,float observed){strafe::Row row;row.id=++serial;row.player=o.player;row.group=group;row.x=x;row.remaining=remaining;row.observed=observed;Append("R "+strafe::Encode(row)+"\n");std::lock_guard lock(mutex);++status.strafeRows;});
 }
 void BaselineEvent(const Baseline&b){
  if(!collect)return;const auto&a=b.anchor.past[2];auto it=recentAnchors.find(a.track);if(it==recentAnchors.end()||b.count<2||b.count>b.points.size())return;
  for(auto&recent:it->second){if(std::abs(recent.time-a.time)>1e-6)continue;
   for(int k=0;k<FrameKnots;++k){if(recent.paired&(1u<<k))continue;const double target=a.time+Horizons[k];
    for(size_t j=1;j<b.count;++j){const auto&l=b.points[j-1];const auto&u=b.points[j];if(!(l.time<=target&&u.time>=target&&u.time>l.time&&u.time-l.time<.05))continue;
     const double f=(target-l.time)/(u.time-l.time);Vec d{};for(int q=0;q<3;++q)d[q]=float(l.position[q]+(u.position[q]-l.position[q])*f-a.position[q]);
     if(Finite(d)&&Length(d)<=3000*Horizons[k]+32){std::ostringstream row;row.imbue(std::locale::classic());row<<std::setprecision(9)<<"B "<<recent.id<<' '<<k<<' '<<d[0]<<' '<<d[1]<<' '<<d[2]<<'\n';Append(row.str());recent.paired|=1u<<k;std::lock_guard lock(mutex);++status.paired;}
     break;}}
   break;}
 }
 void Training(uint64_t owner,uint64_t ticket=UINT64_MAX){
   if(ticket==UINT64_MAX)ticket=fitGeneration;if(ticket!=fitGeneration)return;if(!std::filesystem::exists(data)&&!std::filesystem::exists(frozenV3)&&!std::filesystem::exists(root/"sequence-observations-v2.txt")){State("Collect observations, then request training explicitly");return;}
   FitJob job;job.owner=owner;job.ticket=ticket;FlushData(true);job.snapshots={std::make_shared<storage::Snapshot>(root/"sequence-observations-v2.txt"),std::make_shared<storage::Snapshot>(frozenV3),std::make_shared<storage::Snapshot>(data)};job.selection=sampleSelection;job.includeBots=includeBots;
   {std::lock_guard lock(fitMutex);for(auto&queued:fitQueue)if(queued.owner==owner){queued=std::move(job);fitWake.notify_one();return;}if(fitQueue.size()>=2){State("Sequence training queue full; collection continues");return;}fitQueue.push_back(std::move(job));}fitWake.notify_one();
 }
 void FitRun()noexcept{
   while(!stop){FitJob job;{std::unique_lock lock(fitMutex);fitWake.wait_for(lock,std::chrono::milliseconds(100),[&]{return stop||!fitQueue.empty();});if(stop)break;if(fitQueue.empty())continue;job=std::move(fitQueue.front());fitQueue.pop_front();}if(job.ticket!=fitGeneration)continue;
     {std::lock_guard lock(mutex);status.training=true;status.operation=std::string(job.owner?"Training personal sequence candidate":"Training global sequence candidate")+"; selection="+(job.selection?std::to_string(job.selection):"all")+"; snapshot bytes="+std::to_string(job.snapshots[0]->bytes+job.snapshots[1]->bytes);}
     try{auto cancel=[&]{return stop||job.ticket!=fitGeneration;};storage::Scratch scratch;std::vector<std::filesystem::path>paths;for(auto&snapshot:job.snapshots)if(snapshot&&snapshot->input){auto path=scratch.Next();snapshot->Copy(path,cancel);paths.push_back(path);snapshot.reset();}TrainingPolicy policy;policy.includeBots=job.includeBots;auto result=FitDatasets(paths,job.owner,job.selection,cancel,policy);
       if(result.fitted){std::lock_guard commit(fitMutex);if(stop||job.ticket!=fitGeneration){result.fitted=false;result.evaluation="Sequence fit cancelled; previous model retained";}else{std::lock_guard publish(publishMutex);auto next=*models.load();if(next.local.size()>=129&&!next.local.contains(job.owner))throw std::runtime_error("Sequence model capacity reached");SaveModel(root/("sequence-v4-model-"+std::to_string(job.owner)+".txt"),result.model);next.local[job.owner]=std::move(result.model);models.store(std::make_shared<const Models>(std::move(next)));}}
       metadata::Replace(root/("sequence-v4-evaluation-"+std::to_string(job.owner)+".txt"),result.evaluation,256*1024);
       // Strafe timing is global (personal rhythm enters through per-player leg statistics).
       std::string strafeReport;bool strafeAccepted=strafeModel.load()->accepted;
       if(!job.owner&&!cancel()){{std::lock_guard lock(mutex);status.operation="Training strafe timing model";}auto timing=FitStrafe(paths,policy,cancel);strafeReport=timing.evaluation;
        if(timing.fitted&&!cancel()){const auto old=strafeModel.load();if(timing.model.accepted||!old->accepted){SaveModel(root/"strafe-model-v1.txt",timing.model);strafeModel.store(std::make_shared<const strafe::Model>(timing.model));strafeAccepted=timing.model.accepted;}else strafeReport+="Previous accepted strafe timing model retained.\n";}
        metadata::Replace(root/"strafe-evaluation-v1.txt",strafeReport.empty()?std::string("No strafe timing report"):strafeReport,256*1024);}
       std::lock_guard lock(mutex);status.training=false;status.evaluation=result.evaluation;if(!strafeReport.empty())status.strafeEvaluation=strafeReport;status.strafeAccepted=strafeAccepted;status.models=models.load()->local.size();status.operation=result.fitted?"Sequence model fitted (schema 4); legacy accepted models unchanged":result.evaluation;
     }catch(const std::exception&e){std::lock_guard lock(mutex);status.training=false;status.error=e.what();status.operation="Sequence training failed; previous model retained";}
   }
 }

 void LoadOutcomes(const std::filesystem::path&file,ShotPolicy&policy){if(!std::filesystem::exists(file))return;for(const auto&o:ReadOutcomes(file))if(!policy.Add(o))throw std::runtime_error("Invalid or duplicate shot outcome across files");}
 void OutcomeEvent(const Outcome&o){auto next=*shots.load();if(!next.Add(o))return;auto file=root/"chosen-outcomes-v3.txt";if(!outcomeWriter){neuralio::EnsureAppend(file);outcomeWriter=std::make_unique<appendlog::Writer>(file);}auto&out=*outcomeWriter;const auto row=OutcomeRow(o);const std::string prefix=!out.LogicalBytes()?"MOVEMENT_CHOSEN_OUTCOMES 3\n":out.LastByte()!='\n'?"\n":"";if(out.LogicalBytes()+prefix.size()+row.size()>OutcomeByteLimit)throw std::runtime_error("Outcome storage limit reached");try{out.Append(prefix+row);out.Flush();}catch(...){writable=false;throw;}shots.store(std::make_shared<const ShotPolicy>(std::move(next)));}
 void Run()noexcept{try{
 std::filesystem::create_directories(layout::Transfer(root,"SequenceImport"));ReadPolicies();{std::lock_guard lock(mutex);status.policiesReady=true;}
 Models m;std::string modelErrors;
 for(int schema:{2,3,FrameSchema}){const std::string prefix=schema==2?"sequence-model-":schema==3?"sequence-v3-model-":"sequence-v4-model-";
  for(const auto&e:std::filesystem::directory_iterator(root)){const auto name=e.path().filename().string();if(!name.starts_with(prefix)||!name.ends_with(".txt"))continue;
   try{auto model=LoadModel(e.path());if(model.schema!=schema||name!=prefix+std::to_string(model.owner)+".txt")throw std::runtime_error("Model namespace/provenance mismatch");if(m.local.size()>=129&&!m.local.contains(model.owner))throw std::runtime_error("Sequence model capacity exceeded");m.local[model.owner]=std::move(model);}
   catch(const std::exception&e){modelErrors+=name+": "+e.what()+"; prior usable version retained. ";}
  }
 }
 for(const auto&[id,policy]:registry.load()->players)if(policy.selection==Selection::Imported&&policy.imported&&!m.imported.contains(policy.imported)){
  auto file=layout::Transfer(root,"SequenceImport")/(std::to_string(policy.imported)+".txt");if(m.imported.size()<64&&std::filesystem::exists(file))try{m.imported[policy.imported]=LoadModel(file);}catch(const std::exception&e){modelErrors+=std::string("Selected import unavailable: ")+e.what()+"; ";}
 }
 models.store(std::make_shared<const Models>(std::move(m)));{std::lock_guard lock(mutex);status.models=models.load()->local.size();}
 if(std::filesystem::exists(root/"strafe-model-v1.txt"))try{auto timing=strafe::LoadModel(root/"strafe-model-v1.txt");strafeModel.store(std::make_shared<const strafe::Model>(timing));std::lock_guard lock(mutex);status.strafeAccepted=timing.accepted;}catch(const std::exception&e){modelErrors+=std::string("strafe-model-v1.txt: ")+e.what()+"; ";}
 if(std::filesystem::exists(root/"strafe-evaluation-v1.txt"))try{auto text=metadata::Read(root/"strafe-evaluation-v1.txt",256*1024);std::lock_guard lock(mutex);status.strafeEvaluation=text;}catch(...){}
 if(std::filesystem::exists(root/"strafe-players-v1.txt"))try{legStats=strafe::LoadStats(root/"strafe-players-v1.txt");strafeStats.store(std::make_shared<const strafe::Stats>(legStats));}catch(const std::exception&e){modelErrors+=std::string("strafe-players-v1.txt: ")+e.what()+"; ";}
 {std::lock_guard dataLock(dataMutex);if(std::filesystem::exists(data)&&!EmptyDataset(data)&&DatasetSchema(data)!=FrameSchema)throw std::runtime_error("Frame corpus namespace/schema mismatch; originals retained");neuralio::EnsureAppend(data);neuralio::EnsureAppend(frozenV3);for(auto file:{"chosen-outcomes-v2.txt","chosen-outcomes-v3.txt"})neuralio::EnsureAppend(root/file);}
 // v2 and v3 corpora are read only: new observations go to the v4 file.
 ReadData(root/"sequence-observations-v2.txt");ReadData(frozenV3);ReadData(data);{std::lock_guard dataLock(dataMutex);if(std::filesystem::exists(data))Writer();}
 ShotPolicy outcomes;LoadOutcomes(root/"chosen-outcomes-v2.txt",outcomes);LoadOutcomes(root/"chosen-outcomes-v3.txt",outcomes);shots.store(std::make_shared<const ShotPolicy>(std::move(outcomes)));
 {std::lock_guard lock(mutex);status.examples=loadedLabels;status.ready=true;}State("Ready; cue collection saves automatically, training requires an explicit request",modelErrors);
 }catch(const std::exception&e){writable=false;State("Sequence data unavailable; writes/training paused, usable models and originals retained",e.what());}
 while(true){try{std::lock_guard dataLock(dataMutex);FlushData(false);}catch(const std::exception&e){State("Sequence writes paused after compression flush failure",e.what());}
 if(statsDirty){const auto now=Clock::now();if(now-statsPublished>std::chrono::seconds(5)){strafeStats.store(std::make_shared<const strafe::Stats>(legStats));statsPublished=now;}if(now-statsSaved>std::chrono::seconds(60)){try{strafe::SaveStats(root/"strafe-players-v1.txt",legStats);statsDirty=false;}catch(...){}statsSaved=now;}}Event event;uint64_t queuedEpoch=0;{std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(100),[&]{return stop||!queue.empty();});if(queue.empty()){if(stop)break;continue;}queuedEpoch=queue.front().generation;event=std::move(queue.front().event);queue.pop_front();}if(epoch!=generation){histories.clear();pending.clear();lastAnchor.clear();strafeTracks.clear();recentAnchors.clear();epoch=generation;}if(std::holds_alternative<Received>(event)&&queuedEpoch!=epoch)continue;if(!writable)continue;
 try{std::lock_guard dataLock(dataMutex);if(auto observation=std::get_if<Received>(&event))ObservationEvent(*observation);else if(auto baseline=std::get_if<Baseline>(&event))BaselineEvent(*baseline);else if(auto train=std::get_if<TrainCommand>(&event)){if(!stop)Training(train->owner,train->ticket);}else if(auto set=std::get_if<Set>(&event)){auto next=*registry.load();if(next.players.size()>=1024&&!next.players.contains(set->id))throw std::runtime_error("Player policy capacity reached");next.players[set->id]=set->policy;SavePolicies(next);registry.store(std::make_shared<const Registry>(std::move(next)));}else if(auto imported=std::get_if<ImportCommand>(&event)){auto model=LoadModel(layout::Transfer(root,"SequenceImport")/(std::to_string(imported->id)+".txt"));std::lock_guard publish(publishMutex);auto next=*models.load();if(next.imported.size()>=64&&!next.imported.contains(imported->id))throw std::runtime_error("Imported model catalog full");next.imported[imported->id]=std::move(model);models.store(std::make_shared<const Models>(std::move(next)));State("Imported trained sequence candidate (unvalidated)");}
 if(auto outcome=std::get_if<Outcome>(&event))OutcomeEvent(*outcome);
 if(std::holds_alternative<ImportShotCommand>(event)){FlushData(true);outcomeWriter.reset();auto path=layout::Transfer(root,"SequenceImport")/"verified-outcomes.txt";if(!std::filesystem::exists(path))throw std::runtime_error("Missing SequenceImport/verified-outcomes.txt");auto imported=ReadOutcomes(path);ShotPolicy validated;LoadOutcomes(root/"chosen-outcomes-v2.txt",validated);for(const auto&o:imported)if(!validated.Add(o))throw std::runtime_error("Imported outcome duplicates retained legacy ID or exceeds combined limits");WriteOutcomes(root/"chosen-outcomes-v3.txt",imported);neuralio::EnsureAppend(root/"chosen-outcomes-v3.txt");shots.store(std::make_shared<const ShotPolicy>(std::move(validated)));State("Imported user-supplied v3 outcomes; legacy v2 and prior v3 retained");}

 }catch(const std::exception&e){std::lock_guard lock(mutex);status.error=e.what();status.operation=writable?"Sequence operation failed; original files retained":"Sequence writes paused after IO error; inspect dataset tail before restarting";}}
 try{std::lock_guard dataLock(dataMutex);FlushData(true);}catch(const std::exception&e){State("Shutdown flush failed; retained file can be recovered on restart",e.what());}
 if(statsDirty)try{strafe::SaveStats(root/"strafe-players-v1.txt",legStats);}catch(...){}
 }
};
Service::Service(std::filesystem::path p):p(std::make_unique<Impl>(std::move(p))){}
Service::~Service(){Stop();}void Service::Configure(bool enabled,size_t requested,bool all,bool bots){p->sampleSelection=all?0:std::clamp(requested,size_t(1024),size_t(1000000));p->includeBots=bots;if(p->collect.exchange(enabled)!=enabled){++p->generation;if(!enabled){auto policies=p->registry.load();if(std::none_of(policies->players.begin(),policies->players.end(),[](const auto&e){return e.second.train;}))Cancel();}}}
bool Service::Observe(const Received&o)noexcept{return p->Push(o);}bool Service::RecordBaseline(const Baseline&b)noexcept{return p->Push(b);}
std::shared_ptr<const strafe::Model> Service::StrafeModel()const{return p->strafeModel.load();}std::shared_ptr<const strafe::Stats> Service::StrafeStats()const{return p->strafeStats.load();}bool Service::Train(uint64_t id)noexcept{return id<=UINT32_MAX&&p->Push(Impl::TrainCommand{id,p->fitGeneration.load()});}
bool Service::SetPolicy(uint64_t id,const Policy&policy)noexcept{return id&&id<=UINT32_MAX&&ValidPolicy(policy)&&p->Push(Impl::Set{id,policy});}
bool Service::RecordOutcome(const Outcome&o)noexcept{return p->Push(o);}
bool Service::ImportOutcomes()noexcept{return p->Push(Impl::ImportShotCommand{});}
std::shared_ptr<const ShotPolicy> Service::Outcomes()const{return p->shots.load();}
bool Service::Import(uint64_t id)noexcept{return id&&id<=UINT32_MAX&&p->Push(Impl::ImportCommand{id});}
std::shared_ptr<const Registry> Service::Policies()const{return p->registry.load();}std::shared_ptr<const Models> Service::Published()const{return p->models.load();}
Status Service::GetStatus()const{std::lock_guard lock(p->mutex);auto s=p->status;s.collecting=p->collect;s.queued=p->queue.size();s.dropped=p->dropped;return s;}
void Service::ResetHistory()noexcept{++p->generation;}void Service::Cancel()noexcept{std::lock_guard lock(p->fitMutex);++p->fitGeneration;p->fitQueue.clear();}
std::array<std::shared_ptr<storage::Snapshot>,3> Service::SnapshotCorpora()const{std::lock_guard lock(p->dataMutex);p->FlushData(true);return {std::make_shared<storage::Snapshot>(p->root/"sequence-observations-v2.txt"),std::make_shared<storage::Snapshot>(p->frozenV3),std::make_shared<storage::Snapshot>(p->data)};}
std::shared_ptr<storage::Snapshot> Service::SnapshotData()const{std::lock_guard lock(p->dataMutex);if(std::filesystem::exists(p->data)&&!appendlog::IsAppend(p->data))throw std::runtime_error("Neural storage initialization is in progress; retry the snapshot after readiness");p->FlushData(true);return std::make_shared<storage::Snapshot>(p->data);}
void Service::Stop()noexcept{if(!p)return;p->stop=true;p->wake.notify_all();p->fitWake.notify_all();if(p->worker.joinable())p->worker.join();if(p->trainer.joinable())p->trainer.join();{std::lock_guard lock(p->fitMutex);p->fitQueue.clear();}}
bool ShotPolicy::Add(const Outcome&o){
 if(o.evidence>3||(o.known&&o.evidence==3)||!o.shot||o.weapon>1023||o.rangeBand>15||int(o.chosen)<1||int(o.chosen)>3||!std::isfinite(o.damage)||o.damage<0||o.damage>10000||seen.contains(o.shot)||seen.size()>=65536)return false;
 const bool known=KnownWeaponContext(o.context);if(!known&&!(o.context==WeaponContext{}))return false;
 const unsigned legacy=(o.weapon<<5)|(o.rangeBand<<1)|unsigned(o.air);
 const auto key=ContextKeyFor(o.weapon,o.context,o.rangeBand,o.air);
 if(cells.size()+contextCells.size()>=4096&&(known?!contextCells.contains(key):!cells.contains(legacy)))return false;
 seen.insert(o.shot);auto&arm=(known?contextCells[key]:cells[legacy])[int(o.chosen)-1];++arm.attempted;if(o.known){++arm.known;arm.hits+=o.damage>0;arm.kills+=o.killed;arm.damage+=o.damage;}return true;
}
namespace {
Splash ChooseArms(const std::array<Arm,3>&arms,Objective objective,bool&evidence,bool explore){
 int least=0;uint64_t attempts=0;bool sufficient=true;
 for(int i=0;i<3;++i){const auto&a=arms[i];attempts+=a.attempted;if(a.attempted<arms[least].attempted)least=i;if(a.known<16||a.known<double(a.attempted)*.95)sufficient=false;}
 if(!sufficient)return explore?Splash(least+1):Splash::Include;
 // One stable exploratory choice per16 completed attempts; repeated inference cannot advance it.
 if(explore&&attempts&&attempts%16==0)return Splash(least+1);
 double best=-1e30;int selected=0;
 for(int i=0;i<3;++i){const auto&a=arms[i];double score=objective==Objective::ExpectedDamage?a.damage/a.known:double(objective==Objective::Kill?a.kills:a.hits)/a.known;double penalty=objective==Objective::ExpectedDamage?200/std::sqrt(double(a.known)):1/std::sqrt(double(a.known));if(score-penalty>best){best=score-penalty;selected=i;}}
 evidence=true;return Splash(selected+1);
}
}
Splash ShotPolicy::Choose(unsigned weapon,unsigned range,bool air,Objective objective,bool&evidence,bool explore)const{
 evidence=false;if(weapon>1023||range>15||int(objective)<0||int(objective)>2)return Splash::Include;
 auto it=cells.find((weapon<<5)|(range<<1)|unsigned(air));return it==cells.end()?Splash::Include:ChooseArms(it->second,objective,evidence,explore);
}
Splash ShotPolicy::Choose(unsigned weapon,const WeaponContext&context,unsigned range,bool air,Objective objective,bool&evidence,bool explore)const{
 evidence=false;if(weapon>1023||range>15||int(objective)<0||int(objective)>2||!KnownWeaponContext(context))return Splash::Include;
 auto it=contextCells.find(ContextKeyFor(weapon,context,range,air));return it==contextCells.end()?Splash::Include:ChooseArms(it->second,objective,evidence,explore);
}
}
