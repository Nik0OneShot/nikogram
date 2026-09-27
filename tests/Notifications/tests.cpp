#include "../../Nikogram/src/Features/ImGui/Notifications/NotificationStyle.h"
#include "../../Nikogram/src/Features/ImGui/Menu/BindLayout.h"
#include "../../Nikogram/src/Features/Ticks/TickIndicatorStyle.h"
#include <iostream>
#include <string_view>
#include <limits>
#include <stdexcept>
int main()
{
    using namespace NotificationStyle;int checks=0;
    auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("Notification style check failed");};
    check(Remaining(0,5,.2f)==1);check(Remaining(.2f,5,.2f)==1);
    check(std::abs(Remaining(2.7f,5,.2f)-.5f)<.0001f);
    check(Remaining(5.2f,5,.2f)==0);check(Remaining(100,5,.2f)==0);
    check(Remaining(0,0,0)==0);check(Remaining(0,-1,.2f)==0);
    check(Remaining(std::numeric_limits<float>::quiet_NaN(),5,.2f)==0);
    check(Filled(1,24)==24);check(Filled(.5f,24)==12);check(Filled(0,24)==0);
    check(Filled(-1,24)==0);check(Filled(2,24)==24);check(Filled(.01f,24)==1);
    for(float scale:{.5f,.75f,1.f,1.25f,1.5f,2.f,3.f})for(float width:{1.f,20.f,80.f,204.f,400.f,1000.f})
    {
        auto b=Layout(width,scale);check(b.count>=1&&b.count<=24);check(b.cell>0);
        check(std::abs(b.Left(0))<.001f);check(std::abs(b.Right(b.count-1)-width)<.002f);
        for(int i=1;i<b.count;++i)check(b.Left(i)>b.Right(i-1));
        int last=b.count;for(int tick=0;tick<=1000;++tick)
        {int n=Filled(Remaining(tick*.01f,5,.2f),b.count);check(n>=0&&n<=last);last=n;}
        check(last==0);
    }
    check(Layout(0,1).count==0);check(Layout(50,0).count==0);
    check(Layout(std::numeric_limits<float>::infinity(),1).count==0);
    check(BindLayout::Columns(0,true,1920,200,16)==0);
    check(BindLayout::Rows(0,0)==0);
    check(BindLayout::ClampAxis(-100,200,0,1920)==0);
    check(BindLayout::ClampAxis(1900,200,0,1920)==1720);
    check(BindLayout::ClampAxis(300,200,0,1920)==300);
    check(BindLayout::ClampAxis(1080,80,0,1054)==974);
    check(BindLayout::ClampAxis(-50,80,26,1080)==26);
    check(BindLayout::ClampAxis(-50,2000,0,1920)==0);
    for (float scale : {.85f,1.f,1.5f,2.f}) for (float pos : {-1000.f,0.f,100.f,1900.f,3000.f})
    {
        const float size=200*scale, bar=26*scale;
        const float x=BindLayout::ClampAxis(pos,size,0,1920);
        const float y=BindLayout::ClampAxis(pos,size,bar,1080-bar);
        check(x>=0 && x+size<=1920);
        check(y>=bar && y+size<=1080-bar);
    }
    for (int count : {1,2,3,10,100}) for (float width : {320.f,1280.f,1920.f,3440.f})
    for (float stride : {100.f,250.f,500.f}) for (bool horizontal : {false,true})
    {
        const int cols=BindLayout::Columns(count,horizontal,width,stride,16);
        const int rows=BindLayout::Rows(count,cols);
        check(cols>=1 && cols<=count);
        check(horizontal || cols==1);
        check(rows*cols>=count && (rows-1)*cols<count);
        check(cols==1 || cols*stride<=width-16);
        for(int i=0;i<count;++i) check(i/cols<rows && i%cols<cols);
    }
    using namespace TickIndicatorStyle;
    check(Charge(0,24,false)==0); check(Charge(12,24,false)==.5f); check(Charge(24,24,false)==1);
    check(Charge(99,24,false)==1); check(Charge(-1,24,false)==0);
    check(Charge(0,0,false)==0); check(Charge(24,24,true)==0);
    check(!Full(0,0,false)); check(!Full(23,24,false)); check(Full(24,24,false)); check(!Full(24,24,true));
    check(LabelBrightness(0)==.6f); check(LabelBrightness(1)==1.f);
    auto statusIs=[](const char* actual, const char* expected){ return std::string_view(actual)==expected; };
    check(statusIs(Status(24,24,false,false),"FULL"));
    check(statusIs(Status(99,24,false,false),"FULL"));
    check(statusIs(Status(12,24,false,false),"CHARGING"));
    check(statusIs(Status(1,24,false,false),"CHARGING"));
    check(statusIs(Status(0,24,false,false),"EMPTY"));
    check(statusIs(Status(24,24,false,true),"WARPING"));
    check(statusIs(Status(24,24,true,false),"SPEEDHACK"));
    check(statusIs(Status(0,0,false,false),"EMPTY"));
    float previous=0;
    for(int ticks=0;ticks<=24;++ticks)
    {
        const float brightness=LabelBrightness(Charge(ticks,24,false));
        check(brightness>=previous && brightness>=.6f && brightness<=1.f);
        check(Full(ticks,24,false)==(ticks==24)); previous=brightness;
    }
    std::cout<<"Notification, bind layout and tick style: "<<checks<<" checks passed\n";
}
