#pragma once
#include "PetMotion.h"
#include <array>

namespace PetMotion
{
    enum class GamePhase { Idle, Follow, Sneak, ScareLeap, Tease, Recoil, Laugh, AwareReply, Sheepish,
        CountIn, Chase, Gather, ClosingTalk };
    class Games
    {
        uint32_t random=0x561FA329;
        float Rand(){random^=random<<13;random^=random>>17;random^=random<<5;return (random&0xffffff)/float(0x1000000);}
        std::array<bool,4> participants={},caught={};
        std::array<float,4> evade={};
        float timer=0,total=0,hopWait=0,followFor=0,chaseTime=0,trick=0,home=0,talkFor=0;
        int actor=-1,victim=-1,round=0,hops=0,reply=0,burst=0,lock=-1;
        bool aware=false,firstScare=true,firstChase=true,runPast=false,manual=false;
        GamePhase phase=GamePhase::Idle;
        float cooldown=0;
        void Phase(GamePhase next){phase=next;timer=0;hopWait=0;hops=0;}
        static bool Ready(const Pet& p){return p.active && (p.Grounded() || p.mode==Mode::Sit || p.mode==Mode::Sleep);}
        static float Feet(const Pet& p,const Options& o){return p.y+((p.mode==Mode::UnderHang || p.mode==Mode::Hang)?o.size*.75f:0);}
        void Navigate(Pet& p,float x,float y,int support,const World& w,const Options& o,float speed)
        {
            p.GameWalk(x,speed);
            if(p.mode==Mode::Climb){if(p.support!=support || y>Feet(p,o)+o.size*.3f)p.GameDescend();else p.GameAscend();return;}
            if(p.mode==Mode::UnderHang){if(support!=p.support)p.GameDrop(o);else p.faceRight=x>p.x;return;}
            if(!p.Grounded())return;
            if(p.support==support)return;
            if(p.support>=0){p.GameLeave(w,o);return;}
            if(support>=0)for(const auto& r:w.windows)if(r.id==support){p.GameWindow(r,o);break;}
        }
        bool Hop(Pet& p,int count,float spacing,const Options& o)
        {
            p.GameWalk(p.x,0);
            if(!p.Grounded())return false;
            if(hops>=count)return true;
            if(hopWait<=0){p.SocialHop(o);++hops;hopWait=spacing;}
            return false;
        }
        void BeginScare(std::array<Pet,4>& pets,const std::array<bool,4>& eligible,int selectedActor=-1,int selectedVictim=-1)
        {
            int list[4],n=0;for(int i=0;i<4;i++)if(eligible[i])list[n++]=i;
            actor=firstScare && eligible[1]?1:list[int(Rand()*n)];
            victim=firstScare && eligible[0] && actor!=0?0:-1;
            if(victim<0){do{victim=list[int(Rand()*n)];}while(victim==actor);}
            if(selectedActor>=0){actor=selectedActor;victim=selectedVictim;}
            firstScare=false;participants={};participants[actor]=participants[victim]=true;
            for(int i=0;i<4;i++)if(participants[i]){pets[i].game=true;pets[i].social=false;pets[i].age=0;pets[i].mode=Mode::Stand;}
            round=0;aware=false;followFor=6+Rand()*6;Phase(GamePhase::Follow);total=0;
        }
        void NextScareRound(std::array<Pet,4>& pets)
        {
            if(round==1){Finish(pets);return;}
            ++round;std::swap(actor,victim);aware=Rand()<.5f;followFor=5+Rand()*5;Phase(GamePhase::Follow);
        }
        void BeginChase(std::array<Pet,4>& pets,const std::array<bool,4>& eligible,const World& w,const Options& o,int selectedActor=-1)
        {
            participants=eligible;caught={};evade={};int list[4],n=0;
            for(int i=0;i<4;i++)if(eligible[i]){list[n++]=i;pets[i].game=true;pets[i].social=false;pets[i].age=0;pets[i].mode=Mode::Stand;}
            actor=firstChase && eligible[0]?0:list[int(Rand()*n)];firstChase=false;
            if(selectedActor>=0)actor=selectedActor;
            home=Rand()<.5f?o.size*.6f:w.barWidth-o.size*.6f;victim=-1;lock=-1;trick=0;chaseTime=0;
            total=0;Phase(GamePhase::CountIn);
        }
    public:
        struct Metrics { int counts=0,catches=0,dodges=0,intercepts=0,scareGames=0,chaseGames=0,awareReplies=0; } metrics;
        void Seed(uint32_t seed){random=seed?seed:1;}
        bool Active()const{return phase!=GamePhase::Idle;}
        GamePhase State()const{return phase;}
        int Actor()const{return actor;}
        int Victim()const{return victim;}
        int Round()const{return round;}
        bool Aware()const{return aware;}
        float Cooldown()const{return cooldown;}
        float ChaseAge()const{return chaseTime;}
        bool Caught(int i)const{return caught[i];}
        bool IsScare()const{return phase>=GamePhase::Follow && phase<=GamePhase::Sheepish;}
        const char* ManualReason(bool chase,int selectedActor,int selectedVictim,const std::array<Pet,4>& pets,
            const std::array<bool,4>& enabled,const World& w,const Options& o)const
        {
            if(Active())return "Stop the current game first.";
            if(w.top)return "Requires the bottom taskbar.";
            if(!o.jump)return "Enable jumping first.";
            if(chase && !o.run)return "Enable running first.";
            int n=0;for(bool e:enabled)if(e)++n;
            if(n<(chase?3:2))return chase?"Enable at least three pets.":"Enable at least two pets.";
            if(selectedActor<0 || selectedActor>=4 || !enabled[selectedActor])return "Choose an enabled pet for the lead role.";
            if(!chase && (selectedVictim<0 || selectedVictim>=4 || !enabled[selectedVictim] || selectedActor==selectedVictim))return "Choose a different enabled pet as scaree.";
            for(int i=0;i<4;i++)if(enabled[i] && (chase || i==selectedActor || i==selectedVictim) && !Ready(pets[i]))return "Waiting for participants to land or finish climbing.";
            if(w.barWidth<o.size*((chase?n:2)*.72f+.2f))return "Not enough taskbar space.";
            return nullptr;
        }
        bool StartManual(bool chase,int selectedActor,int selectedVictim,std::array<Pet,4>& pets,
            const std::array<bool,4>& enabled,const World& w,const Options& o)
        {
            if(ManualReason(chase,selectedActor,selectedVictim,pets,enabled,w,o))return false;
            if(chase){BeginChase(pets,enabled,w,o,selectedActor);++metrics.chaseGames;}
            else{BeginScare(pets,enabled,selectedActor,selectedVictim);++metrics.scareGames;}
            manual=true;return true;
        }
        void Finish(std::array<Pet,4>& pets)
        {
            for(auto& p:pets){p.game=false;p.gameSit=false;p.age=0;}
            participants={};phase=GamePhase::Idle;cooldown=120;manual=false;
        }
        void FaceSpeaker(std::array<Pet,4>& pets)const
        {
            if(phase!=GamePhase::ClosingTalk || victim<0)return;
            for(int i=0;i<4;i++)if(participants[i] && i!=victim)pets[i].faceRight=pets[victim].x>pets[i].x;
        }
        void Update(float dt,float clockDt,double uptime,bool open,std::array<Pet,4>& pets,
            const std::array<bool,4>& enabled,const World& w,const Options& o,bool scares,bool chases,bool conversation)
        {
            if(!open)return; // Grace is absolute uptime; everything else pauses.
            if(!Active())
            {
                cooldown=std::max(0.f,cooldown-clockDt);
                if(uptime<50 || cooldown>0 || w.top || !o.jump || conversation)return;
                std::array<bool,4> eligible={};int n=0;
                for(int i=0;i<4;i++){eligible[i]=enabled[i] && Ready(pets[i]);if(eligible[i])++n;}
                if(n<2 || w.barWidth<o.size*(n*.72f+.2f))return;
                const bool chaseOK=chases && o.run && n>=3;
                if(scares && (!chaseOK || Rand()<.5f)){BeginScare(pets,eligible);++metrics.scareGames;}
                else if(chaseOK){BeginChase(pets,eligible,w,o);++metrics.chaseGames;}
                else return;
            }
            if(w.top || !o.jump || (!IsScare() && !o.run) || (!manual && (IsScare()?!scares:!chases))){Finish(pets);return;}
            for(int i=0;i<4;i++)if(participants[i] && (!enabled[i] || !pets[i].active || pets[i].mode==Mode::Drag)){Finish(pets);return;}
            timer+=dt;total+=dt;hopWait-=dt;
            // Safety exit for unreachable geometry, never fabricate a capture.
            if(total>(IsScare()?120.f:240.f)){Finish(pets);return;}
            const float scale=o.size/64, floor=w.height-w.barHeight, half=o.size*.44f;
            if(IsScare())
            {
                auto& a=pets[actor];auto& b=pets[victim];
                if(phase==GamePhase::Follow)
                {
                    if(std::abs(b.gameTarget-b.x)<3 || timer<dt*2)b.GameWalk(std::clamp(b.x+(b.faceRight?1.f:-1.f)*o.size*2,half,w.barWidth-half),22);
                    const float behind=b.x+(b.faceRight?-1.f:1.f)*o.size*1.2f;
                    Navigate(a,behind,Feet(b,o),b.support,w,o,42);
                    if(timer>=followFor && a.Grounded() && b.Grounded() && a.support==b.support && std::abs(a.x-b.x)<o.size*2){b.GameWalk(b.x,0);Phase(GamePhase::Sneak);}
                }
                else if(phase==GamePhase::Sneak)
                {
                    const float behind=b.x+(b.faceRight?-1.f:1.f)*o.size*.6f;
                    Navigate(a,behind,b.y,b.support,w,o,32);
                    if(a.Grounded() && b.Grounded() && a.support==b.support && std::abs(a.x-behind)<o.size)
                    {
                        const float duration=2*std::sqrt(2*45.f*scale/(850.f*scale));
                        a.GameJump((behind-a.x)/duration*1.2f,45*scale,o);Phase(GamePhase::ScareLeap);
                        if(aware)b.faceRight=a.x>b.x;
                    }
                }
                else if(phase==GamePhase::ScareLeap)
                {
                    if(aware)b.faceRight=a.x>b.x;
                    if(a.Grounded()){a.GameWalk(a.x,0);Phase(aware?GamePhase::AwareReply:GamePhase::Tease);if(aware)++metrics.awareReplies;}
                }
                else if(phase==GamePhase::Tease){if(Hop(a,2,.32f,o)){b.GameJump((b.faceRight?-1.f:1.f)*100*scale,32*scale,o);Phase(GamePhase::Recoil);}}
                else if(phase==GamePhase::Recoil){if(b.Grounded()){b.GameWalk(b.x,0);b.faceRight=a.x>b.x;Phase(GamePhase::Laugh);}}
                else if(phase==GamePhase::Laugh){if(Hop(a,4,.32f,o))NextScareRound(pets);}
                else if(phase==GamePhase::AwareReply){b.faceRight=a.x>b.x;if(Hop(b,3,.38f,o))Phase(GamePhase::Sheepish);}
                else if(phase==GamePhase::Sheepish){if(Hop(a,1,.4f,o))NextScareRound(pets);}
                return;
            }
            auto& chaser=pets[actor];
            if(phase==GamePhase::CountIn)
            {
                Navigate(chaser,home,floor,-1,w,o,65);
                if(chaser.Grounded() && chaser.support<0 && std::abs(chaser.x-home)<3)
                {
                    int old=hops;if(Hop(chaser,10,1.2f,o)){Phase(GamePhase::Chase);lock=-1;}metrics.counts+=hops>old?1:0;
                }
            }
            if(phase==GamePhase::CountIn || phase==GamePhase::Chase)
            {
                if(phase==GamePhase::Chase)chaseTime+=dt;
                const float clever=std::clamp(chaseTime/40.f,0.f,1.f);
                for(int i=0;i<4;i++)if(participants[i] && i!=actor)
                {
                    auto& p=pets[i];evade[i]-=dt;
                    if(caught[i])
                    {
                        const float bench=home<w.barWidth*.5f?w.barWidth-half-i*o.size*.6f:half+i*o.size*.6f;
                        Navigate(p,bench,floor,-1,w,o,100);
                        if(p.support<0 && (p.Grounded() || p.mode==Mode::Sit) && std::abs(p.x-bench)<4)p.gameSit=true;
                        continue;
                    }
                    if(p.mode==Mode::Climb && chaser.mode==Mode::Climb && p.support==chaser.support && chaser.Descending() && chaser.y<p.y+o.size){p.GameDrop(o);evade[i]=2;}
                    if(p.Grounded())
                    {
                        const float away=p.x>=chaser.x?1.f:-1.f;
                        float goal=away>0?w.barWidth-half:half;
                        if(std::abs(p.x-goal)<o.size*.3f)goal=away>0?half:w.barWidth-half;
                        p.GameWalk(goal,115);
                        if(evade[i]<=0 && phase==GamePhase::Chase && chaser.support==p.support && std::abs(p.x-chaser.x)<o.size*1.4f)
                        {
                            p.GameJump(-away*210*scale,60*scale,o);evade[i]=2; ++metrics.dodges;
                            if(chaser.Grounded() && Rand()<clever*.85f)
                            {chaser.GameJump((p.x-chaser.x)*2.f+p.vx*.45f,55*scale,o);++metrics.intercepts;}
                            else {trick=Rand()<.5f?.7f:1.2f;runPast=Rand()<.5f;lock=runPast?-1:i;}
                        }
                        else if(evade[i]<=0 && o.climb)
                        {
                            evade[i]=2+Rand()*3;
                            for(const auto& r:w.windows)if(r.id!=p.support && std::abs((r.left+r.right)*.5f-p.x)<o.size*3 && p.GameWindow(r,o))break;
                        }
                    }
                }
                if(phase==GamePhase::CountIn)return;
                int nearest=-1;float distance=1e9f;int remaining=0;
                for(int i=0;i<4;i++)if(participants[i] && i!=actor && !caught[i])
                {++remaining;float d=std::hypot(pets[i].x-chaser.x,Feet(pets[i],o)-Feet(chaser,o));if(d<distance){distance=d;nearest=i;}}
                if(!remaining){Phase(GamePhase::Gather);return;}
                if(lock>=0 && !caught[lock])nearest=lock;
                victim=nearest;auto& target=pets[nearest];
                if(trick>0)
                {trick-=dt;chaser.GameWalk(runPast?chaser.x+(chaser.faceRight?1.f:-1.f)*o.size:chaser.x,runPast?150:0);}
                else
                {
                    lock=-1;
                    float predicted=target.x+(target.mode==Mode::Jump?target.vx*.18f*clever:0);
                    // Experienced chasers briefly stop to bait a nearby leap.
                    if(clever>.5f && chaser.Grounded() && target.Grounded() && distance<o.size*1.8f && Rand()<dt*.35f){trick=.35f;runPast=false;lock=nearest;}
                    Navigate(chaser,predicted,Feet(target,o),target.support,w,o,125+clever*65);
                }
                for(int i=0;i<4;i++)if(participants[i] && i!=actor && !caught[i])
                {
                    const auto& p=pets[i];
                    if(std::abs(p.x-chaser.x)<o.size*.28f && std::abs(Feet(p,o)-Feet(chaser,o))<o.size*.28f)
                    {caught[i]=true;++metrics.catches;if(lock==i)lock=-1;}
                }
                return;
            }
            if(phase==GamePhase::Gather)
            {
                int n=0;for(bool b:participants)if(b)++n;int slot=0;bool ready=true;
                for(int i=0;i<4;i++)if(participants[i])
                {
                    float goal=w.barWidth*.5f+(slot++-(n-1)*.5f)*o.size*.72f;
                    Navigate(pets[i],goal,floor,-1,w,o,90);
                    ready &= pets[i].Grounded() && pets[i].support<0 && std::abs(pets[i].x-goal)<3;
                }
                if(ready){talkFor=5+Rand()*10;reply=0;burst=0;victim=-1;Phase(GamePhase::ClosingTalk);}
                return;
            }
            if(phase==GamePhase::ClosingTalk)
            {
                if(timer>=talkFor){Finish(pets);return;}
                if(victim<0){while(!participants[reply%4])++reply;victim=reply%4;burst=1+int(Rand()*3);hops=0;}
                FaceSpeaker(pets);
                if(Hop(pets[victim],burst,.4f,o)){++reply;victim=-1;hopWait=.65f;hops=0;}
            }
        }
    };
}
