#include "../../Nikogram/src/Features/ImGui/TextStyle.h"
#include <cassert>
#include "../../Nikogram/src/Features/Aimbot/TargetPolicy.h"
#include "../../Nikogram/src/Features/Simulation/MovementSimulation/PredictionPolicy.h"
#include <iostream>
#include "../../Nikogram/src/Features/Aimbot/RocketSafetyGeometry.h"
#include "../../Nikogram/src/Features/Aimbot/LeadRestrictPolicy.h"
#include "../../Nikogram/src/Features/Aimbot/SelfDamagePolicy.h"
#include "../../Nikogram/src/Features/Simulation/MovementSimulation/LedgePrediction.h"
#include "../../Nikogram/src/Features/Simulation/MovementSimulation/CounterStrafe.h"
#include <string_view>
#include "../../Nikogram/src/Features/CritHack/CritIndicatorStyle.h"
#include "../../Nikogram/src/Features/Visuals/SpectatorList/SpectatorStyle.h"
int main()
{
    {
        using LeadRestrictPolicy::Exception;
        const auto allowed=[](float target,float shot,float limit,float distance,float surface,bool retained=false){return std::string_view(Exception(target,shot,limit,distance,surface,retained))=="close_range_exception";};
        assert(allowed(5,25,20,60,0));
        assert(allowed(5,30,20,112,64));
        assert(!allowed(20,25,20,60,0));
        assert(!allowed(5,31,20,60,0));
        assert(!allowed(5,25,20,113,0));
        assert(!allowed(5,25,20,60,65));
        assert(allowed(5,32,20,128,64,true));
        assert(!allowed(5,33,20,128,64,true));
        assert(!allowed(5,32,20,129,64,true));
        assert(!allowed(5,65,60,60,0,true));
        assert(!allowed(0,0,0,60,0));
        assert(!allowed(NAN,25,20,60,0));
        assert(!allowed(5,INFINITY,20,60,0));
        assert(!allowed(5,25,20,-1,0));
        std::cout << "LeadRestrict: close geometry, FOV, bounded angle, retention margins and invalid inputs passed\n";
    }
    {
        using namespace RocketSafetyGeometry;
        float fraction; V3 normal;
        assert(SegmentBox({0,0,0},{100,0,0},{30,-24,-24},{78,24,58},fraction,normal));
        assert(std::fabs(fraction-.3f)<.0001f && normal[0]==-1);
        assert(!SegmentBox({0,30,0},{100,30,0},{30,-24,-24},{78,24,58},fraction,normal));
        assert(SegmentBox({50,0,0},{100,0,0},{30,-24,-24},{78,24,58},fraction,normal) && fraction==0);
        assert(SegmentBox({100,0,0},{0,0,0},{30,-24,-24},{78,24,58},fraction,normal));
        assert(std::fabs(fraction-.22f)<.0001f && normal[0]==1);
        assert(SegmentBox({0,24,0},{100,24,0},{30,-24,-24},{78,24,58},fraction,normal));
        assert(!SegmentBox({0,0,0},{0,0,0},{30,-24,-24},{78,24,58},fraction,normal));
        assert(!SegmentBox({0,0,0},{100,0,0},{78,-24,-24},{30,24,58},fraction,normal));
        assert(!SegmentBox({NAN,0,0},{100,0,0},{30,-24,-24},{78,24,58},fraction,normal));
        // Representative close body crossing before a distant floor intersection.
        assert(SegmentBox({50.324f,-419.662f,-134.797f},{66,-417,-140},{51,-430,-192},{99,-382,-110},fraction,normal));
        assert(fraction<.05f);
        // The caller must not select that hull if a world wall blocks the ray sooner.
        assert(!(fraction <= .01f));
        Heartbeat throttle;
        assert(throttle.Due(1000,false));
        for(unsigned long long ms=1001;ms<6000;++ms) assert(!throttle.Due(ms,false));
        assert(throttle.Due(6000,false));
        assert(throttle.Due(6001,true));
        assert(!throttle.Due(6002,false));
        assert(throttle.Due(10,false)); // clock/session reset
        std::cout << "RocketSafety: swept collision hulls, close-body contact, wall ordering, invalid geometry and idle throttling passed\n";
    }
    assert(!SelfDamagePolicy::Block(500,200,20,0));
    assert(SelfDamagePolicy::Block(1,200,200,100));
    assert(!SelfDamagePolicy::Block(0,200,200,100));
    assert(!SelfDamagePolicy::Block(50,200,200,75));
    assert(SelfDamagePolicy::Block(51,200,200,75));
    assert(SelfDamagePolicy::Block(50,200,40,25));
    assert(!SelfDamagePolicy::Block(60,200,200,50));
    assert(SelfDamagePolicy::Block(60+60,200,200,50));
    assert(SelfDamagePolicy::Block(std::numeric_limits<float>::quiet_NaN(),200,200,100));
    std::cout << "SelfDamage: protection endpoints, HP allowance, lethal shots, cumulative detonations and invalid estimates passed\n";
    assert(LedgePrediction::Evidence(180,300,1,.12f));
    assert(LedgePrediction::Evidence(300,300,-1,.12f));
    assert(!LedgePrediction::Evidence(300,300,1,.12f));
    assert(!LedgePrediction::Evidence(180,300,1,.5f));
    assert(!LedgePrediction::Evidence(180,300,1,.01f));
    assert(!LedgePrediction::Evidence(0,0,0,.12f));
    assert(LedgePrediction::Walkable(true,false,.8f));
    assert(!LedgePrediction::Walkable(true,false,.4f));
    assert(!LedgePrediction::Walkable(false,false,1));
    assert(!LedgePrediction::Walkable(true,true,1));
    assert(LedgePrediction::LookAhead(300,1200,1.f/66)>37.5f);
    assert(LedgePrediction::LookAhead(10000,1200,1.f/66)<=96);
    assert(LedgePrediction::LookAhead(300,0,1.f/66)==0);
    assert(LedgePrediction::Brake(10,1200,1.f/66)==0);
    assert(LedgePrediction::Brake(300,1200,1.f/66)<300);
    std::cout << "LedgePrediction: evidence, support, bounded lookahead and braking checks passed\n";
    {
        std::vector<CounterStrafe::Sample> weave, straight, single, wide;
        constexpr float pi=3.14159265359f;
        for(int i=0;i<=60;++i)
        {
            const float t=i/66.f, phase=t*2*pi/.24f;
            weave.push_back({t,100*t,12*std::sin(phase),100,12*2*pi/.24f*std::cos(phase)});
            straight.push_back({t,100*t,0,100,0});
            single.push_back({t,100*t,12*std::sin(t*2*pi/1.8f),100,12*2*pi/1.8f*std::cos(t*2*pi/1.8f)});
            wide.push_back({t,100*t,80*std::sin(phase),100,80*2*pi/.24f*std::cos(phase)});
        }
        auto result=CounterStrafe::Detect(weave);
        CounterStrafe::Audit audit;
        const auto audited=CounterStrafe::Detect(weave,&audit);
        assert(audited.valid==result.valid && audited.ax==result.ax && audited.ay==result.ay && audited.center==result.center && audited.confidence==result.confidence);
        assert(std::string_view(audit.reason)=="accepted" && audit.reversals>=3);
        CounterStrafe::Detect({},&audit);
        assert(std::string_view(audit.reason)=="insufficient_samples" && audit.reversals==0);
        assert(result.valid && std::fabs(result.ay)>.99f);
        assert(result.confidence>0 && result.confidence<=1);
        CounterStrafe::Estimate centered{true,0,1,0,0,200,1};
        assert(CounterStrafe::Correction(centered,10,100,0)==0);
        assert(CounterStrafe::Correction(centered,10,100,.15f)<-100);
        assert(CounterStrafe::Correction(centered,-10,-100,.15f)>100);
        assert(CounterStrafe::Correction(centered,0,0,.5f)==0);
        assert(!CounterStrafe::Detect(straight).valid);
        assert(!CounterStrafe::Detect(single).valid);
        assert(!CounterStrafe::Detect(wide).valid);
        auto shortHistory=weave; shortHistory.resize(8);
        assert(!CounterStrafe::Detect(shortHistory).valid);
        auto duplicate=weave; duplicate[10].time=duplicate[9].time;
        assert(!CounterStrafe::Detect(duplicate).valid);
        auto invalid=weave; invalid[10].vx=std::numeric_limits<float>::quiet_NaN();
        assert(!CounterStrafe::Detect(invalid).valid);
        auto rotated=weave;
        for(auto& p:rotated) { std::swap(p.x,p.y); std::swap(p.vx,p.vy); p.x+=300; p.y-=500; }
        assert(CounterStrafe::Detect(rotated).valid);
        auto stopped=weave;
        for(size_t i=36;i<stopped.size();++i) { stopped[i].vy=0; stopped[i].y=stopped[35].y; }
        assert(!CounterStrafe::Detect(stopped).valid);
        for(float period:{.075f,.09f,.12f})
        {
            std::vector<CounterStrafe::Sample> fast;
            for(int i=0;i<=30;++i)
            {
                const float t=i/66.f, phase=t*2*pi/period;
                fast.push_back({t,100*t,2*std::sin(phase),100,2*2*pi/period*std::cos(phase)});
            }
            assert(CounterStrafe::Detect(fast).valid);
            CounterStrafe::Audit fastAudit;
            assert(CounterStrafe::Detect(fast,&fastAudit).valid);
            assert(fastAudit.rapid && std::string_view(fastAudit.reason)=="accepted");
            auto jitter=fast;
            for(auto& p:jitter) p.y=0;
            assert(!CounterStrafe::Detect(jitter).valid);
            auto mismatched=fast;
            for(auto& p:mismatched) p.y=-p.y;
            assert(!CounterStrafe::Detect(mismatched).valid);
            auto sparse=fast;
            sparse.resize(16);
            for(size_t i=0;i<sparse.size();++i) sparse[i].time=i*.045f;
            assert(!CounterStrafe::Detect(sparse).valid);
        }
        std::cout << "CounterStrafe: rapid reversals, small displacement, velocity jitter, inconsistent positions and sparse updates passed\n";
        std::cout << "CounterStrafe: weave, straight, single reversal, wide, insufficient, duplicate, nonfinite, rotation and stopped-pattern checks passed\n";
    }
    assert(TargetPolicy::CandidateCount(8, 1, true) == 2);
    assert(TargetPolicy::CandidateCount(8, 1, false) == 1);
    assert(TargetPolicy::CandidateCount(0, 3, true) == 0);
    for (int limit=1;limit<=6;++limit)
    {
        std::vector<bool> seen(64);
        for (unsigned tick=0;tick<128;++tick)
        {
            auto chosen=TargetPolicy::Candidates(64,limit,true,tick);
            assert(chosen.size() <= std::size_t(limit+2));
            auto sorted=chosen; std::sort(sorted.begin(),sorted.end());
            assert(std::adjacent_find(sorted.begin(),sorted.end())==sorted.end());
            for(auto index:chosen) { assert(index<64); seen[index]=true; }
        }
        assert(std::all_of(seen.begin(),seen.end(),[](bool v){return v;}));
    }
    assert(TargetPolicy::Candidates(8,0,true,0).empty());
    assert(PredictionPolicy::Confidence(1,1)==0.f);
    assert(PredictionPolicy::Confidence(2,1)==.5f);
    assert(PredictionPolicy::Confidence(0,0)==0.f);
    assert(PredictionPolicy::Confidence(3,0)==1.f);
    assert(!PredictionPolicy::Fresh(1.f,1.f));
    assert(PredictionPolicy::Fresh(1.1f,1.f));
    assert(PredictionPolicy::Discontinuity(-.1f,0.f,100.f));
    assert(PredictionPolicy::Discontinuity(2.f,0.f,100.f));
    assert(PredictionPolicy::Discontinuity(.1f,1000.f,100.f));
    assert(!PredictionPolicy::Discontinuity(.1f,10.f,100.f));
    assert(PredictionPolicy::LandingYaw(5.f)==0.f && PredictionPolicy::LandingYaw(-5.f)==0.f);
    int state=7;
    {
        const int original=state;
        PredictionPolicy::ScopeExit outer([&]{state=original;});
        state=8;
        try {
            const int innerOriginal=state;
            PredictionPolicy::ScopeExit inner([&]{state=innerOriginal;});
            state=9;
            throw 1;
        } catch(int) {}
        assert(state==8);
    }
    assert(state==7);
    std::cout << "PredictionPolicy: confidence, freshness, discontinuity, landing reset, scoped restoration and bounded/fair target scheduling passed\n";
    assert(!TargetPolicy::HasFlightTick(0));
    assert(!TargetPolicy::HasFlightTick(-1));
    assert(TargetPolicy::HasFlightTick(1));
    assert(!TargetPolicy::SplashRisk(false, false, 146.f, 100.f, true));
    assert(TargetPolicy::SplashRisk(true, false, 146.f, 100.f, true));
    assert(!TargetPolicy::SplashRisk(true, true, 146.f, 100.f, true));
    assert(!TargetPolicy::SplashRisk(true, false, 146.f, 100.f, false));
    assert(!TargetPolicy::SplashRisk(true, false, 0.f, 0.f, true));
    assert(!TargetPolicy::SplashRisk(true, false, 146.f, 90000.f, true));
    static_assert(SpectatorStyle::WatchingLocal(7, 7));
    static_assert(!SpectatorStyle::WatchingLocal(8, 7));
    static_assert(!SpectatorStyle::WatchingLocal(0, 7));
    static_assert(!SpectatorStyle::WatchingLocal(0, 0));
    static_assert(!SpectatorStyle::WatchingLocal(-1, -1));
    using namespace Workspace;
    assert(!PreserveTextColour);
    {ScopedTextColour ordinary(false);assert(!PreserveTextColour);}
    {ScopedTextColour name(true);assert(PreserveTextColour);
        {ScopedTextColour nested(false);assert(PreserveTextColour);}
        assert(PreserveTextColour);
    }
    assert(!PreserveTextColour);
    {ScopedTextColour activeBind;assert(PreserveTextColour);}
    assert(!PreserveTextColour);
    try{ScopedTextColour name(true);throw 1;}catch(int){}
    assert(!PreserveTextColour);
    Accent[0]=.25f;TextColour[0]=.8f;TextColourOverride=false;
    assert(TextChannel(0)==.25f);
    {ScopedTextColour name;assert(!TextColourOverride&&TextChannel(0)==.25f);}
    TextColourOverride=true;
    {ScopedTextColour name;assert(TextColourOverride&&TextChannel(0)==.8f);}
    assert(TextColourOverride&&TextChannel(0)==.8f&&!PreserveTextColour);
    std::cout<<"TextStyle: 13 scope/preference checks passed\n";
    using namespace CritIndicatorStyle;
    assert(std::string_view(ReserveStatus(0)) == "EMPTY");
    assert(std::string_view(ReserveStatus(-1)) == "EMPTY");
    assert(std::string_view(ReserveStatus(1)) == "READY");
    assert(Charge(0,7,true)==0.f);
    assert(Charge(9,7,true)==1.f);
    assert(Charge(3,0,true)==0.f);
    assert(Charge(3,7,false)==0.f);
    assert(Charge(-1,7,true)==0.f);
    assert(LabelBrightness(0.f)==.6f);
    float previous=0.f;
    for(int stored=0;stored<=7;++stored)
    {
        const auto brightness=LabelBrightness(Charge(stored,7,true));
        assert(brightness>=previous && brightness<=1.f);
        assert(brightness==TickIndicatorStyle::LabelBrightness(TickIndicatorStyle::Charge(stored,7,false)));
        previous=brightness;
    }
    assert(LabelBrightness(1.f)==1.f);
    assert(LabelBrightness(-1.f)==.6f);
    assert(LabelBrightness(2.f)==1.f);
    assert(!Full(0,7,true));
    assert(!Full(6,7,true));
    assert(Full(7,7,true));
    assert(Full(8,7,true));
    assert(!Full(7,7,false));
    assert(!Full(0,0,true));
    std::cout<<"CritIndicatorStyle: 34 status/brightness checks passed\n";
    assert(Streaming(true,false,11.f,10.f));
    assert(!Streaming(true,false,10.f,10.f));
    assert(!Streaming(true,false,9.f,10.f));
    assert(!Streaming(false,false,11.f,10.f));
    assert(!Streaming(true,true,11.f,10.f));
    assert(BarCharge(0.f,true)==1.f);
    assert(BarCharge(.5f,true)==1.f);
    assert(BarCharge(.5f,false)==.5f);
    assert(BarCharge(0.f,Streaming(true,false,10.f,10.f))==0.f);
    std::cout<<"CritIndicatorStyle: 9 stream/restore checks passed\n";
    namespace SS = SpectatorStyle;
    assert(SS::Classify(4,true).view==SS::First);
    assert(SS::Classify(5,true).view==SS::Third);
    assert(SS::Classify(1,true).state==SS::Dead);
    assert(SS::Classify(2,true).state==SS::Freeze);
    assert(SS::Classify(3,true).view==SS::Fixed);
    assert(!SS::Classify(6,true).targeted);
    assert(SS::Classify(6,true).view==SS::Free);
    assert(SS::Classify(0,true).state==SS::Dead);
    assert(SS::Classify(99,true).state==SS::Presumed);
    for(int mode=0;mode<=6;++mode)
    {
        auto c=SS::Classify(mode,false);
        assert(c.state==SS::Presumed && c.view==SS::Unknown && !c.targeted);
        c=SS::Classify(mode,true);
        assert(SS::Include(c.state,c.view,15,63));
        assert(!SS::Include(c.state,c.view,0,63));
        assert(!SS::Include(c.state,c.view,15,0));
        assert(!SS::Include(c.state,c.view,15 & ~c.state,63));
        assert(!SS::Include(c.state,c.view,15,63 & ~c.view));
    }
    int layouts=0;
    for(bool group : {false,true}) for(int cols : {1,2,3}) for(int height : {260,400,600})
    {
        std::vector<int> targets;
        for(int i=0;i<101;++i) targets.push_back(group ? i/5 : i%5);
        auto pages=SS::Paginate(targets,group,cols,230,4,6,16,102,22,height);
        std::vector<int> seen(101);
        for(const auto& page : pages)
        {
            assert(!page.empty());
            for(const auto& p : page)
            {
                assert(p.x>=6 && p.x+230<=6+cols*234);
                assert(p.y>=22 && p.y+(p.header?16:102)+22<=height);
                if(!p.header) ++seen[p.index];
            }
        }
        for(int n : seen) assert(n==1);
        ++layouts;
    }
    assert(SS::Columns(708,230,4)==3);
    assert(SS::Columns(200,230,4)==1);
    assert(SS::Paginate({},false,1,230,4,6,16,102,22,260).size()==1);
    std::cout<<"SpectatorStyle: classification/filter checks and "<<layouts<<" 101-player pagination scenarios passed\n";
    const std::array<int,4> compact = {70,70,65,90};
    assert(SS::FitColumns(compact,600)==compact);
    const std::array<int,4> hidden = {70,0,0,0};
    assert(SS::FitColumns(hidden,600)==hidden);
    const auto constrained=SS::FitColumns(compact,180);
    int total=0;
    for(int width : constrained) { assert(width>0); total+=width; }
    assert(total<=180);
    assert(SS::FitColumns(hidden,30)[1]==0);
    assert(SS::FitColumns(hidden,30)[0]==30);
    std::cout<<"SpectatorStyle: content-width and narrow-screen checks passed\n";
    assert(SS::SizeHorizontal(708,240,4,1,170).width==240);
    assert(SS::SizeHorizontal(708,240,4,2,170).width==484);
    assert(SS::SizeHorizontal(708,240,4,10,170).columns==2);
    assert(SS::SizeHorizontal(708,240,4,0,170).columns==1);
    assert(SS::SizeHorizontal(180,180,4,10,170).width==180);
    assert(SS::SizeHorizontal(708,170,4,4,170).width==692);
    std::cout<<"SpectatorStyle: 6 compact horizontal sizing checks passed\n";
    const auto withoutFooter=SS::Paginate({1,1,1},false,1,230,4,6,16,30,22,128,false);
    const auto withFooter=SS::Paginate({1,1,1},false,1,230,4,6,16,30,22,128,true);
    assert(withoutFooter.size()==1 && withFooter.size()==2);
    std::cout<<"SpectatorStyle: footer-free height budget passed\n";
    for (bool farEdge : {false,true}) for (float delta : {-1000.f,-50.f,0.f,50.f,1000.f})
    {
        const auto [pos,size]=SS::ResizeAxis(100.f,300.f,delta,farEdge,200.f,26.f,900.f);
        assert(pos>=26.f && pos+size<=900.f && size>=200.f);
        assert(farEdge ? pos==100.f : pos+size==400.f);
    }
    const auto [nearPos,nearSize]=SS::ResizeAxis(26.f,200.f,1000.f,false,200.f,26.f,900.f);
    assert(nearPos==26.f && nearSize==200.f);
    std::cout<<"SpectatorStyle: resize anchors, minimum size and taskbar boundaries passed\n";
}
