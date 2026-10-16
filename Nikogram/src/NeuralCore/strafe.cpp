#include "strafe.h"
#include "storage.h"
#include "metadata.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>

namespace movement::strafe {
namespace {
template<class A>bool Finite(const A&a,float bound=1e6f){for(auto v:a)if(!std::isfinite(v)||std::abs(v)>bound)return false;return true;}
uint64_t Mix(uint64_t x){x+=0x9e3779b97f4a7c15ull;x=(x^(x>>30))*0xbf58476d1ce4e5b9ull;x=(x^(x>>27))*0x94d049bb133111ebull;return x^(x>>31);}
uint64_t Stamp(){return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
// Binary/categorical inputs may be new at inference; continuous ones need training coverage.
constexpr std::array<bool,Inputs> Continuous{1,1,0,1,1,0,1,1,1,1,1,0, 0,0,0,0,0,0,0,0,0,0, 1,1,0,1,1,1};
}

Reversal LegTracker::Update(const Sample&s)noexcept{
 Reversal r;
 if(!std::isfinite(s.time)||!std::isfinite(s.x)||!std::isfinite(s.y)||!std::isfinite(s.vx)||!std::isfinite(s.vy))return r;
 if(count&&s.time<=samples[count-1].time)return r;
 if(count&&s.time-samples[count-1].time>.2)Reset();
 if(count==samples.size()){std::move(samples.begin()+1,samples.end(),samples.begin());--count;}
 samples[count++]=s;
 // Principal lateral axis over the last second, time weighted (packet bursts do not bias it).
 size_t first=count-1;while(first>0&&s.time-samples[first-1].time<=AxisSeconds)--first;
 const double duration=s.time-samples[first].time;bool valid=false;float nax=ax,nay=ay,var=0;
 if(count-first>=6&&duration>=.2){
  double mx=0,my=0;for(size_t i=first+1;i<count;++i){const double dt=samples[i].time-samples[i-1].time;mx+=samples[i].vx*dt;my+=samples[i].vy*dt;}mx/=duration;my/=duration;
  double xx=0,xy=0,yy=0;for(size_t i=first+1;i<count;++i){const double dt=samples[i].time-samples[i-1].time,x=samples[i].vx-mx,y=samples[i].vy-my;xx+=x*x*dt;xy+=x*y*dt;yy+=y*y*dt;}
  const double angle=.5*std::atan2(2*xy,xx-yy);nax=float(std::cos(angle));nay=float(std::sin(angle));
  const double along=xx*nax*nax+2*xy*nax*nay+yy*nay*nay;var=float(along/duration);
  valid=along>=.82*(xx+yy)&&var>=900.f;
 }
 auto lose=[&]{axisValid=false;sign=pending=0;legStart=-1;crossing=-1;legCount=0;peak=0;};
 if(!valid){if(axisValid)lose();lastTime=-1;return r;}
 if(axisValid){float dot=nax*ax+nay*ay;if(dot<0){nax=-nax;nay=-nay;dot=-dot;}if(dot<.866f){lose();}}
 ax=nax;ay=nay;variance=var;axisValid=true;
 const float u=s.vx*ax+s.vy*ay;
 if(lastTime>=0&&s.time>lastTime){if(lastU*u<0)crossing=lastTime+(s.time-lastTime)*double(lastU/(lastU-u));accel=float((u-lastU)/(s.time-lastTime));}else accel=0;
 const float threshold=std::max(20.f,std::sqrt(variance)*.22f);
 const int next=u>threshold?1:u<-threshold?-1:0;
 if(!next)pending=0;
 else if(next==sign){pending=0;peak=std::max(peak,std::abs(u));}
 else if(!sign){sign=next;pending=0;peak=std::abs(u);}
 else if(next!=pending){pending=next;pendingTime=s.time;}
 else if(s.time-pendingTime>=.012){
  // Confirmed: two coherent samples. Time it at the interpolated zero crossing.
  const double at=crossing>=0&&crossing<=s.time&&crossing>=pendingTime-.2&&crossing>legStart?crossing:pendingTime;
  r.happened=true;r.time=at;
  if(legStart>=0){r.duration=float(at-legStart);
   if(r.duration>=MinLeg&&r.duration<=MaxLeg){if(legCount==MaxLegs){std::move(legs.begin()+1,legs.end(),legs.begin());--legCount;}legs[legCount++]=r.duration;}
   else if(r.duration>MaxLeg)legCount=0; // a long hold is a new pattern, not a leg
  }
  legStart=at;sign=next;pending=0;peak=std::abs(u);
 }
 lastU=u;lastTime=s.time;return r;
}
float LegTracker::MeanLeg(int n)const noexcept{const int k=std::min(n,legCount);if(k<=0)return 0;float sum=0;for(int i=legCount-k;i<legCount;++i)sum+=legs[i];return sum/float(k);}
float LegTracker::StdLeg(int n)const noexcept{const int k=std::min(n,legCount);if(k<2)return 0;const float m=MeanLeg(n);float sum=0;for(int i=legCount-k;i<legCount;++i)sum+=(legs[i]-m)*(legs[i]-m);return std::sqrt(sum/float(k));}

std::array<float,2> Blend(const Stats&s,uint64_t player)noexcept{
 const float global=s.global.n?float(s.global.Mean()):.35f;
 if(!player)return {global,0.f};auto it=s.players.find(player);if(it==s.players.end()||!it->second.n)return {global,0.f};
 const float n=float(it->second.n);return {(n*float(it->second.Mean())+PriorLegs*global)/(n+PriorLegs),n/(n+PriorLegs)};
}

bool MakeFeatures(const LegTracker&t,const Context&c,double now,float extra,Features&x)noexcept{
 x.fill(0);if(!t.Strafing()||!std::isfinite(now)||!std::isfinite(extra))return false;
 const float elapsed=float(now-t.legStart)+extra;if(!(elapsed>=-.02f)||elapsed>MaxLeg+Window)return false;
 const float mean=t.MeanLeg(4);
 x[0]=std::max(0.f,elapsed);x[1]=t.legCount?t.legs[t.legCount-1]:0;x[2]=t.legCount?1.f:0.f;x[3]=mean;x[4]=t.StdLeg(4);x[5]=float(std::min(t.legCount,4))/4.f;
 x[6]=mean>0?std::clamp(x[0]/mean,0.f,3.f):0;
 x[7]=std::min(std::abs(t.Lateral())/450.f,3.f);x[8]=std::min(t.peak/450.f,3.f);x[9]=std::clamp(-float(t.sign)*t.accel/3000.f,-3.f,3.f);x[10]=std::min(std::abs(t.Along())/450.f,3.f);x[11]=c.ground?1.f:0.f;
 if(c.cls>=1&&c.cls<=9){x[12+c.cls-1]=1;x[21]=1;}
 x[22]=std::clamp(c.playerMean,0.f,3.f);x[23]=std::clamp(c.playerConfidence,0.f,1.f);
 if(c.shooterKnown){const float d=std::hypot(c.shooterDx,c.shooterDy);if(d>1){x[24]=1;x[25]=std::abs((c.shooterDx*t.ax+c.shooterDy*t.ay)/d);x[26]=std::min(c.shooterDistance/3000.f,6.f);}}
 x[27]=std::clamp(c.threat,0.f,1.f);
 return Finite(x,6);
}

std::string Encode(const Row&r){std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(6)<<r.id<<' '<<r.player<<' '<<r.group<<' '<<r.remaining<<' '<<r.observed;for(auto v:r.x)o<<' '<<v;return o.str();}
bool Decode(const std::string&text,Row&r)noexcept{try{std::istringstream in(text);in.imbue(std::locale::classic());if(!(in>>r.id>>r.player>>r.group>>r.remaining>>r.observed)||!r.id||!r.group||r.player>UINT32_MAX)return false;for(auto&v:r.x)if(!(in>>v))return false;in>>std::ws;
 const bool event=r.remaining>=0&&r.observed==-1.f,censored=r.remaining==-1.f&&r.observed>=0;
 return in.eof()&&Finite(r.x,6)&&(event||censored)&&std::isfinite(r.remaining)&&std::isfinite(r.observed)&&r.remaining<=MaxRecorded&&r.observed<=MaxRecorded&&r.x[0]>=0&&r.x[0]<=MaxLeg+Window;}catch(...){return false;}}
int Bin(float t)noexcept{if(!(t>=0))return 0;return std::min(int(t/Window),Bins-1);}

Network::Network(uint32_t seed){std::mt19937 rng(seed);std::uniform_real_distribution<float>d(-.1f,.1f);for(auto&v:w1)v=d(rng);for(auto&v:w2)v=d(rng);}
namespace {
void Hiddens(const Network&n,const Features&x,std::array<float,Hidden>&h){for(int j=0;j<Hidden;++j){float v=n.b1[j];for(int k=0;k<Inputs;++k)v+=n.w1[j*Inputs+k]*x[k];h[j]=std::tanh(v);}}
void Softmax(Output&z){float peak=*std::max_element(z.begin(),z.end());float sum=0;for(auto&v:z){v=std::exp(std::clamp(v-peak,-40.f,0.f));sum+=v;}for(auto&v:z)v/=sum;}
}
Output Network::Probabilities(const Features&x)const noexcept{std::array<float,Hidden>h{};Hiddens(*this,x,h);Output z=b2;for(int b=0;b<Bins;++b)for(int j=0;j<Hidden;++j)z[b]+=w2[b*Hidden+j]*h[j];Softmax(z);return z;}
void Network::Learn(const Row&row,float rate)noexcept{
 std::array<float,Hidden>h{};Hiddens(*this,row.x,h);Output p=b2;for(int b=0;b<Bins;++b)for(int j=0;j<Hidden;++j)p[b]+=w2[b*Hidden+j]*h[j];Softmax(p);
 // Event: cross-entropy on its bin. Censored: -log P(T beyond the last fully observed bin).
 Output e=p;
 if(row.remaining>=0)e[Bin(row.remaining)]-=1;
 else{const int k=Bin(row.observed)+1;if(k>=Bins)return;float tail=0;for(int b=k;b<Bins;++b)tail+=p[b];if(tail<1e-6f)tail=1e-6f;for(int b=k;b<Bins;++b)e[b]-=p[b]/tail;}
 std::array<float,Hidden>g{};for(int b=0;b<Bins;++b)for(int j=0;j<Hidden;++j)g[j]+=e[b]*w2[b*Hidden+j];
 for(int b=0;b<Bins;++b){for(int j=0;j<Hidden;++j)w2[b*Hidden+j]-=rate*(e[b]*h[j]+1e-5f*w2[b*Hidden+j]);b2[b]-=rate*e[b];}
 for(int j=0;j<Hidden;++j){const float gj=g[j]*(1-h[j]*h[j]);b1[j]-=rate*gj;for(int k=0;k<Inputs;++k)w1[j*Inputs+k]-=rate*(gj*row.x[k]+1e-5f*w1[j*Inputs+k]);}
}
bool Distribution(const Model&m,const Features&x,Output&out)noexcept{
 if(!m.trained||!Finite(x,6))return false;
 for(int k=0;k<Inputs;++k)if(Continuous[k]&&(x[k]<m.low[k]-.15f||x[k]>m.high[k]+.15f))return false;
 out=m.net.Probabilities(x);return Finite(out,2);
}
namespace {
float Quantile(const Output&p,float q){float cdf=0;for(int b=0;b<Bins;++b){const float next=cdf+p[b];if(next>=q){if(b==Bins-1)return std::numeric_limits<float>::infinity();const float fraction=p[b]>0?(q-cdf)/p[b]:0;return (float(b)+std::clamp(fraction,0.f,1.f))*Window;}cdf=next;}return std::numeric_limits<float>::infinity();}
}
float Remaining(const Model&m,const Features&x,float q)noexcept{Output p;if(!(q>0&&q<1)||!Distribution(m,x,p))return std::numeric_limits<float>::quiet_NaN();return Quantile(p,q);}
float TimeToReversal(const Model&m,const LegTracker&t,const Context&c,double now,float q)noexcept{Features x;if(!m.trained||!MakeFeatures(t,c,now,0,x))return std::numeric_limits<float>::quiet_NaN();return Remaining(m,x,q);}
Plan MakePlan(const Model&m,const LegTracker&t,const Context&c,double now,float q)noexcept{
 Plan p;if(!m.trained||!m.accepted){p.reason="strafe timing model not trained or not better than its baseline";return p;}
 if(!t.Strafing()){p.reason="no confirmed strafe pattern";return p;}
 const float first=TimeToReversal(m,t,c,now,q);if(std::isnan(first)){p.reason="strafe state outside training coverage";return p;}
 p.first=std::isinf(first)?1e9f:std::max(0.f,first);
 p.leg=t.legCount>=2?t.MeanLeg(4):c.playerMean>0?c.playerMean:m.fallbackLeg;p.leg=std::clamp(p.leg,.08f,MaxLeg);
 p.valid=true;p.reason=std::isinf(first)?"learned strafe timing: holds current direction":"learned strafe timing";return p;
}

FitResult Fit(const std::function<void(const std::function<void(const Row&)>&)>&rows,const Policy&policy,const std::function<bool()>&cancel){
 FitResult r;r.model.low.fill(6);r.model.high.fill(-6);
 try{
 storage::Scratch scratch;storage::Sort<uint64_t>groupSort(scratch,cancel);std::map<uint64_t,uint64_t>perPlayer;uint64_t eligible=0;
 auto wanted=[&](const Row&row){return policy.includeBots||row.player!=0;};
 rows([&](const Row&row){if(!wanted(row))return;groupSort.Add(row.group);++perPlayer[row.player];++eligible;});
 storage::Groups groups(scratch,groupSort);
 if(groups.size()<6||eligible<768){r.evaluation="Strafe timing needs 768 leg landmarks across 6 time groups (bots excluded unless included)";return r;}
 const uint64_t v=groups[groups.size()*6/10],t=groups[groups.size()*8/10];
 // One-group embargo between splits: leg history features reach a few seconds back.
 const uint64_t vb=groups.Upper(v)<groups.size()?groups[groups.Upper(v)]:UINT64_MAX,tb=groups.Upper(t)<groups.size()?groups[groups.Upper(t)]:UINT64_MAX;
 // Balance: no single player supplies more than playerShare of the corpus.
 const double cap=std::max(256.,double(eligible)*std::max(policy.playerShare,2./double(std::max<size_t>(perPlayer.size(),1))));
 struct Keyed{uint64_t key;Row row;};struct Less{bool operator()(const Keyed&a,const Keyed&b)const{return a.key<b.key;}};
 storage::Sort<Keyed,Less>shuffled(scratch,cancel);storage::Array<Row>val(scratch,cancel),test(scratch,cancel);uint64_t trainN=0,capped=0;double legSum=0;uint64_t legN=0;
 // Elapsed-only baseline: empirical remaining-time histogram per landmark-elapsed bin (events only).
 std::array<Output,Bins> histogram{};std::array<double,Bins> histogramN{};
 rows([&](const Row&row){if(!wanted(row))return;const auto count=perPlayer[row.player];if(double(count)>cap&&double(Mix(row.id)%1000000)>=cap/double(count)*1e6){++capped;return;}
  if(row.group<v){shuffled.Add({Mix(row.id^0x5f3a),row});++trainN;for(int k=0;k<Inputs;++k){r.model.low[k]=std::min(r.model.low[k],row.x[k]);r.model.high[k]=std::max(r.model.high[k],row.x[k]);}
   if(row.remaining>=0){const int e=Bin(row.x[0]);histogram[e][Bin(row.remaining)]+=1;histogramN[e]+=1;}if(row.x[2]>.5f){legSum+=row.x[1];++legN;}}
  else if(row.group>=vb&&row.group<t)val.push_back(row);else if(row.group>=tb)test.push_back(row);});
 val.Seal();test.Seal();
 if(trainN<512||val.size()<128||test.size()<128){r.evaluation="Strafe timing needs 512 training + 128 validation + 128 test landmarks after the time split";return r;}
 for(int e=0;e<Bins;++e)for(int b=0;b<Bins;++b)histogram[e][b]=float((histogram[e][b]+.5)/(histogramN[e]+.5*Bins));
 storage::Array<Row>train(scratch,cancel);shuffled.Each([&](const Keyed&k){train.push_back(k.row);});train.Seal();
 if(legN)r.model.fallbackLeg=std::clamp(float(legSum/double(legN)),.08f,MaxLeg);
 auto rowLoss=[](const Output&p,const Row&row){if(row.remaining>=0)return -std::log(std::max(p[Bin(row.remaining)],1e-6f));const int k=Bin(row.observed)+1;if(k>=Bins)return 0.f;float tail=0;for(int b=k;b<Bins;++b)tail+=p[b];return -std::log(std::max(tail,1e-6f));};
 Network current,best;double bestLoss=std::numeric_limits<double>::infinity();
 for(int epoch=0;epoch<24;++epoch){
  if(cancel&&cancel()){r.evaluation="Strafe timing fit cancelled";return r;}
  // Globally shuffled corpus plus a per-epoch shuffle window: no chronological drift.
  std::mt19937 rng(9137u+unsigned(epoch));std::vector<Row>window;window.reserve(512);const float rate=.01f/(1+epoch*.08f);
  auto drain=[&](size_t keep){while(window.size()>keep){std::uniform_int_distribution<size_t>pick(0,window.size()-1);auto i=pick(rng);std::swap(window[i],window.back());current.Learn(window.back(),rate);window.pop_back();}};
  for(const auto&row:train){window.push_back(row);if(window.size()==512)drain(256);}drain(0);
  double l=0;uint64_t n=0;for(const auto&row:val){l+=rowLoss(current.Probabilities(row.x),row);++n;}l/=double(std::max<uint64_t>(n,1));if(l<bestLoss){bestLoss=l;best=current;}
 }
 r.model.net=best;r.model.trained=true;r.model.rows=trainN;r.model.revision=Stamp();
 if(!Finite(best.w1,100)||!Finite(best.w2,100)||!Finite(best.b1,100)||!Finite(best.b2,100)){r.evaluation="Strafe timing fit produced invalid weights";r.model.trained=false;return r;}
 struct Score{double model=0,base=0,mae=0,baseMae=0;uint64_t n=0,timed=0,refused=0;};
 auto score=[&](const auto&set){Score s;for(const auto&row:set){Output p;if(!Distribution(r.model,row.x,p)){++s.refused;continue;}
   s.model+=rowLoss(p,row);s.base+=rowLoss(histogram[Bin(row.x[0])],row);++s.n;
   if(row.remaining>=0&&row.remaining<=MaxLeg){float predicted=Quantile(p,.5f);if(std::isinf(predicted))predicted=float(Bins-1)*Window;
    const float mean=row.x[5]>0?row.x[3]:row.x[22]>0?row.x[22]:r.model.fallbackLeg;const float baseline=std::max(0.f,mean-row.x[0]);
    s.mae+=std::abs(predicted-row.remaining);s.baseMae+=std::abs(baseline-row.remaining);++s.timed;}}
  return s;};
 const auto vs=score(val),ts=score(test);
 auto good=[](const Score&s){return s.timed>=64&&s.mae<=.97*s.baseMae&&s.model<s.base;};
 r.model.accepted=good(vs)&&good(ts);
 r.model.testMae=ts.timed?ts.mae/double(ts.timed):0;r.model.baselineMae=ts.timed?ts.baseMae/double(ts.timed):0;
 std::ostringstream o;o.imbue(std::locale::classic());o<<std::fixed<<std::setprecision(4)
  <<"Strafe timing "<<(r.model.accepted?"ACCEPTED":"rejected")<<" (movesim uses it only when accepted). eligible="<<eligible<<" capped="<<capped<<" train="<<trainN<<" validation="<<val.size()<<" test="<<test.size()<<" bots="<<(policy.includeBots?"included":"excluded")<<'\n';
 for(auto [name,s]:{std::pair<const char*,Score>{"validation",vs},{"test",ts}})o<<name<<": logloss="<<s.model/double(std::max<uint64_t>(s.n,1))<<" elapsed_only_logloss="<<s.base/double(std::max<uint64_t>(s.n,1))<<" outside_coverage="<<s.refused
  <<" timed="<<s.timed<<" median_timing_MAE="<<s.mae/double(std::max<uint64_t>(s.timed,1))<<"s mean_of_recent_legs_MAE="<<s.baseMae/double(std::max<uint64_t>(s.timed,1))<<"s criterion="<<(good(s)?"pass":"fail (needs >=64 timed landmarks, >=3% lower MAE and lower log-loss)")<<'\n';
 o<<"Timing is compared with a mean-of-recent-legs rule. Movesim's current corridor-centre convergence predicts no reversal time, so the hit-rate comparison needs live play.\n";
 r.evaluation=o.str();r.fitted=true;return r;
 }catch(const std::exception&e){r.evaluation=e.what();r.fitted=false;r.model.trained=false;return r;}
}

void SaveModel(const std::filesystem::path&path,const Model&m){
 if(!m.trained||!m.revision||m.rows<512)throw std::runtime_error("Refusing untrained strafe timing model");
 std::ostringstream f;f.imbue(std::locale::classic());f<<std::setprecision(9)<<"MOVEMENT_STRAFE_MODEL 1 28 12 17\n"<<m.revision<<' '<<m.rows<<' '<<m.accepted<<' '<<m.testMae<<' '<<m.baselineMae<<' '<<m.fallbackLeg<<'\n';
 auto write=[&](const auto&a){for(auto v:a)f<<v<<' ';f<<'\n';};write(m.low);write(m.high);write(m.net.w1);write(m.net.b1);write(m.net.w2);write(m.net.b2);
 metadata::Replace(path,f.str(),1024*1024);
}
Model LoadModel(const std::filesystem::path&path){
 std::istringstream f(metadata::Read(path,1024*1024));f.imbue(std::locale::classic());std::string header;std::getline(f,header);if(!header.empty()&&header.back()=='\r')header.pop_back();
 if(header!="MOVEMENT_STRAFE_MODEL 1 28 12 17")throw std::runtime_error("Unsupported strafe timing model");Model m;int accepted=0;
 if(!(f>>m.revision>>m.rows>>accepted>>m.testMae>>m.baselineMae>>m.fallbackLeg)||!m.revision||m.rows<512||accepted<0||accepted>1||!std::isfinite(m.fallbackLeg))throw std::runtime_error("Invalid strafe timing provenance");
 auto read=[&](auto&a){for(auto&v:a)if(!(f>>v))throw std::runtime_error("Truncated strafe timing model");};read(m.low);read(m.high);read(m.net.w1);read(m.net.b1);read(m.net.w2);read(m.net.b2);f>>std::ws;
 if(!f.eof()||!Finite(m.low,6)||!Finite(m.high,6)||!Finite(m.net.w1,100)||!Finite(m.net.b1,100)||!Finite(m.net.w2,100)||!Finite(m.net.b2,100))throw std::runtime_error("Invalid strafe timing weights");
 m.accepted=accepted!=0;m.trained=true;return m;
}
void SaveStats(const std::filesystem::path&path,const Stats&s){
 std::ostringstream f;f.imbue(std::locale::classic());f<<std::setprecision(12)<<"MOVEMENT_STRAFE_PLAYERS 1\n"<<s.global.n<<' '<<s.global.sum<<' '<<s.global.square<<'\n';
 for(const auto&[id,p]:s.players)f<<id<<' '<<p.n<<' '<<p.sum<<' '<<p.square<<'\n';metadata::Replace(path,f.str(),8*1024*1024);
}
Stats LoadStats(const std::filesystem::path&path){
 std::istringstream f(metadata::Read(path,8*1024*1024));f.imbue(std::locale::classic());std::string header;std::getline(f,header);if(!header.empty()&&header.back()=='\r')header.pop_back();
 if(header!="MOVEMENT_STRAFE_PLAYERS 1")throw std::runtime_error("Unsupported strafe player statistics");Stats s;
 if(!(f>>s.global.n>>s.global.sum>>s.global.square))throw std::runtime_error("Invalid strafe statistics");uint64_t id;
 while(f>>id){PlayerLegs p;if(!(f>>p.n>>p.sum>>p.square)||!id||id>UINT32_MAX||s.players.contains(id)||s.players.size()>=65536)throw std::runtime_error("Invalid strafe player statistics");s.players[id]=p;}
 if(!f.eof())throw std::runtime_error("Truncated strafe player statistics");return s;
}
}
