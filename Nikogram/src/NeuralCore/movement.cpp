#include "movement.h"
#include "storage.h"
#include "metadata.h"
#include "neural_input.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <variant>

namespace movement {
namespace {
template<class T> bool Finite(const T& a,float limit=1e7f) {for(auto x:a)if(!std::isfinite(x)||std::abs(x)>limit)return false;return true;}
Vec Sub(Vec a,const Vec& b){for(int k=0;k<3;++k)a[k]-=b[k];return a;}
float Length(const Vec& a){return std::sqrt(std::inner_product(a.begin(),a.end(),a.begin(),0.f));}
int Cell(const Features& x){return (x[11]>.5f?0:4)+(x[12]<.225f?0:x[12]<.4f?1:x[12]<.625f?2:3);}
bool Valid(const Observation& o){return o.track&&o.session&&std::isfinite(o.time)&&o.time>=0&&Finite(o.position)&&Finite(o.velocity,3000);}
bool Valid(const Example& e){return e.group&&Finite(e.x,6)&&e.x[12]>=.08f&&e.x[12]<=1.f&&e.x[9]>=.05f&&e.x[10]>=.05f&&e.x[9]<=2&&e.x[10]<=2&&Finite(e.actual,4096)&&Finite(e.baseline,4096)&&(e.x[11]==0||e.x[11]==1);}
Vec Absolute(const Network& n,const Features& x){auto r=n.Predict(x);auto b=Linear(x);for(int k=0;k<3;++k)r[k]=b[k]+ResidualScale*r[k];return r;}
uint64_t Stamp(){return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
void Replace(const std::filesystem::path& path,const std::string& text){
    auto tmp=path;tmp+=".tmp";auto old=layout::Previous(path);std::filesystem::create_directories(old.parent_path());recovery::RetainPrevious(path,old);
    {std::ofstream f(tmp,std::ios::binary|std::ios::trunc);if(!f||!(f<<text)||!f.flush())throw std::runtime_error("Cannot write temporary file");}
    std::error_code ec;
    if(std::filesystem::exists(path)){std::filesystem::remove(old,ec);ec.clear();std::filesystem::rename(path,old,ec);if(ec)throw std::runtime_error("Cannot preserve previous file");}
    std::filesystem::rename(tmp,path,ec);
    if(ec){if(std::filesystem::exists(old)){std::error_code ignored;std::filesystem::rename(old,path,ignored);}throw std::runtime_error("Cannot commit file; previous file retained");}
}
template<class Fn> void Rows(const std::filesystem::path& path,Fn fn,const std::function<bool()>& cancel={}){
    if(!std::filesystem::exists(path))return;
    neuralio::Input f(path,archive::DefaultLimit,cancel);std::string line;
    std::array<char,64> header{};if(!f.getline(header.data(),std::streamsize(header.size())) )throw std::runtime_error("Incompatible dataset header");line=header.data();if(!line.empty()&&line.back()=='\r')line.pop_back();if(line!="MOVEMENT_DATA 1 13 3")throw std::runtime_error("Incompatible dataset header");
    size_t n=0;
    std::array<char,2049> buffer{};
    while(f.getline(buffer.data(),std::streamsize(buffer.size()))){
        line=buffer.data();
        storage::Check(cancel);
        Example e;if(line.size()>2048||!Decode(line,e))throw std::runtime_error("Corrupt dataset row; file preserved");
        if(!fn(e,n++))break;
    }
    if(f.bad()||(!f.eof()&&f.fail()))throw std::runtime_error("Dataset read failed or row exceeds 2048 bytes");
}
struct Score {size_t n=0;double square=0,base=0,prior=0,mean=0,baseMean=0,worst=0,baseWorst=0;
    void Add(const Model& m,const Example& e,const Model* old){auto p=Absolute(m.net,e.x);double a=Length(Sub(p,e.actual)),b=Length(Sub(e.baseline,e.actual));++n;square+=a*a;base+=b*b;mean+=a;baseMean+=b;worst=std::max(worst,a);baseWorst=std::max(baseWorst,b);if(old){double q=Length(Sub(Absolute(old->net,e.x),e.actual));prior+=q*q;}}
    bool Good()const{return n>=8&&base>1e-6&&square<base*.9409&&mean<baseMean*.99&&worst<=baseWorst*1.10;}
};
}
void ValidateDataset(const std::filesystem::path&path,const std::function<bool()>&cancel){Rows(path,[](const Example&,size_t){return true;},cancel);}
bool MakeFeatures(const Anchor& a,float h,Features& x) noexcept {
    if(!a.valid||!std::isfinite(h)||h<.08f||h>MaxHorizon)return false;
    const auto& p=a.past;for(const auto& o:p)if(!Valid(o))return false;
    double dt1=p[2].time-p[1].time,dt0=p[1].time-p[0].time;
    if(dt0<.005||dt1<.005||dt0>.2||dt1>.2||p[0].track!=p[2].track||p[1].track!=p[2].track||p[0].session!=p[2].session||p[1].session!=p[2].session||p[0].player!=p[2].player||p[1].player!=p[2].player)return false;
    for(int k=0;k<3;++k){x[k]=p[2].velocity[k]/600.f;x[k+3]=float((p[2].position[k]-p[1].position[k])/dt1/600.);x[k+6]=float((p[1].position[k]-p[0].position[k])/dt0/600.);}
    x[9]=float(dt0/.1);x[10]=float(dt1/.1);x[11]=p[2].ground?1.f:0.f;x[12]=h;
    return Finite(x,6.f);
}
Vec Linear(const Features& x) noexcept {return {x[0]*600*x[12],x[1]*600*x[12],x[2]*600*x[12]};}
Network::Network(uint32_t seed){std::mt19937 rng(seed);std::uniform_real_distribution<float>d(-.08f,.08f);for(auto&v:w1)v=d(rng);for(auto&v:w2)v=d(rng);}
Vec Network::Predict(const Features& x)const noexcept{
    std::array<float,Hidden> h{};for(int j=0;j<Hidden;++j){float z=b1[j];for(int k=0;k<Inputs;++k)z+=w1[j*Inputs+k]*x[k];h[j]=std::tanh(z);}
    Vec y=b2;for(int k=0;k<Outputs;++k)for(int j=0;j<Hidden;++j)y[k]+=w2[k*Hidden+j]*h[j];return y;
}
void Network::Learn(const Example& e,float rate)noexcept{
    std::array<float,Hidden> h{},g{};for(int j=0;j<Hidden;++j){float z=b1[j];for(int k=0;k<Inputs;++k)z+=w1[j*Inputs+k]*e.x[k];h[j]=std::tanh(z);}
    Vec y=b2,err{};auto base=Linear(e.x);for(int k=0;k<3;++k){for(int j=0;j<Hidden;++j)y[k]+=w2[k*Hidden+j]*h[j];err[k]=std::clamp(y[k]-(e.actual[k]-base[k])/ResidualScale,-2.f,2.f);}
    for(int j=0;j<Hidden;++j){for(int k=0;k<3;++k)g[j]+=err[k]*w2[k*Hidden+j];g[j]*=1-h[j]*h[j];}
    for(int k=0;k<3;++k){for(int j=0;j<Hidden;++j)w2[k*Hidden+j]-=rate*(err[k]*h[j]+.0001f*w2[k*Hidden+j]);b2[k]-=rate*err[k];}
    for(int j=0;j<Hidden;++j){for(int k=0;k<Inputs;++k)w1[j*Inputs+k]-=rate*(g[j]*e.x[k]+.0001f*w1[j*Inputs+k]);b1[j]-=rate*g[j];}
}
Prediction Predict(const Pool& pool,uint64_t player,const Features& x,const Vec& baseline)noexcept{
    Prediction r;if(!Finite(x,6)||!Finite(baseline,4096)||x[12]<.08f||x[12]>1)return r;
    if(std::find(pool.disabled.begin(),pool.disabled.end(),player)!=pool.disabled.end()){r.reason="player disabled";return r;}
    for(auto id:{player,uint64_t(0)}){
        auto it=std::find_if(pool.models.begin(),pool.models.end(),[&](const Model& m){return m.player==id;});
        if(it==pool.models.end()||!it->promoted||!it->supported[Cell(x)]){r.reason="insufficient held-out evidence for horizon/mode";continue;}
        bool range=true;for(int k=0;k<Inputs;++k)if(x[k]<it->low[k]-.025f||x[k]>it->high[k]+.025f)range=false;
        if(!range){r.reason="outside trained movement range";continue;}
        auto value=Absolute(it->net,x);
        if(!Finite(value,4096)||Length(Sub(value,baseline))>32.f){r.reason="correction exceeds 32 units";continue;}
        r.displacement=value;r.valid=true;r.personal=id!=0;r.revision=it->revision;r.reason=r.personal?"personal model":"general model";return r;
    }return r;
}
Prediction PredictExperimental(const Pool& candidates,const Pool& accepted,uint64_t player,const Features& x)noexcept{
    Prediction r;r.reason="no applicable trained experimental model";
    Example domain;domain.group=1;domain.x=x;if(!Valid(domain))return r;
    if(std::find(accepted.disabled.begin(),accepted.disabled.end(),player)!=accepted.disabled.end()){r.reason="player disabled";return r;}
    for(bool candidate:{true,false})for(auto id:{player,uint64_t(0)}){
        const auto& pool=candidate?candidates:accepted;
        auto it=std::find_if(pool.models.begin(),pool.models.end(),[&](const Model&m){return m.player==id;});
        if(it==pool.models.end()||!it->trained||!it->revision||it->rows<MinTraining)continue;
        if(candidate?(it->promoted||it->fitCounts[Cell(x)]<8):(!it->promoted||!it->supported[Cell(x)]))continue;
        bool range=true;for(int k=0;k<Inputs;++k)if(x[k]<it->low[k]-.025f||x[k]>it->high[k]+.025f)range=false;
        if(!range){r.reason="outside trained experimental range";continue;}
        auto value=Absolute(it->net,x);
        if(!Finite(value,4096)||Length(value)>3000.f*x[12]+32.f){r.reason="experimental displacement exceeds physical magnitude bound";continue;}
        r.displacement=value;r.valid=true;r.personal=id!=0;r.revision=it->revision;
        r.reason=candidate?(r.personal?"experimental personal candidate (unvalidated)":"experimental general candidate (unvalidated)"):
            (r.personal?"experimental accepted-personal fallback (unvalidated use)":"experimental accepted-general fallback (unvalidated use)");return r;
    }return r;
}
std::string Encode(const Example& e){std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(9)<<e.player<<' '<<e.group<<' '<<int(e.paired);for(auto v:e.x)s<<' '<<v;for(auto v:e.actual)s<<' '<<v;for(auto v:e.baseline)s<<' '<<v;return s.str();}
static bool DecodeLegacy(const std::string& line,Example& e)noexcept{try{std::istringstream s(line);s.imbue(std::locale::classic());int b=-1;if(!(s>>e.player>>e.group>>b)||b<0||b>1)return false;e.paired=b!=0;for(auto&v:e.x)if(!(s>>v))return false;for(auto&v:e.actual)if(!(s>>v))return false;for(auto&v:e.baseline)if(!(s>>v))return false;s>>std::ws;return s.eof()&&Valid(e);}catch(...){return false;}}
// The stream fallback preserves legacy numeric spellings and edge cases.
bool Decode(const std::string& line,Example& e) noexcept {
    const char* p=line.data();const char* const end=p+line.size();
    auto space=[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\v'||c=='\f';};
    auto read=[&](auto& value){
        while(p!=end&&space(*p))++p;
        if(p==end)return false;
        auto result=std::from_chars(p,end,value);
        if(result.ec!=std::errc{} || (result.ptr!=end&&!space(*result.ptr)))return false;
        p=result.ptr;return true;
    };
    Example parsed;int paired=-1;
    if(!read(parsed.player)||!read(parsed.group)||!read(paired))return DecodeLegacy(line,e);
    if(paired<0||paired>1)return false;parsed.paired=paired!=0;
    for(auto&v:parsed.x)if(!read(v))return DecodeLegacy(line,e);
    for(auto&v:parsed.actual)if(!read(v))return DecodeLegacy(line,e);
    for(auto&v:parsed.baseline)if(!read(v))return DecodeLegacy(line,e);
    while(p!=end&&space(*p))++p;
    if(p!=end||!Valid(parsed))return false;
    e=parsed;return true;
}

void SavePool(const std::filesystem::path& path,const Pool& p){
    if(p.models.size()>MaxModels||p.disabled.size()>1024)throw std::runtime_error("Model count limit");
    for(auto id:p.disabled)if(!id)throw std::runtime_error("Invalid disabled player ID");
    std::set<uint64_t> identities;
    for(const auto&m:p.models){
        if(!m.promoted||!m.revision||m.rows<MinTraining||!identities.insert(m.player).second||!Finite(m.low,6)||!Finite(m.high,6)||!Finite(m.net.w1,100)||!Finite(m.net.b1,100)||!Finite(m.net.w2,100)||!Finite(m.net.b2,100)
            ||!std::isfinite(m.validationRMSE)||!std::isfinite(m.testRMSE)||!std::isfinite(m.baselineRMSE)||m.validationRMSE<0||m.testRMSE<0||m.testRMSE>=m.baselineRMSE||std::none_of(m.supported.begin(),m.supported.end(),[](bool b){return b;}))throw std::runtime_error("Refusing invalid model publication");
        for(int k=0;k<Inputs;++k)if(m.low[k]>m.high[k])throw std::runtime_error("Invalid feature bounds");
    }
    std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(17)<<"MOVEMENT_MODELS 1 13 24 3\n"<<p.models.size()<<' '<<p.disabled.size()<<'\n';
    for(auto id:p.disabled)s<<id<<' ';s<<'\n';
    for(const auto&m:p.models){s<<m.player<<' '<<m.revision<<' '<<m.rows<<' '<<m.evaluatedThrough<<' '<<m.promoted<<' '<<m.validationRMSE<<' '<<m.testRMSE<<' '<<m.baselineRMSE<<'\n';for(auto b:m.supported)s<<b<<' ';s<<'\n';
        auto write=[&](const auto&a){for(auto v:a)s<<v<<' ';s<<'\n';};write(m.low);write(m.high);write(m.net.w1);write(m.net.b1);write(m.net.w2);write(m.net.b2);}
    metadata::Replace(path,s.str(),4*1024*1024);
}
Pool LoadPool(const std::filesystem::path& path){
    std::istringstream s(metadata::Read(path,4*1024*1024));s.imbue(std::locale::classic());std::string header;std::getline(s,header);if(!header.empty()&&header.back()=='\r')header.pop_back();if(header!="MOVEMENT_MODELS 1 13 24 3")throw std::runtime_error("Incompatible model schema/architecture");
    size_t n=0,d=0;if(!(s>>n>>d)||n>MaxModels||d>1024)throw std::runtime_error("Model count invalid");Pool p;p.disabled.resize(d);for(auto&id:p.disabled)if(!(s>>id)||!id)throw std::runtime_error("Invalid player control");std::set<uint64_t> ids;
    for(size_t i=0;i<n;++i){Model m;int promoted;if(!(s>>m.player>>m.revision>>m.rows>>m.evaluatedThrough>>promoted>>m.validationRMSE>>m.testRMSE>>m.baselineRMSE)||promoted!=1||!m.revision||m.rows<MinTraining||!ids.insert(m.player).second)throw std::runtime_error("Invalid model metadata");m.promoted=true;
        if(!std::isfinite(m.validationRMSE)||!std::isfinite(m.testRMSE)||!std::isfinite(m.baselineRMSE)||m.validationRMSE<0||m.testRMSE<0||m.baselineRMSE<=0||m.testRMSE>=m.baselineRMSE)throw std::runtime_error("Invalid model evaluation");
        for(auto& b:m.supported){int v;if(!(s>>v)||v<0||v>1)throw std::runtime_error("Invalid support mask");b=v!=0;}if(std::none_of(m.supported.begin(),m.supported.end(),[](bool b){return b;}))throw std::runtime_error("No validated support");
        auto read=[&](auto&a,float limit){for(auto&v:a)if(!(s>>v)||!std::isfinite(v)||std::abs(v)>limit)throw std::runtime_error("Invalid or nonfinite model value");};read(m.low,6);read(m.high,6);for(int k=0;k<Inputs;++k)if(m.low[k]>m.high[k])throw std::runtime_error("Invalid feature bounds");read(m.net.w1,100);read(m.net.b1,100);read(m.net.w2,100);read(m.net.b2,100);p.models.push_back(m);}
    s>>std::ws;if(!s.eof())throw std::runtime_error("Unexpected model content");for(auto& m:p.models)m.trained=true;return p;
}
namespace {
bool CandidateValid(const Model&m){
    if(!m.trained||m.promoted||!m.revision||m.rows<MinTraining||!Finite(m.low,6)||!Finite(m.high,6)||!Finite(m.net.w1,100)||!Finite(m.net.b1,100)||!Finite(m.net.w2,100)||!Finite(m.net.b2,100))return false;
    uint64_t count=0;for(auto n:m.fitCounts){if(n>m.rows)return false;count+=n;}if(count!=m.rows)return false;
    for(int k=0;k<Inputs;++k)if(m.low[k]>m.high[k])return false;
    return true;
}
void ExplainBlocked(TrainingResult& r){if(!r.diagnostic.empty())return;std::ostringstream s;s<<r.reason<<'\n';for(int c=0;c<8;++c)s<<(c<4?"ground":"air")<<" horizon_band="<<c%4<<" paired_val=not_evaluated paired_test=not_evaluated criterion="<<r.reason<<'\n';r.diagnostic=s.str();}
std::string Criteria(const Score&s){std::string r;auto add=[&](const char* v){if(!r.empty())r+=',';r+=v;};if(s.n<8)add("paired_count<8");if(s.base<=1e-6)add("native_error_zero");if(!(s.square<s.base*.9409))add("rmse_gain<3pct");if(!(s.mean<s.baseMean*.99))add("mean_gain<1pct");if(!(s.worst<=s.baseWorst*1.10))add("worst_error>110pct_native");return r.empty()?"pass":r;}
}
void SaveCandidates(const std::filesystem::path& path,const Pool& p){
    if(p.models.size()>MaxModels||!p.disabled.empty())throw std::runtime_error("Invalid experimental candidate pool");
    std::set<uint64_t> ids;for(const auto&m:p.models)if(!CandidateValid(m)||!ids.insert(m.player).second)throw std::runtime_error("Refusing untrained/invalid experimental candidate");
    std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(17)<<"MOVEMENT_CANDIDATES 1 13 24 3 TRAINED_ONLY\n"<<p.models.size()<<'\n';
    for(const auto&m:p.models){s<<m.player<<' '<<m.revision<<' '<<m.rows<<" 1\n";auto write=[&](const auto&a){for(auto v:a)s<<v<<' ';s<<'\n';};write(m.fitCounts);write(m.low);write(m.high);write(m.net.w1);write(m.net.b1);write(m.net.w2);write(m.net.b2);}metadata::Replace(path,s.str(),4*1024*1024);
}
Pool LoadCandidates(const std::filesystem::path& path){
    std::istringstream s(metadata::Read(path,4*1024*1024));s.imbue(std::locale::classic());std::string header;std::getline(s,header);if(!header.empty()&&header.back()=='\r')header.pop_back();if(header!="MOVEMENT_CANDIDATES 1 13 24 3 TRAINED_ONLY")throw std::runtime_error("Incompatible experimental candidate schema");
    size_t count=0;if(!(s>>count)||count>MaxModels)throw std::runtime_error("Experimental candidate count invalid");Pool p;std::set<uint64_t> ids;
    for(size_t i=0;i<count;++i){Model m;int trained=0;if(!(s>>m.player>>m.revision>>m.rows>>trained)||trained!=1||!ids.insert(m.player).second)throw std::runtime_error("Invalid trained candidate provenance");m.trained=true;
        auto read=[&](auto&a){for(auto&v:a)if(!(s>>v))throw std::runtime_error("Invalid experimental candidate value");};read(m.fitCounts);read(m.low);read(m.high);read(m.net.w1);read(m.net.b1);read(m.net.w2);read(m.net.b2);if(!CandidateValid(m))throw std::runtime_error("Invalid experimental candidate bounds");p.models.push_back(m);}
    s>>std::ws;if(!s.eof())throw std::runtime_error("Unexpected experimental candidate content");return p;
}
namespace {
uint64_t MixKey(uint64_t x){x+=0x9e3779b97f4a7c15ull;x=(x^(x>>30))*0xbf58476d1ce4e5b9ull;x=(x^(x>>27))*0x94d049bb133111ebull;return x^(x>>31);}
// Stable per-example key (content hash): balancing decisions agree across every scan.
uint64_t ExampleKey(const Example&e){uint64_t h=1469598103934665603ull;auto add=[&](const void*p,size_t n){auto b=static_cast<const unsigned char*>(p);for(size_t i=0;i<n;++i){h^=b[i];h*=1099511628211ull;}};add(&e.player,sizeof e.player);add(&e.group,sizeof e.group);add(e.x.data(),sizeof e.x);add(e.actual.data(),sizeof e.actual);return MixKey(h);}
}
static TrainingResult Fit(const std::filesystem::path& path,uint64_t player,size_t selection,const std::function<bool()>& cancel,const Model* old,const Network* initial,uint64_t evaluationAfter,bool experimental,const TrainingPolicy& policy){
    TrainingResult out;out.model.player=player;out.model.low.fill(6);out.model.high.fill(-6);storage::Scratch scratch;storage::Sort<uint64_t> groupSort(scratch,cancel);size_t eligible=0;storage::Array<Example>all(scratch,cancel);
    // Bots (account 0) are kept out of the global model unless explicitly included.
    auto wanted=[&](const Example&e){return player?e.player==player:(policy.includeBots||e.player!=0);};
    // Shuffle once (external sort on a content hash): SGD never sees chronological order.
    struct Keyed{uint64_t key;Example e;};struct KeyLess{bool operator()(const Keyed&a,const Keyed&b)const{return a.key<b.key;}};storage::Sort<Keyed,KeyLess> shuffle(scratch,cancel);std::map<uint64_t,uint64_t> perPlayer;
    Rows(path,[&](const Example&e,size_t){if(wanted(e)){groupSort.Add(e.group);shuffle.Add({ExampleKey(e),e});++eligible;++perPlayer[e.player];}return true;},cancel);
    shuffle.Each([&](const Keyed&k){all.push_back(k.e);});
    all.Seal();auto scan=[&](auto fn){size_t n=0;for(const auto&e:all)if(!fn(e,n++))break;};
    // Balance (global model only): no single player supplies more than playerShare of the examples.
    // A player may supply up to max(playerShare, twice an even share): few-player datasets keep their data.
    const double cap=player?1e300:std::max(256.,double(eligible)*std::max(policy.playerShare,2./double(std::max<size_t>(perPlayer.size(),1))));uint64_t capped=0;
    auto balanced=[&](const Example&e){const double count=double(perPlayer[e.player]);return count<=cap||double(ExampleKey(e)%1000000)<cap/count*1e6;};
    auto cancelled=[&]{if(cancel&&cancel()){out.cancelled=true;out.trained=out.model.trained=out.model.promoted=false;out.diagnostic.clear();out.reason="Training cancelled; last good model preserved";return true;}return false;};
    if(cancelled())return out;
    storage::Groups order(scratch,groupSort);
    if(eligible<MinTraining+128||order.size()<6){out.reason="Need 640 examples across 6 independent time segments";return out;}
    uint64_t val=order[order.size()*6/10],test=order[order.size()*8/10];
    const uint64_t cutoff=experimental?0:std::max(evaluationAfter,old?old->evaluatedThrough:uint64_t(0));
    if(cutoff){auto first=order.Upper(cutoff);size_t fresh=size_t(order.size()-first);if(fresh<4){out.reason="Need 4 fresh time segments after last evaluation/import";return out;}val=order[first];test=order[first+fresh/2];}
    const size_t stride=selection&&eligible>selection?1+(eligible-1)/selection:1;
    auto take=[&](size_t n){return n%stride==0;};size_t trainN=0,valN=0,testN=0,ordinal=0;
    auto pick=[&](const Example&e){return take(ordinal++)&&balanced(e);};
    scan([&](const Example&e,size_t){if(!wanted(e))return true;bool selected=take(ordinal++);if(selected&&!balanced(e)){++capped;selected=false;}if(!selected)return true;if(e.group<val){++trainN;++out.model.fitCounts[Cell(e.x)];for(int k=0;k<Inputs;++k){out.model.low[k]=std::min(out.model.low[k],e.x[k]);out.model.high[k]=std::max(out.model.high[k],e.x[k]);}}else if(e.group<test)++valN;else ++testN;return true;});
    if(trainN<MinTraining||valN<64||testN<64){out.reason="Selection needs 512 training + 64 validation + 64 test examples";return out;}
    Network current=initial?*initial:old?old->net:Network{};double best=std::numeric_limits<double>::infinity();
    for(int epoch=0;epoch<24;++epoch){if(cancelled())return out;ordinal=0;
        // Per-epoch shuffle window on top of the global shuffle.
        std::mt19937 rng(4409u+unsigned(epoch));std::vector<Example> window;window.reserve(512);const float rate=.004f/(1+epoch*.08f);
        auto drain=[&](size_t keep){while(window.size()>keep){std::uniform_int_distribution<size_t>choose(0,window.size()-1);auto i=choose(rng);std::swap(window[i],window.back());current.Learn(window.back(),rate);window.pop_back();}};
        scan([&](const Example&e,size_t){if(wanted(e)&&pick(e)&&e.group<val){window.push_back(e);if(window.size()==512)drain(256);}return true;});drain(0);
        double error=0;size_t count=0;ordinal=0;
        scan([&](const Example&e,size_t){if(wanted(e)&&pick(e)&&e.group>=val&&e.group<test){auto v=Sub(Absolute(current,e.x),e.actual);error+=std::inner_product(v.begin(),v.end(),v.begin(),0.);++count;}return true;});
        if(count&&error/count<best){best=error/count;out.model.net=current;}
    }
    if(cancelled())return out;
    if(!std::isfinite(best)||!Finite(out.model.net.w1,100)||!Finite(out.model.net.b1,100)||!Finite(out.model.net.w2,100)||!Finite(out.model.net.b2,100)){out.reason="Fit rejected: nonfinite or out-of-bounds weights";return out;}
    std::array<Score,8> vs{},ts{};ordinal=0;
    scan([&](const Example&e,size_t){if(!wanted(e))return true;bool selected=pick(e);if(!selected||!e.paired)return true;if(e.group>=test)ts[Cell(e.x)].Add(out.model,e,old);else if(e.group>=val)vs[Cell(e.x)].Add(out.model,e,old);return true;});
    if(cancelled())return out;
    double sq=0,bs=0,vsq=0;size_t n=0,vn=0;
    std::ostringstream diagnostic;diagnostic.imbue(std::locale::classic());diagnostic<<std::fixed<<std::setprecision(3)<<"eligible_rows="<<eligible<<" selection="<<(selection?std::to_string(selection):"all")<<" stride="<<stride<<" fit_rows="<<trainN<<" validation_rows="<<valN<<" test_rows="<<testN<<" experimental="<<experimental<<" shuffled=yes balance_capped="<<capped<<" bots="<<(player?"n/a":policy.includeBots?"included":"excluded")<<'\n';
    for(int c=0;c<8;++c){bool ok=vs[c].Good()&&ts[c].Good();bool priorFailed=old&&old->supported[c]&&ts[c].square>ts[c].prior*.99;if(priorFailed)ok=false;out.model.supported[c]=ok;if(ok){sq+=ts[c].square;bs+=ts[c].base;n+=ts[c].n;vsq+=vs[c].square;vn+=vs[c].n;}
        diagnostic<<(c<4?"ground":"air")<<" horizon_band="<<c%4<<" fit="<<out.model.fitCounts[c]<<" paired_val="<<vs[c].n<<" paired_test="<<ts[c].n;
        for(auto scored:{std::make_pair("val",&vs[c]),std::make_pair("test",&ts[c])}){const auto&s=*scored.second;diagnostic<<' '<<scored.first<<"_rmse=";if(s.n)diagnostic<<std::sqrt(s.square/s.n);else diagnostic<<"n/a";diagnostic<<' '<<scored.first<<"_native_rmse=";if(s.n)diagnostic<<std::sqrt(s.base/s.n);else diagnostic<<"n/a";diagnostic<<' '<<scored.first<<"_criteria="<<Criteria(s);}
        diagnostic<<" incumbent="<<(priorFailed?"gain<1pct":"pass/not_applicable")<<" decision="<<(experimental?"unvalidated_only":ok?"accepted":"rejected")<<'\n';}
    out.model.rows=trainN;out.model.revision=Stamp();out.model.evaluatedThrough=order.back();out.model.promoted=n>0;
    if(n){out.model.testRMSE=std::sqrt(sq/n);out.model.baselineRMSE=std::sqrt(bs/n);out.model.validationRMSE=std::sqrt(vsq/vn);out.reason="Promoted after held-out ordinary-baseline evaluation";}
    else out.reason="Candidate rejected: insufficient paired baseline evidence or no held-out improvement";
    out.trained=out.model.trained=true;
    if(experimental){out.model.promoted=false;out.model.supported.fill(false);out.reason="Experimental candidate fitted (unvalidated); accepted models unchanged";}
    out.diagnostic=out.reason+'\n'+diagnostic.str();
    if(cancelled())return out;
    return out;
}
TrainingResult Train(const std::filesystem::path& path,uint64_t player,size_t selection,const std::function<bool()>& cancel,const Model* old,const Network* initial,uint64_t evaluationAfter,const TrainingPolicy& policy){try{auto r=Fit(path,player,selection,cancel,old,initial,evaluationAfter,false,policy);ExplainBlocked(r);return r;}catch(...){if(!(cancel&&cancel()))throw;TrainingResult r;r.cancelled=true;r.reason="Training cancelled; previous model retained";ExplainBlocked(r);return r;}}
TrainingResult TrainExperimental(const std::filesystem::path& path,uint64_t player,size_t selection,const std::function<bool()>& cancel,const Network* initial,const TrainingPolicy& policy){try{auto r=Fit(path,player,selection,cancel,nullptr,initial,0,true,policy);ExplainBlocked(r);return r;}catch(...){if(!(cancel&&cancel()))throw;TrainingResult r;r.cancelled=true;r.reason="Training cancelled; previous model retained";ExplainBlocked(r);return r;}}

struct Service::Impl {
    struct Command {Action action;uint64_t player,ticket;};using Event=std::variant<Observation,Baseline,Command>;
    struct Queued {Event event;uint64_t epoch;};
    std::filesystem::path root,data,models;std::mutex mutex,dataMutex;std::condition_variable wake;std::deque<Queued> queue;
    std::atomic<bool> stop=false;std::atomic<uint64_t> cancelGeneration=0,collectionGeneration=0;uint64_t workerGeneration=0;std::atomic<std::shared_ptr<const Pool>> published,candidates;std::thread worker,trainer;Options options;Status status;
    std::atomic<uint64_t> dropped=0;
    struct Track {std::deque<Observation> history;double lastAnchor=-1;uint64_t group=0;long long block=-1;};
    struct Pending {Anchor anchor;uint64_t group=0;unsigned done=0;Baseline baseline;};
    std::map<std::pair<uint64_t,long long>,uint64_t> timeGroups;
    std::map<uint64_t,Track> tracks;std::deque<Pending> pending;uint64_t groupSerial=Stamp(),sinceTrain=0;bool writable=true;std::unique_ptr<appendlog::Writer>appendWriter;
    std::map<uint64_t,uint64_t> personRows,personTrained;uint64_t lastTrained=0;
    struct FitJob{uint64_t player,ticket;bool experimental;Options options;std::shared_ptr<storage::Snapshot>snapshot;};
    std::mutex fitMutex,fitRunMutex;std::condition_variable fitWake;std::deque<FitJob>fitQueue;
    Impl(std::filesystem::path r):root(std::move(r)),data(root/"observations.txt"),models(root/"models.txt") {published.store(std::make_shared<const Pool>());candidates.store(std::make_shared<const Pool>());worker=std::thread([this]{Run();});trainer=std::thread([this]{FitRun();});}
    template<class F>void ReadRows(const std::filesystem::path&path,F fn,const storage::Cancel&cancel={}){Rows(path,fn,[&]{return stop||(cancel&&cancel());});}
    void State(std::string s,std::string error={}){std::lock_guard lock(mutex);status.state=std::move(s);status.error=std::move(error);}
    Options Opt(){std::lock_guard lock(mutex);return options;}
    bool Push(Event e)noexcept{try{std::unique_lock lock(mutex,std::try_to_lock);if(!lock||stop||queue.size()>=QueueLimit){++dropped;return false;}if(!std::holds_alternative<Command>(e)&&!options.collect)return false;queue.push_back({std::move(e),collectionGeneration.load()});wake.notify_one();return true;}catch(...){++dropped;return false;}}
    void Publish(Pool pool){SavePool(models,pool);published.store(std::make_shared<const Pool>(std::move(pool)));std::lock_guard lock(mutex);status.models=published.load()->models.size();}
    void PublishCandidates(Pool pool){SaveCandidates(root/"experimental-candidates.txt",pool);candidates.store(std::make_shared<const Pool>(std::move(pool)));std::lock_guard lock(mutex);status.candidates=candidates.load()->models.size();}
    void Evaluation(const TrainingResult& result){auto text=result.diagnostic.substr(0,16384);metadata::Replace(root/"last-evaluation.txt",text,16384);std::lock_guard lock(mutex);status.evaluation=std::move(text);}
    void RetainCandidate(const TrainingResult& result){
        if(!result.trained||result.cancelled)return;auto pool=*candidates.load();auto m=result.model;m.promoted=false;m.supported.fill(false);
        auto it=std::find_if(pool.models.begin(),pool.models.end(),[&](const Model&v){return v.player==m.player;});
        if(it!=pool.models.end())*it=std::move(m);else if(pool.models.size()<MaxModels)pool.models.push_back(std::move(m));else throw std::runtime_error("Experimental candidate capacity reached; old candidates retained");PublishCandidates(std::move(pool));
    }
    void FlushData(bool force){if(!appendWriter)return;try{if(force)appendWriter->Flush();else appendWriter->MaybeFlush();}catch(...){writable=false;throw;}}
    appendlog::Writer& Writer(){if(!appendWriter){if(!appendlog::IsAppend(data))neuralio::EnsureAppend(data);appendWriter=std::make_unique<appendlog::Writer>(data);}return *appendWriter;}
    void Append(const Example&e){
        if(!writable)return;try{auto&out=Writer();const std::string prefix=!out.LogicalBytes()?"MOVEMENT_DATA 1 13 3\n":out.LastByte()!='\n'?"\n":"";out.Append(prefix+Encode(e)+"\n");
        if(personRows.size()>=1024&&!personRows.contains(e.player)){personTrained.erase(personRows.begin()->first);personRows.erase(personRows.begin());}++personRows[e.player];++sinceTrain;std::lock_guard lock(mutex);++status.examples;if(e.paired)++status.paired;status.diskBytes=std::filesystem::file_size(data);
        }catch(...){writable=false;throw;}
    }
    void Observe(const Observation&o){
        if(!Opt().collect||!Valid(o))return;auto publishedNow=published.load();if(o.player&&std::find(publishedNow->disabled.begin(),publishedNow->disabled.end(),o.player)!=publishedNow->disabled.end())return;{std::lock_guard lock(mutex);++status.observations;}
        if(tracks.size()>=128&&!tracks.contains(o.track)){tracks.clear();pending.clear();}
        auto&t=tracks[o.track];bool discontinuity=false;
        if(!t.history.empty()){const auto& last=t.history.back();double dt=o.time-last.time;
            if(last.session==o.session&&dt==0)return;
            discontinuity=last.session!=o.session||dt<.005||dt>.2||Length(Sub(o.position,last.position))>float(dt*3000+32)||last.player!=o.player;
            if(discontinuity){t={};std::erase_if(pending,[&](const Pending&p){return p.anchor.past[2].track==o.track;});}}
        constexpr float horizons[]={.15f,.3f,.5f,.75f};
        for(auto& p:pending){const auto&a=p.anchor.past[2];if(a.track!=o.track||a.session!=o.session)continue;float h=float(o.time-a.time);
            for(unsigned b=0;b<4;++b){if((p.done&(1u<<b))||h<horizons[b])continue;p.done|=1u<<b;
                // Label is this actual received observation, never a predicted or render-interpolated position.
                if(h>horizons[b]+.06f||h>1||static_cast<long long>(o.time/8)!=static_cast<long long>(a.time/8))continue;
                Example e;e.player=a.player;e.group=p.group;if(!MakeFeatures(p.anchor,h,e.x))continue;e.actual=Sub(o.position,a.position);e.baseline=Linear(e.x);
                for(size_t j=1;j<p.baseline.count;++j){const auto&left=p.baseline.points[j-1];const auto&right=p.baseline.points[j];if(left.time<=o.time&&right.time>=o.time&&right.time>left.time&&right.time-left.time<.05){float u=float((o.time-left.time)/(right.time-left.time));for(int k=0;k<3;++k)e.baseline[k]=left.position[k]+u*(right.position[k]-left.position[k])-a.position[k];e.paired=true;break;}}
                if(Valid(e))Append(e);
            }
        }
        std::erase_if(pending,[&](const Pending&p){return p.done==15||(p.anchor.past[2].session==o.session&&o.time-p.anchor.past[2].time>1.2);});
        t.history.push_back(o);while(t.history.size()>3)t.history.pop_front();
        if(t.history.size()==3&&(t.lastAnchor<0||o.time-t.lastAnchor>=.1)){
            Anchor a;std::copy(t.history.begin(),t.history.end(),a.past.begin());a.valid=true;Features x;if(MakeFeatures(a,.3f,x)){
                auto block=static_cast<long long>(o.time/8);if(static_cast<long long>(a.past[0].time/8)!=block)return;
                if(t.block!=block){t.block=block;auto key=std::make_pair(o.session,block);auto [it,inserted]=timeGroups.emplace(key,groupSerial+1);if(inserted)++groupSerial;t.group=it->second;if(timeGroups.size()>256)timeGroups.erase(timeGroups.begin());}
                if(pending.size()>=2048)pending.pop_front();pending.push_back({a,t.group});t.lastAnchor=o.time;
            }
        }
    }
    void BaselineEvent(const Baseline& b){if(!Opt().collect||!b.anchor.valid||b.count<2||b.count>b.points.size())return;for(size_t i=0;i<b.count;++i)if(!Finite(b.points[i].position)||!std::isfinite(b.points[i].time)||(i&&b.points[i].time<=b.points[i-1].time))return;
        for(auto it=pending.rbegin();it!=pending.rend();++it){const auto&a=it->anchor.past[2];const auto&z=b.anchor.past[2];if(a.track==z.track&&a.session==z.session&&a.player==z.player&&std::abs(a.time-z.time)<.0001&&it->anchor.past==b.anchor.past){if(b.count>it->baseline.count)it->baseline=b;break;}}
    }
    void Training(uint64_t player,uint64_t ticket,bool experimental=false){
        if(!writable||ticket!=cancelGeneration)return;if(!std::filesystem::exists(data)){State("Waiting for observations; training starts automatically");return;}FlushData(true);FitJob job{player,ticket,experimental,Opt(),std::make_shared<storage::Snapshot>(data)};
        {std::lock_guard lock(fitMutex);for(auto&q:fitQueue)if(q.player==player&&q.experimental==experimental){q=std::move(job);return;}if(fitQueue.size()>=2){State("Training queue full; collection continues");return;}fitQueue.push_back(std::move(job));}
        if(!experimental){if(player)personTrained[player]=personRows[player];else lastTrained=sinceTrain;}fitWake.notify_one();
    }
    void FitRun()noexcept{while(!stop){
      {std::unique_lock lock(fitMutex);fitWake.wait_for(lock,std::chrono::milliseconds(100),[&]{return stop||!fitQueue.empty();});if(stop)break;if(fitQueue.empty())continue;}
      std::lock_guard run(fitRunMutex);FitJob job;{std::lock_guard lock(fitMutex);if(stop||fitQueue.empty())continue;job=std::move(fitQueue.front());fitQueue.pop_front();}
      if(job.ticket!=cancelGeneration)continue;try{ExecuteFit(job);}catch(const std::exception&e){std::lock_guard lock(mutex);status.training=false;status.state="Training failed/cancelled; previous models retained";status.error=e.what();}
    }}
    void ExecuteFit(FitJob&job){const auto player=job.player,ticket=job.ticket;const bool experimental=job.experimental;if(ticket!=cancelGeneration)return;{std::lock_guard lock(mutex);status.training=true;status.state=std::string(experimental?"Training experimental candidate":player?"Training personal model":"Training general model")+"; selection="+(job.options.all?"all":std::to_string(job.options.samples))+"; snapshot bytes="+std::to_string(job.snapshot->bytes);}
        auto pool=*published.load();auto old=std::find_if(pool.models.begin(),pool.models.end(),[&](const Model&m){return m.player==player;});
        std::optional<Network> imported;uint64_t importedCutoff=0;
        if(!experimental&&(std::filesystem::exists(root/"imported-candidates.txt")||std::filesystem::exists(root/"import-provenance.txt"))){std::ifstream provenance(root/"import-provenance.txt");std::string tag;int version=0;if(!(provenance>>tag>>version>>importedCutoff)||tag!="MOVEMENT_IMPORT"||version!=1||!importedCutoff)throw std::runtime_error("Missing import provenance");if(std::filesystem::exists(root/"imported-candidates.txt")){auto importedPool=LoadPool(root/"imported-candidates.txt");for(const auto& m:importedPool.models)if(m.player==player){imported=m.net;break;}}}
        if(experimental){auto fitted=candidates.load();for(const auto&m:fitted->models)if(m.player==player){imported=m.net;break;}if(!imported&&old!=pool.models.end())imported=old->net;}
        auto opt=job.options;auto cancelled=[&]{return stop||ticket!=cancelGeneration;};storage::Scratch scratch;auto snapshot=scratch.Next();job.snapshot->Copy(snapshot,cancelled);job.snapshot.reset();
        TrainingPolicy policy;policy.includeBots=opt.includeBots;
        auto result=experimental?TrainExperimental(snapshot,player,opt.all?0:opt.samples,cancelled,imported?&*imported:nullptr,policy):Train(snapshot,player,opt.all?0:opt.samples,cancelled,old==pool.models.end()?nullptr:&*old,imported?&*imported:nullptr,importedCutoff,policy);
        try{Evaluation(result);}catch(const std::exception&e){std::lock_guard lock(mutex);status.evaluation=result.diagnostic;status.error=e.what();}
        if(result.trained&&!result.cancelled&&!cancelled()){try{RetainCandidate(result);}catch(const std::exception&e){std::lock_guard lock(mutex);status.error=e.what();}}
        if(!experimental&&result.model.promoted&&!stop&&ticket==cancelGeneration){if(old!=pool.models.end())*old=result.model;else if(pool.models.size()<MaxModels)pool.models.push_back(result.model);else{result.model.promoted=false;result.reason="Personal model capacity reached; last models retained";}if(result.model.promoted)Publish(std::move(pool));}
        {std::lock_guard lock(mutex);status.training=false;status.state=result.reason;}
    }
    void Export(bool identified){auto folder=layout::Transfer(root,"Exports")/std::to_string(Stamp());std::filesystem::create_directories(folder);auto p=*published.load();if(!identified){std::erase_if(p.models,[](const Model&m){return m.player!=0;});p.disabled.clear();}SavePool(folder/"models.txt",p);
        std::ofstream f(folder/"observations.txt");if(!f)throw std::runtime_error("Cannot export dataset");f<<"MOVEMENT_DATA 1 13 3\n";storage::Scratch scratch;storage::Sort<uint64_t> sort(scratch,[&]{return bool(stop);});ReadRows(data,[&](const Example&e,size_t){sort.Add(e.group);return true;});storage::Groups groups(scratch,sort);
        ReadRows(data,[&](Example e,size_t){if(!identified)e.player=0;e.group=groups.Rank(e.group)+1;f<<Encode(e)<<'\n';return bool(f);},[&]{return bool(stop);});if(!f.flush())throw std::runtime_error("Export write failed");f.close();metadata::CompressPlainFile(folder/"observations.txt",[&]{return bool(stop);});
        std::ofstream guide(folder/"README.txt");guide<<"Movement format 1. "<<(identified?"Identified export: contains stable player IDs.":"Anonymous export: no player IDs, names, absolute positions, session IDs, or personal models.")<<"\nImported models require local held-out baseline revalidation before use.\n";State("Export saved to "+folder.string());
    }
    void Import(bool identified){
        auto folder=layout::Transfer(root,"Import");Pool incoming=LoadPool(folder/"models.txt");if(!identified&&(!incoming.disabled.empty()||std::any_of(incoming.models.begin(),incoming.models.end(),[](const Model&m){return m.player!=0;})))throw std::runtime_error("Identified import requires explicit option");
        // Validate every row and capacity BEFORE mutation. Imported evaluations are not trusted for live use.
        size_t count=0;ReadRows(folder/"observations.txt",[&](const Example&e,size_t){if(!identified&&e.player)throw std::runtime_error("Dataset contains player IDs");++count;return true;});
        if(!count)throw std::runtime_error("Import needs observations for local retraining");
        auto current=std::filesystem::exists(data)?std::filesystem::file_size(data):0;
        // External baseline claims are discarded. Their observed movement can train,
        // but only fresh local ordinary-solver pairs can validate a promotion.
        auto merged=root/"import-merged.txt";{std::ofstream out(merged);out<<"MOVEMENT_DATA 1 13 3\n";if(current)ReadRows(data,[&](const Example&e,size_t){out<<Encode(e)<<'\n';return bool(out);});storage::Scratch scratch;storage::Sort<uint64_t> sort(scratch,[&]{return bool(stop);});ReadRows(folder/"observations.txt",[&](const Example&e,size_t){sort.Add(e.group);return true;});storage::Groups ids(scratch,sort);const auto base=groupSerial;if(ids.size()>UINT64_MAX-base)throw std::overflow_error("Imported group ID overflow");groupSerial+=ids.size();
        ReadRows(folder/"observations.txt",[&](Example e,size_t){e.group=base+ids.Rank(e.group)+1;e.paired=false;e.baseline=Linear(e.x);out<<Encode(e)<<'\n';return bool(out);});if(!out.flush())throw std::runtime_error("Import write failed");}
        ReadRows(merged,[](const Example&,size_t){return true;});
        Replace(root/"import-provenance.txt","MOVEMENT_IMPORT 1 "+std::to_string(groupSerial)+"\n");
        SavePool(root/"imported-candidates.txt",incoming);
        auto backup=recovery::Backup(root,"Imports","import-backup-"+std::to_string(Stamp())+".txt");if(current)std::filesystem::rename(data,backup);
        try{std::filesystem::rename(merged,data);}catch(...){if(current)std::filesystem::rename(backup,data);throw;}
        // Retain imported weights as a reviewable candidate, never silently replace local good models.
        {std::lock_guard lock(mutex);status.examples+=count;status.diskBytes=std::filesystem::file_size(data);}tracks.clear();pending.clear();timeGroups.clear();++collectionGeneration;State("Imported data and candidate models; train/evaluate locally before use");
    }
    void CommandEvent(Command c){FlushData(true);if(c.action==Action::ResetAll||c.action==Action::ResetPlayer||c.action==Action::ImportAnonymous||c.action==Action::ImportIdentified)appendWriter.reset();std::unique_lock<std::mutex>guard(fitRunMutex,std::defer_lock);if(c.action!=Action::Train&&c.action!=Action::TrainExperimental&&c.action!=Action::Cancel&&c.action!=Action::ExportAnonymous&&c.action!=Action::ExportIdentified){++cancelGeneration;{std::lock_guard lock(fitMutex);fitQueue.clear();}guard.lock();}if(!writable&&c.action!=Action::ResetAll&&c.action!=Action::ExportAnonymous&&c.action!=Action::ExportIdentified)throw std::runtime_error("Existing files require reset/recovery before mutation");auto pool=*published.load();switch(c.action){
        case Action::Train:Training(c.player,c.ticket);break;case Action::TrainExperimental:Training(c.player,c.ticket,true);break;case Action::Cancel:++cancelGeneration;break;
        case Action::ExportAnonymous:Export(false);break;case Action::ExportIdentified:Export(true);break;
        case Action::ImportAnonymous:Import(false);break;case Action::ImportIdentified:Import(true);break;
        case Action::DisablePlayer:std::erase_if(pending,[&](const Pending&p){return p.anchor.past[2].player==c.player;});if(c.player&&pool.disabled.size()<1024&&std::find(pool.disabled.begin(),pool.disabled.end(),c.player)==pool.disabled.end())pool.disabled.push_back(c.player);Publish(std::move(pool));State("Player disabled");break;
        case Action::EnablePlayer:std::erase(pool.disabled,c.player);Publish(std::move(pool));State("Player enabled");break;
        case Action::ResetAll:{++collectionGeneration;auto backup=layout::Recovery(root,"ResetBackups")/std::to_string(Stamp());std::filesystem::create_directories(backup);std::vector<std::pair<std::filesystem::path,std::filesystem::path>> moved;try{for(auto name:{"observations.txt","observations.txt.import-backup","import-merged.txt","models.txt","models.txt.previous","models.txt.tmp","imported-candidates.txt","imported-candidates.txt.previous","imported-candidates.txt.tmp","import-provenance.txt","import-provenance.txt.previous","import-provenance.txt.tmp","player-reset.txt","experimental-candidates.txt","experimental-candidates.txt.previous","experimental-candidates.txt.tmp","last-evaluation.txt","last-evaluation.txt.previous","last-evaluation.txt.tmp"}){std::string entry=name;auto source=entry.ends_with(".previous")?layout::Previous(root/entry.substr(0,entry.size()-9)):root/name;if(std::filesystem::exists(source)){std::filesystem::rename(source,backup/name);moved.emplace_back(source,backup/name);}}}catch(...){for(auto it=moved.rbegin();it!=moved.rend();++it){std::error_code ec;std::filesystem::rename(it->second,it->first,ec);if(ec){writable=false;throw std::runtime_error("Reset interrupted; some files retained in ResetBackups, writes paused");}}throw;}{std::ofstream empty(data,std::ios::binary);if(!empty||!(empty<<"MOVEMENT_DATA 1 13 3\n")||!empty.flush()){writable=false;throw std::runtime_error("Reset data retained in ResetBackups; could not initialize empty working dataset");}}tracks.clear();pending.clear();personRows.clear();personTrained.clear();published.store(std::make_shared<const Pool>());candidates.store(std::make_shared<const Pool>());writable=true;sinceTrain=lastTrained=0;{std::lock_guard lock(mutex);status={};status.state="Reset complete; prior data preserved in ResetBackups";}break;}
        case Action::ResetPlayer:{if(!c.player)throw std::runtime_error("Select a stable player ID");auto outPath=root/"player-reset.txt";{std::ofstream out(outPath);out<<"MOVEMENT_DATA 1 13 3\n";ReadRows(data,[&](const Example&e,size_t){if(e.player!=c.player)out<<Encode(e)<<'\n';return bool(out);});if(!out.flush())throw std::runtime_error("Player reset write failed");}auto backup=recovery::Backup(root,"PlayerResets","player-reset-"+std::to_string(Stamp())+".backup");if(std::filesystem::exists(data))std::filesystem::rename(data,backup);try{std::filesystem::rename(outPath,data);}catch(...){if(std::filesystem::exists(backup))std::filesystem::rename(backup,data);throw;}std::erase_if(pool.models,[&](const Model&m){return m.player==c.player;});std::erase_if(pending,[&](const Pending&p){return p.anchor.past[2].player==c.player;});std::erase_if(tracks,[&](const auto&p){return !p.second.history.empty()&&p.second.history.back().player==c.player;});personRows.erase(c.player);personTrained.erase(c.player);try{Publish(std::move(pool));}catch(...){std::error_code ec;std::filesystem::remove(data,ec);ec.clear();std::filesystem::rename(backup,data,ec);if(ec){writable=false;throw std::runtime_error("Player reset interrupted; prior dataset retained in backup, writes paused");}throw;}
        if(std::filesystem::exists(root/"imported-candidates.txt")){auto imported=LoadPool(root/"imported-candidates.txt");std::erase_if(imported.models,[&](const Model&m){return m.player==c.player;});SavePool(root/"imported-candidates.txt",imported);}
        {auto fitted=*candidates.load();std::erase_if(fitted.models,[&](const Model&m){return m.player==c.player;});PublishCandidates(std::move(fitted));}
        Status refreshed;ReadRows(data,[&](const Example&e,size_t){++refreshed.examples;if(e.paired)++refreshed.paired;return true;});{std::lock_guard lock(mutex);status.examples=refreshed.examples;status.paired=refreshed.paired;status.diskBytes=std::filesystem::file_size(data);status.storageFull=false;}State("Personal data/model reset; general aggregate retained");break;}
    }}
    void Run()noexcept{
        try{std::filesystem::create_directories(root);std::filesystem::create_directories(layout::Transfer(root,"Import"));{std::lock_guard dataLock(dataMutex);neuralio::EnsureAppend(data);}
            auto previous=layout::Previous(models);
            if(std::filesystem::exists(models))published.store(std::make_shared<const Pool>(LoadPool(models)));
            else if(std::filesystem::exists(previous))published.store(std::make_shared<const Pool>(LoadPool(previous)));
            Status loaded;ReadRows(data,[&](const Example&e,size_t){++loaded.examples;if(e.paired)++loaded.paired;groupSerial=std::max(groupSerial,e.group+1);return true;},[&]{return bool(stop);});{std::lock_guard dataLock(dataMutex);if(std::filesystem::exists(data))Writer();}loaded.models=published.load()->models.size();loaded.diskBytes=std::filesystem::exists(data)?std::filesystem::file_size(data):0;loaded.state="Ready; collection and use are independent";{std::lock_guard lock(mutex);status=loaded;}
        }catch(const std::exception&e){writable=false;State("Load failed; existing files preserved",e.what());}
        // An invalid experimental file must not disable accepted models or dataset collection.
        if(writable)try{auto path=root/"experimental-candidates.txt";if(std::filesystem::exists(path))candidates.store(std::make_shared<const Pool>(LoadCandidates(path)));std::string evaluation;auto last=root/"last-evaluation.txt";if(std::filesystem::exists(last))evaluation=metadata::Read(last,16384);std::lock_guard lock(mutex);status.candidates=candidates.load()->models.size();status.evaluation=std::move(evaluation);}catch(const std::exception&e){std::lock_guard lock(mutex);status.error=std::string("Experimental candidates unavailable: ")+e.what();}
        while(true){try{std::lock_guard dataLock(dataMutex);FlushData(false);}catch(const std::exception&e){State("Dataset writes paused after compression flush failure",e.what());}Queued queued;{std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(100),[&]{return stop||!queue.empty();});if(queue.empty()){if(stop)break;continue;}queued=std::move(queue.front());queue.pop_front();}
            if(workerGeneration!=collectionGeneration){tracks.clear();pending.clear();workerGeneration=collectionGeneration;}
            auto& event=queued.event;if(!std::holds_alternative<Command>(event)&&queued.epoch!=workerGeneration)continue;
            try{std::lock_guard dataLock(dataMutex);if(auto o=std::get_if<Observation>(&event))Observe(*o);else if(auto b=std::get_if<Baseline>(&event))BaselineEvent(*b);else if(!stop){CommandEvent(std::get<Command>(event));if(writable&&std::filesystem::exists(data)&&!appendlog::IsAppend(data))neuralio::EnsureAppend(data);}
                if(!stop&&Opt().collect&&Opt().autoTrain&&sinceTrain>=lastTrained+4096){auto ticket=cancelGeneration.load();Training(0,ticket);if(!stop&&ticket==cancelGeneration){uint64_t candidate=0,best=UINT64_MAX;auto snapshot=published.load();for(auto[id,n]:personRows)if(id&&n>=1024&&n>=personTrained[id]+512&&personTrained[id]<best&&std::find(snapshot->disabled.begin(),snapshot->disabled.end(),id)==snapshot->disabled.end()){candidate=id;best=personTrained[id];}if(candidate)Training(candidate,ticket);}}
            }catch(const std::exception&e){std::lock_guard lock(mutex);status.error=e.what();status.state=writable?"IO operation failed; original files retained":"Dataset writes paused after IO error; inspect dataset tail before restarting";}catch(...){State("Operation failed; ordinary predictor retained","Unexpected worker error");}
        }
        try{std::lock_guard dataLock(dataMutex);FlushData(true);if(writable&&std::filesystem::exists(data)&&!appendlog::IsAppend(data))neuralio::EnsureAppend(data);}catch(const std::exception&e){State("Shutdown flush failed; retained file can be recovered on restart",e.what());return;}
        State("Stopped; pending training cancelled");
    }
};
Service::Service(const std::filesystem::path&r):p(std::make_unique<Impl>(r)){}
Service::~Service(){Stop();}
void Service::InvalidateHistory()noexcept{++p->collectionGeneration;}
void Service::Configure(Options o){o.samples=std::clamp(o.samples,size_t(1024),size_t(1000000));bool start=false;{std::lock_guard lock(p->mutex);if(p->options.collect!=o.collect){start=o.collect;++p->collectionGeneration;if(!o.collect)++p->cancelGeneration;}p->options=o;}if(start&&o.autoTrain)p->Push(Impl::Command{Action::Train,0,p->cancelGeneration.load()});}
bool Service::Observe(const Observation&o)noexcept{return p->Push(o);}
bool Service::RecordBaseline(const Baseline&b)noexcept{return p->Push(b);}
bool Service::Request(Action a,uint64_t id)noexcept{if(a==Action::Cancel){++p->cancelGeneration;return true;}return p->Push(Impl::Command{a,id,p->cancelGeneration.load()});}
std::shared_ptr<const Pool> Service::Published()const noexcept{return p->published.load();}
std::shared_ptr<const Pool> Service::Candidates()const noexcept{return p->candidates.load();}
Status Service::GetStatus()const{std::unique_lock lock(p->mutex);auto s=p->status;s.dropped=p->dropped;s.queued=p->queue.size();return s;}
std::shared_ptr<storage::Snapshot> Service::SnapshotData()const{std::lock_guard lock(p->dataMutex);if(std::filesystem::exists(p->data)&&!appendlog::IsAppend(p->data))throw std::runtime_error("Neural storage initialization is in progress; retry the snapshot after readiness");p->FlushData(true);return std::make_shared<storage::Snapshot>(p->data);}
void Service::Stop()noexcept{if(!p)return;p->stop=true;++p->cancelGeneration;p->wake.notify_all();p->fitWake.notify_all();if(p->worker.joinable())p->worker.join();if(p->trainer.joinable())p->trainer.join();{std::lock_guard lock(p->fitMutex);p->fitQueue.clear();}}
}
