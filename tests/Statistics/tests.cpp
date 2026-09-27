#include "../../Nikogram/src/Features/Statistics/Storage.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <functional>
using namespace Statistics;
int checks=0;
void Check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void Reject(const std::function<void()>& operation,const char* why)
{bool rejected=false;try{operation();}catch(...){rejected=true;}Check(rejected,why);}
CombatEvent Kill()
{
    CombatEvent e;e.kind=CombatEvent::Death;e.local=1;
    e.attacker={1,"Local",true,true,false};e.victim={2,"Target",true,true,true};
    e.hasFlags=e.hasCustom=true;e.keys={"class:2","weapon:17","map:cp.test"};e.names={"Sniper","Weapon ID 17","cp.test"};return e;
}
int main()try
{
    for(int bits=0;bits<64;++bits)
        Check(TrackingAllowed(bits&1,bits&2,bits&4,bits&8,bits&16,bits&32)==(bits==11),"tracking requires live matching account; demos and practice excluded");
    for(int priority=-10;priority<=10;++priority)
    {
        Check(!ManualPriorityLabel({{"Target",priority,true}},priority).empty()==(priority>=1),"manual priority threshold exactly one");
        Check(ManualPriorityLabel({{"Display label",priority,false}},10).empty(),"display-only labels never qualify");
    }
    Check(ManualPriorityLabel({},10).empty(),"automatic priority alone never qualifies");
    Check(ManualPriorityLabel({{"Detected",10,true,false}},10).empty(),"automatic detection provenance excluded");
    Check(ManualPriorityLabel({{"Detected",10,true,false},{"Manual",1,true,true}},10)=="Manual","manual lower tag controls grouping despite high automatic priority");
    Check(ManualPriorityLabel({{"",1,true}},1)=="Unnamed priority","empty display name does not change eligibility");
    Check(ManualPriorityLabel({{"High",8,true},{"Low",1,true}},-1).empty(),"ignored effective priority suppresses stats");
    Check(ManualPriorityLabel({{"High",8,true},{"Low",1,true}},8)=="High","highest manual targeting label wins");
    Model m;auto e=Kill();Check(Apply(m,e),"local kill recorded");
    for(View* v:{&m.match,&m.session,&m.lifetime})
    {
        Check(v->total.n[Kills]==1 && v->players.at(2).counts.n[Kills]==1,"kill in every scope and pair");
        Check(v->total.n[PeakStreak]==1 && v->players.at(2).counts.n[PeakStreak]==0,"streak is personal, not a pair streak");
        Check(v->breakdowns.size()==3 && v->breakdowns.at("map:cp.test").counts.n[Kills]==1,"class weapon map breakdown recorded");
        Check(v->total.Known(Kills) && !v->total.Known(DamageOut),"availability is not invented for unseen event types");
    }
    e.victim.name="Renamed";Apply(m,e);Check(m.lifetime.players.size()==1 && m.lifetime.players.at(2).name=="Renamed","stable identity survives rename");
    e.victim.priority=false;Apply(m,e);Check(m.lifetime.total.n[Kills]==3 && m.lifetime.players.at(2).counts.n[Kills]==2,"unprioritized still counts overall, not per player");
    auto groups=GroupPlayers(m.lifetime,[](uint32_t){return std::string("New label");});
    Check(groups.at("New label").n[Kills]==2,"historical records regroup under current label");
    Check(GroupPlayers(m.lifetime,[](uint32_t){return std::string();}).empty() && m.lifetime.players.size()==1,"priority removal hides but does not erase history");
    m.NewMatch();Check(m.match.total.n[Kills]==0 && m.session.total.n[Kills]==3 && m.lifetime.total.n[Kills]==3 && !m.streak,"new visit resets only match and current streak");
    e=Kill();e.feign=true;Check(!Apply(m,e) && m.streak==0,"feign deaths excluded");
    e.feign=false;e.victim={0,"Bot",false,true,false};Check(!Apply(m,e),"bot victim excluded");
    e=Kill();e.attacker={3,"Other",true,true,false};Check(!Apply(m,e),"unrelated fight excluded");
    e=Kill();e.local=0;Check(!Apply(m,e),"no account excluded");
    for(int flags=0;flags<16;++flags)
    {
        Model sample;auto kill=Kill();kill.headshot=(flags&1)!=0;kill.backstab=(flags&2)!=0;kill.domination=(flags&4)!=0;kill.revenge=(flags&8)!=0;
        Apply(sample,kill);const auto& c=sample.lifetime.total;
        Check(c.n[Kills]==1 && c.n[Headshots]==uint64_t(kill.headshot) && c.n[Backstabs]==uint64_t(kill.backstab),"kill subtype metrics");
        Check(c.n[Dominations]==uint64_t(kill.domination) && c.n[Revenges]==uint64_t(kill.revenge),"killer rivalry flags");
        kill.attacker={3,"Other",true,true,true};kill.assister={1,"Local",true,true,false};
        kill.assistDomination=kill.domination;kill.assistRevenge=kill.revenge;
        Model assist;Apply(assist,kill);
        Check(assist.lifetime.total.n[Assists]==1 && assist.lifetime.total.n[Kills]==0,"assists are not kills");
        Check(assist.lifetime.total.n[Dominations]==uint64_t(kill.assistDomination) && assist.lifetime.total.n[Revenges]==uint64_t(kill.assistRevenge),"assister rivalry flags attributed to local player");
        Check(assist.lifetime.players.at(2).counts.n[Assists]==1,"assist attaches to victim, not teammate");
        kill.victim={1,"Local",true,true,false};kill.assister={4,"Assistant",true,true,true};
        Model death;Apply(death,kill);
        Check(death.lifetime.total.n[Deaths]==1 && death.lifetime.players.at(3).counts.n[Deaths]==1,"one death to killer");
        Check(death.lifetime.players.at(4).counts.n[Deaths]==0 && death.lifetime.players.at(4).counts.n[AssistedDeaths]==1,"assistant does not double count death");
        Check(death.lifetime.total.n[Dominated]==2*uint64_t(kill.domination),"both earned dominations attributed without duplicating death");
        Check(death.lifetime.breakdowns.at("class:2").counts.n[Deaths]==1 && death.lifetime.breakdowns.at("class:2").counts.n[Dominated]==death.lifetime.total.n[Dominated],"breakdowns agree with overview on assistant flags");
    }
    m={};e=Kill();Apply(m,e);Apply(m,e);Apply(m,e);
    Check(m.lifetime.total.n[PeakStreak]==3,"peak streak accumulated");
    auto death=Kill();death.victim=death.attacker;death.attacker={};Apply(m,death);
    Check(m.lifetime.total.n[Deaths]==1 && m.streak==0 && m.lifetime.players.size()==1,"world death counts without fake player record");
    Apply(m,e);Check(m.lifetime.total.n[PeakStreak]==3 && m.streak==1,"peak is max, not additive");
    death.attacker=death.victim;Apply(m,death);Check(m.lifetime.total.n[Deaths]==2 && !m.lifetime.players.contains(1),"self death excludes local pair");
    Apply(m,e);death.attacker={0,"Bot",false,true,false};Check(!Apply(m,death) && !m.streak && m.lifetime.total.n[Deaths]==2,"bot death resets streak but not stats");
    e=Kill();e.kind=CombatEvent::Hurt;e.damage=150;e.hasCrit=e.hasMini=true;
    for(int direction=0;direction<2;++direction)for(int critical=0;critical<3;++critical)
    {
        Model hurt;auto hit=e;if(direction)std::swap(hit.attacker,hit.victim);hit.crit=critical==1;hit.mini=critical==2;
        Check(Apply(hurt,hit),"damage recorded");const auto& c=hurt.lifetime.total;
        Check(c.n[direction?DamageIn:DamageOut]==150,"damage direction");
        Check(c.n[direction?CritDamageIn:CritDamageOut]==(critical==1?150:0),"critical damage subset");
        Check(c.n[direction?MiniDamageIn:MiniDamageOut]==(critical==2?150:0),"mini critical damage subset");
        Check(!c.Known(Kills) && c.Known(CritDamageIn),"damage availability without inventing death counters");
    }
    m={};e.hasCrit=e.hasMini=false;e.damage=20;Apply(m,e);
    Check(!m.lifetime.total.Known(CritDamageOut) && !m.lifetime.total.Known(MiniDamageOut),"missing flags remain unobserved");
    e.damage=-20;Check(!Apply(m,e),"negative damage ignored");
    e.damage=20;e.victim=e.attacker;Check(!Apply(m,e),"self damage excluded");
    Counts sum,one;one.n[PeakStreak]=8;one.n[Kills]=3;one.observed=1u<<Kills;sum.Add(one);sum.Add(one);
    Check(sum.n[Kills]==6 && sum.n[PeakStreak]==8 && sum.Known(Kills),"group sum combines sums/max and availability");
    Model stored;auto k=Kill();k.victim.name="Quotes \" & unicode \xE2\x98\x85";Apply(stored,k);
    auto tree=Storage::EncodeView(stored.lifetime,1);std::stringstream json;boost::property_tree::write_json(json,tree);
    Storage::Tree decoded;boost::property_tree::read_json(json,decoded);auto view=Storage::DecodeView(decoded,1);
    Check(view.players.at(2).name==k.victim.name && view.total.n==stored.lifetime.total.n,"JSON names and counters round trip");
    Check(view.breakdowns.contains("map:cp.test") && view.total.observed==stored.lifetime.total.observed,"dotted map keys and availability round trip");
    Reject([&]{Storage::DecodeView(decoded,99);},"different account rejected");
    auto bad=decoded;bad.put("version",99);Reject([&]{Storage::DecodeView(bad,1);},"future schema rejected without reinterpretation");
    auto missing=decoded;missing.get_child("overview").erase("kills");
    Check(!Storage::DecodeView(missing,1).total.Known(Kills),"missing stored metric remains unavailable, not a fabricated zero");
    for(const char* invalid:{"-1","1.5","abc","18446744073709551616","123tail",""})
    {auto invalidTree=decoded;invalidTree.put("overview.kills",invalid);Reject([&]{Storage::DecodeView(invalidTree,1);},"invalid counters rejected");}
    auto folder=std::filesystem::temp_directory_path()/("nikogram-statistics-tests-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    auto path=folder/"1.json";Storage::Save(path,stored.lifetime,1);
    Check(Storage::Load(path,1).total.n[Kills]==1,"atomic disk save loads");
    Apply(stored,k);Storage::Save(path,stored.lifetime,1);
    Check(Storage::Load(path,1).total.n[Kills]==2 && Storage::Load(path.string()+".bak",1).total.n[Kills]==1,"backup retains prior complete snapshot");
    Check(!std::filesystem::exists(path.string()+".tmp"),"atomic promotion leaves no pending temporary file");
    // Stress mixed tracked/untracked identities across all three scopes.
    Model stress;
    for(int i=0;i<1000;++i){auto x=Kill();x.victim.id=uint32_t(2+i%20);x.victim.priority=i%2==0;Apply(stress,x);}
    Check(stress.lifetime.total.n[Kills]==1000 && stress.lifetime.players.size()==10,"stress overall includes all while pair records stay restricted");
    for(const auto& [id,r]:stress.lifetime.players)Check(r.counts.n[Kills]==50,"stress per-account counts");
    std::cout<<checks<<" statistics checks passed; isolated fixtures: "<<folder.string()<<'\n';
}
catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
