#include "../../Nikogram/src/Features/ImGui/TextStyle.h"
#include <cassert>
#include <iostream>
#include <string_view>
#include "../../Nikogram/src/Features/CritHack/CritIndicatorStyle.h"
#include "../../Nikogram/src/Features/Visuals/SpectatorList/SpectatorStyle.h"
int main()
{
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
