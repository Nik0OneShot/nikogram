#include "../../Nikogram/src/Features/Blockbot/Model.h"
#include "../../Nikogram/src/Features/Blockbot/Hazards.h"
#include <cstdlib>
#include <iostream>
using namespace BlockbotModel;
int checks=0;
void Check(bool b,const char* what) { ++checks; if(!b) { std::cerr<<"FAIL: "<<what<<'\n'; std::exit(1); } }
bool Near(float a,float b) { return std::abs(a-b)<.001f; }
int main()
{
    Override pause;
    Check(!pause.Paused(10,false),"starts ready");
    Check(pause.Paused(10,true),"input yields immediately");
    Check(pause.Paused(10.49999,false),"full 500ms pause");
    Check(!pause.Paused(10.5,false),"resumes at 500ms");
    for (int i=0;i<100;++i) Check(pause.Paused(11+i*.1,true),"held input keeps extending");
    Check(pause.Paused(21.399,false),"pause extends from final held frame");
    Check(!pause.Paused(21.401,false),"released input resumes");
    Override custom;
    Check(custom.Paused(30,true,0),"zero delay still respects held input");
    Check(!custom.Paused(30,false,0),"zero delay resumes immediately on release");
    Check(custom.Paused(31,true,1),"one second delay starts");
    Check(custom.Paused(31.999,false,1),"one second delay remains active");
    Check(!custom.Paused(32,false,1),"one second boundary");
    Check(custom.Paused(40,true,5),"manual delay above slider accepted");
    Check(custom.Paused(44.999,false,5),"manual delay not capped at one second");
    Check(!custom.Paused(45,false,5),"extended delay ends");
    Check(!custom.Paused(40.1,false,0),"changing delay while paused takes effect");
    Check(!custom.Paused(40,false,-1),"negative delay becomes zero");
    Check(custom.Paused(40.25,false,std::numeric_limits<double>::quiet_NaN()),"invalid delay falls back to default");
    Check(!custom.Paused(40.5,false,std::numeric_limits<double>::infinity()),"infinite delay does not lock control");
    Check(!UseProjectileIntercept(0),"stand in front bypasses projectile estimation");
    Check(UseProjectileIntercept(1),"obstruct projectiles enables projectile estimation");
    Check(!UseProjectileIntercept(2),"invalid teammate mode uses conservative front fallback");
    for(int bits=0;bits<8;++bits)
        Check(PermitDrop(bits&1,bits&2,bits&4)==(bits==7),"drop requires map data, landing and clear hazard path");
    Check(Near(SupportHeight({0,0,1},10,50,80),10),"flat support plane");
    Check(Near(SupportHeight({-.6f,0,.8f},0,32,0),24),"ramp corner support follows actual slope");
    Check(Near(SupportHeight({-.6f,0,.8f},0,-32,0),-24),"downhill ramp corner is not a fake cliff");
    std::vector<BlockbotHazards::Entity> entities;
    Check(BlockbotHazards::Parse("{\"classname\" \"worldspawn\"}\n{\"classname\" \"trigger_hurt\" \"model\" \"*2\"}",entities),"map entity parsing");
    Check(entities.size()==2&&BlockbotHazards::Dangerous(entities[1]),"hurt trigger recognized");
    Check(!BlockbotHazards::Dangerous(entities[0]),"ordinary world entity is not a trigger");
    Check(BlockbotHazards::Dangerous({{"classname","trigger_multiple"},{"OnStartTouch","!activator,SetHealth,0,0,-1"}}),"kill output treated as hazardous");
    Check(BlockbotHazards::Dangerous({{"classname","trigger_teleport"}}),"unknown teleport destination treated conservatively");
    Check(!BlockbotHazards::Parse("{\"classname\" \"trigger_hurt\"",entities),"truncated hazard map rejected");
    Check(!BlockbotHazards::Parse("{\"classname\" \"worldspawn\"} !",entities),"trailing malformed entity rejected");
    Check(BlockbotHazards::Parse("// comment\n{\"classname\" \"worldspawn\"}\n ",entities),"map comments and trailing whitespace");
    BlockbotHazards::Box kill{{-100,-100,-50},{100,100,-40}};
    Check(BlockbotHazards::Intersects({0,0,0},{0,0,-100},kill,{-24,-24,0},{24,24,82}),"death plane crossed during drop");
    Check(!BlockbotHazards::Intersects({200,0,0},{200,0,-100},kill,{-24,-24,0},{24,24,82}),"drop outside hazard allowed");
    Check(BlockbotHazards::Intersects({110,0,0},{110,0,-100},kill,{-24,-24,0},{24,24,82}),"player hull grazing hazard rejected");
    auto navFloor=[](Point p,Point& out){out=p;out.z=0;return true;};
    auto corridor=[](Point a,Point b)
    {
        for(int i=0;i<=100;++i)
        {
            float t=i/100.f,x=a.x+(b.x-a.x)*t,y=a.y+(b.y-a.y)*t;
            bool left=std::abs(x)<=15&&y>=-1&&y<=143;
            bool bridge=x>=-15&&x<=175&&std::abs(y-128)<=15;
            bool right=std::abs(x-160)<=15&&y>=-1&&y<=143;
            if(!(left||bridge||right))return false;
        }
        return true;
    };
    Point navNext;
    Check(Navigate({}, {160,0,0},navFloor,corridor,navNext),"multi-turn U corridor route found");
    Check(navNext.y>0&&Near(navNext.x,0),"route can initially move away from goal");
    Check(corridor({},navNext),"first corner waypoint is traversable");
    Check(!Navigate({}, {160,0,0},navFloor,[](Point,Point){return false;},navNext),"blocked navigation fails closed");
    Check(Navigate({}, {1000,0,0},navFloor,[](Point,Point){return true;},navNext),"far target gets bounded local progress route");
    Check(Near(PursuitVelocity({}, {32,0,0},{500,0,0},{},400).x,400),"short follow waypoint no longer caps pursuit at 192");
    Check(Near(PursuitVelocity({}, {500,0,0},{500,0,0},{},400).x,400),"ground pursuit uses full class speed above 300");
    Check(Near(PursuitVelocity({}, {32,0,0},{500,0,0},{},230).x,230),"slower class retains actual speed limit");
    Check(Near(PursuitVelocity({}, {5,0,0},{5,0,0},{},400).x,30),"slow down at final blocking position");
    Check(Near(PursuitVelocity({}, {}, {}, {100,0,0},400).x,100),"head center matches target velocity");
    Check(Near(PursuitVelocity({}, {}, {}, {},400).x,0),"stationary final target stops");
    Check(Near(PursuitVelocity({}, {0,32,0},{500,0,0},{},400).y,400),"detour keeps steering direction at pursuit speed");
    Check(Near(PursuitVelocity({}, {32,0,0},{500,0,0},{},800).x,450),"command limit remains enforced");
    Check(Near(PursuitVelocity({}, {32,0,0},{500,0,0},{},0).x,0),"immobile player is not forced to move");
    for (float speed : {230.f,300.f,400.f,450.f})
    {
        Point here{10,20,0},end{15,28,0},feed{30,20,0};
        Check(Distance(PursuitVelocity(here,end,end,feed,speed),Velocity(here,end,feed,speed))<.001f,"near-target controller preserved");
    }
    Check(CanTraverse(Terrain::Safe,false),"safe terrain allowed normally");
    Check(!CanTraverse(Terrain::Recoverable,false),"lower landing still counts as danger");
    Check(!CanTraverse(Terrain::Dangerous,false),"danger rejected by default");
    Check(CanTraverse(Terrain::Recoverable,true),"ignore danger permits recovery landing");
    Check(CanTraverse(Terrain::Dangerous,true),"ignore danger permits unsupported route");
    Check(!CanTraverse(Terrain::Blocked,true),"ignore danger never bypasses solid obstacle");
    Check(!CanTraverse(static_cast<Terrain>(99),true),"unknown terrain fails closed");
    Check(RecoveryScore(100,Terrain::Safe)<RecoveryScore(50,Terrain::Recoverable),"supported route preferred");
    Check(RecoveryScore(100,Terrain::Recoverable)<RecoveryScore(50,Terrain::Dangerous),"supported landing preferred over void");
    Check(RetainTarget(true,0,0,10,123,10,123),"following retains identity");
    Check(!RetainTarget(false,0,0,10,123,10,123),"following off preserves closest selection");
    Check(!RetainTarget(true,1,0,10,123,10,123),"selection mode change releases retained target");
    Check(!RetainTarget(true,0,0,10,456,10,123),"reused user ID does not retain another account");
    Check(!RetainTarget(true,0,0,11,123,10,123),"reconnected target requires reacquisition");
    Check(!Eligible(0,0,700,600),"acquisition range stays enforced for new target");
    Check(Eligible(0,0,700,std::numeric_limits<float>::max()),"retained follow target can leave acquisition range");
    Check(!Eligible(0,-1,700,std::numeric_limits<float>::max()),"retention never bypasses ignored priority");
    Check(RecoverHead({30,0,90},{0,0,0},82),"airborne near head can steer back");
    Check(!RecoverHead({30,0,50},{0,0,0},82),"below head cannot promise head recovery");
    Check(!RecoverHead({200,0,90},{0,0,0},82),"head recovery bounded horizontally");
    Check(!RecoverHead({30,0,300},{0,0,0},82),"head recovery bounded vertically");
    for (int bits=0;bits<16;++bits)
    {
        bool moving=bits&1,jumping=bits&2,duck=bits&4,allow=bits&8;
        Check(ManualOverride(moving,jumping,duck,allow)==(moving||jumping||(duck&&!allow)),"crouch option never suppresses movement/jump override");
    }
    for (int bits=0;bits<32;++bits)
    {
        bool menu=bits&1,allow=bits&2,game=bits&4,cursor=bits&8,focus=bits&16;
        Check(UIBlocked(menu,allow,game,cursor,focus)==(!focus||game||((menu||cursor)&&!(menu&&allow))),"UI option respects focus and engine UI");
    }
    Check(Near(Response({100,0,0},{},100,100).x,100),"default acceleration unchanged");
    Check(Near(Response({100,0,0},{},50,100).x,50),"gentler acceleration");
    Check(Near(Response({100,0,0},{},150,100).x,150),"stronger acceleration");
    Check(Near(Response({0,0,0},{100,0,0},100,100).x,0),"default deceleration unchanged");
    Check(Near(Response({0,0,0},{100,0,0},100,50).x,50),"gentler deceleration");
    Check(Near(Response({0,0,0},{100,0,0},100,150).x,-50),"stronger braking opposes inertia");
    Check(Near(Response({-100,0,0},{100,0,0},50,150).x,-200),"reversal uses deceleration response");
    Check(Near(Response({100,0,0},{100,0,0},200,200).x,100),"matching target speed stays stable");
    Check(Near(Response({100,0,0},{},0,100).x,25),"response minimum defended at runtime");
    Check(Near(Response({100,0,0},{},1000,100).x,200),"response maximum defended at runtime");
    Check(Near(Response({100,0,0},{},std::numeric_limits<float>::quiet_NaN(),100).x,100),"invalid response uses original behavior");
    Check(Near(Response({300,300,0},{},200,100).x,std::sqrt(.5f)*450),"tuned diagonal command capped");
    for(int local=0;local<5;++local) for(int target=0;target<5;++target) for(int mode=-1;mode<4;++mode)
    {
        bool valid=(local==2||local==3)&&(target==2||target==3);
        bool expected=valid&&(mode==2||(mode==0&&local!=target)||(mode==1&&local==target));
        Check(TeamAllowed(mode,local,target)==expected,"team filters, enemies default");
    }
    Check(!Eligible(1,0,100,600),"priority zero excluded");
    Check(Eligible(1,1,100,600),"priority one included");
    Check(!Eligible(0,-1,100,600),"automatic ignores ignored players");
    Check(Eligible(2,-1,100,600),"explicit manual choice allowed");
    Check(!Eligible(2,1,601,600),"manual range enforced");
    Check(!Eligible(0,1,std::numeric_limits<float>::quiet_NaN(),600),"nonfinite target rejected");
    Check(Better(1,3,500,2,10),"highest priority first");
    Check(Better(1,2,50,2,100),"priority ties use distance");
    Check(!Better(1,1,10,2,500),"near lower priority loses");
    Check(Better(0,0,50,5,100),"closest ignores rank");
    Check(ManualMatches(3,123,3,123),"stable manual identity");
    Check(!ManualMatches(3,123,3,456),"reused slot/account mismatch rejected");
    Check(!ManualMatches(3,123,4,123),"reconnect requires selection again");
    Check(!ManualMatches(0,0,0,0),"unset identity never matches");
    Check(ManualMatches(4,0,4,0),"bots keyed by session user ID");
    for(int yaw=-720;yaw<=720;++yaw)
    {
        Point world=Velocity({0,0,0},{100,30,0},{10,-20,0},300);
        Point command=Command(world,float(yaw));
        Check(Finite(command)&&std::hypot(command.x,command.y)<=300.001f,"finite capped movement");
        float a=yaw*.017453292519943295f;
        Point restored{command.x*std::cos(a)+command.y*std::sin(a),command.x*std::sin(a)-command.y*std::cos(a),0};
        Check(Distance(restored,world)<.001f,"yaw conversion preserves world movement");
    }
    Check(Near(Command({0,100,0},0).y,-100),"positive world Y is negative side at yaw zero");
    Check(Near(Velocity({}, {}, {50,0,0},300).x,50),"feedforward follows moving platform");
    Check(Near(Velocity({}, {1,0,0}, {},300).x,6),"slows down near goal");
    Check(Near(Velocity({}, {100,0,0}, {},-5).x,0),"negative speed fails closed");
    Check(Near(Command({},std::numeric_limits<float>::quiet_NaN()).x,0),"invalid yaw fails closed");
    Check(OnHead({0,0,82},{0,0,0},{-24,-24,0},{24,24,82}),"standing on head");
    Check(!OnHead({0,0,0},{0,0,0},{-24,-24,0},{24,24,82}),"ground is not head");
    Check(!OnHead({23,0,82},{0,0,0},{-24,-24,0},{24,24,82}),"unsafe head edge excluded");
    Check(Near(HullSpacing({1,0,0},{48,48,82}),52),"axis hull spacing");
    Check(HullSpacing({.70710678f,.70710678f,0},{48,48,82})>71,"diagonal hull spacing avoids overlap");
    Point next;
    Check(Route({0,0,0},{200,0,0},[](Point,Point){return true;},next)&&Near(next.x,200),"direct route");
    Check(!Route({0,0,0},{200,0,0},[](Point,Point){return false;},next),"no safe route stops");
    auto wall=[](Point a,Point b)
    {
        for(int i=0;i<=100;++i)
        {
            float f=i/100.f,x=a.x+(b.x-a.x)*f,y=a.y+(b.y-a.y)*f;
            if(x>80&&x<120&&std::abs(y)<35)return false;
        }
        return true;
    };
    Check(Route({0,0,0},{200,0,0},wall,next)&&std::abs(next.y)>35,"detour around obstacle");
    Check(wall({},next)&&wall(next,{200,0,0}),"both detour legs validated");
    auto pit=[](Point a,Point b){return !(std::min(a.x,b.x)<110&&std::max(a.x,b.x)>90);};
    Check(!Route({0,0,0},{200,0,0},pit,next),"unbridgeable pit stops");
    int budget=1;
    Check(!Route({0,0,0},{200,0,0},[&](Point,Point){return --budget>0;},next),"exhausted route budget fails closed");
    Check(!Route({std::numeric_limits<float>::infinity(),0,0},{200,0,0},wall,next),"nonfinite path rejected");
    auto flat=[](Point p,Point& out){out={p.x,p.y,0};return true;};
    auto clear=[](Point,Point){return true;};
    Check(WalkSegment({}, {200,0,0},flat,clear),"grounded walk");
    Check(!WalkSegment({}, {200,0,0},flat,[](Point,Point){return false;}),"hull obstruction stops movement");
    Check(!WalkSegment({}, {200,0,0},[](Point p,Point& out){out=p;return !(p.x>40&&p.x<80);},clear),"pit between endpoints rejected");
    Check(!WalkSegment({}, {200,0,-40},[](Point p,Point& out){out={p.x,p.y,p.x>40?-40.f:0.f};return true;},clear),"unsafe drop rejected");
    Check(!WalkSegment({}, {200,0,40},[](Point p,Point& out){out={p.x,p.y,p.x>40?40.f:0.f};return true;},clear),"unclimbable step rejected");
    auto stair=[](Point p,Point& out){out={p.x,p.y,p.x>40?16.f:0.f};return true;};
    Check(WalkSegment({}, {200,0,16},stair,clear),"small supported stair allowed");
    int calls=0;
    Check(WalkSegment({}, {16,0,0},flat,[&](Point,Point){return ++calls>1;})&&calls==4,"step-up checks rise, across, descent");
    calls=0;
    Check(!WalkSegment({}, {16,0,0},flat,[&](Point,Point){++calls;return calls>2;}),"blocked overhead step-up rejected");
    Check(!WalkSegment({}, {2000,0,0},flat,clear),"local search bounded");
    Check(!WalkSegment({}, {20,0,200},flat,clear),"unreachable elevation rejected");
    Point entry;
    Check(DropEntry({0,0,100},{0,0,0},[](Point p){return p.x==0&&p.y==0;},entry)
        && entry.x==0 && entry.y==0 && entry.z==100,"vertically aligned drop needs no horizontal progress");
    Check(DropEntry({0,0,100},{60,0,0},[](Point p){return p.x==68&&p.y==8;},entry)
        && entry.x==68 && entry.y==8,"fine opening alignment missed by coarse spokes");
    Check(!DropEntry({0,0,100},{60,0,0},[](Point){return false;},entry),"blocked or unsafe openings refused");
    Check(!DropEntry({0,0,100},{0,0,90},[](Point){return true;},entry),"drop search only for lower targets");
    Check(!DropEntry({0,0,100},{300,0,0},[](Point){return true;},entry),"drop entry search stays local");
    Check(!DropEntry({0,0,100},{NAN,0,0},[](Point){return true;},entry),"nonfinite drop target rejected");
    std::cout<<"Blockbot: "<<checks<<" checks passed\n";
}
