#pragma once
#include "PetMotion.h"
#include <array>

namespace PetMotion
{
    // One quiet conversation at a time. Slots keep a group of up to four close without
    // stacking sprites. Ordinary physics handles every hop and landing.
    class Conversation
    {
        uint32_t random=0xBC183D29;
        float Rand() { random^=random<<13;random^=random>>17;random^=random<<5;return (random&0xffffff)/float(0x1000000); }
        float wait=8,elapsed=0,nextHop=0,leftAtStart=0;
        int members[4]={},count=0,surface=-1,turn=0,rounds=0,hopsLeft=0,speaker=-1;
        float targets[4]={};
        bool talking=false;
    public:
        bool Active() const { return count>=2; }
        int Count() const { return count; }
        int Speaker() const { return speaker; }
        template<size_t N> void FaceSpeaker(std::array<Pet,N>& pets) const
        {
            if(!Active() || speaker<0)return;
            for(int i=0;i<count;i++)if(members[i]!=speaker)
                pets[members[i]].faceRight=pets[speaker].x>pets[members[i]].x;
        }
        template<size_t N> void Cancel(std::array<Pet,N>& pets)
        { for(auto& p:pets)p.social=false;count=0;hopsLeft=0;speaker=-1;talking=false;wait=12+Rand()*16; }
        template<size_t N> void Update(float dt,std::array<Pet,N>& pets,const std::array<bool,N>& enabled,const World& w,const Options& o,bool allowed)
        {
            static_assert(N>=2 && N<=4,"Conversation supports two to four pet slots");
            if(!allowed || w.top || !o.jump) { if(Active())Cancel(pets);return; }
            if(!Active())
            {
                wait-=dt;if(wait>0)return;wait=8+Rand()*12;
                const int first=int(Rand()*N);
                for(int offset=0;offset<int(N) && count<2;offset++)
                {
                    int a=(first+offset)%int(N);
                    if(!enabled[a] || !pets[a].active || !pets[a].Grounded() || pets[a].age>=o.rest-8)continue;
                    surface=pets[a].support;count=1;members[0]=a;
                    for(int j=1;j<int(N);j++)
                    {
                        int b=(a+j)%int(N);
                        if(enabled[b] && pets[b].active && pets[b].Grounded() && pets[b].support==surface && pets[b].age<o.rest-8 && std::abs(pets[b].x-pets[a].x)<o.size*5 && std::abs(pets[b].y-pets[a].y)<2)
                            members[count++]=b;
                    }
                    if(count<2)count=0;
                }
                if(count<2)return;
                float left=0,right=w.barWidth;
                if(surface>=0)for(const auto& r:w.windows)if(r.id==surface){left=r.left;right=r.right;}
                const float spacing=o.size*.72f,half=o.size*.44f;
                if(right-left<spacing*(count-1)+2*half){Cancel(pets);return;}
                const float start=std::clamp(pets[members[0]].x-spacing*(count-1)*.5f,left+half,right-half-spacing*(count-1));
                for(int i=0;i<count;i++){targets[i]=start+i*spacing;pets[members[i]].social=true;}
                leftAtStart=left;elapsed=0;talking=false;turn=0;rounds=2+int(Rand()*3);hopsLeft=0;nextHop=.35f;
            }
            elapsed+=dt;
            float shift=0;
            if(surface>=0)
            {
                bool found=false;for(const auto& r:w.windows)if(r.id==surface){shift=r.left-leftAtStart;found=true;break;}
                if(!found){Cancel(pets);return;}
            }
            bool arrived=true;
            for(int i=0;i<count;i++)
            {
                auto& p=pets[members[i]];
                if(!enabled[members[i]] || !p.active || p.support!=surface || p.age>=o.rest || (!p.Grounded() && p.mode!=Mode::Jump)) {Cancel(pets);return;}
                p.socialTarget=targets[i]+shift;
                arrived &= p.Grounded() && std::abs(p.x-p.socialTarget)<2;
            }
            if(elapsed>(talking?45.f:20.f)){Cancel(pets);return;}
            if(!talking){if(!arrived)return;talking=true;elapsed=0;}
            FaceSpeaker(pets);
            nextHop-=dt;
            if(nextHop<=0 && arrived)
            {
                if(turn>=rounds*count){Cancel(pets);return;}
                if(hopsLeft==0)hopsLeft=1+int(Rand()*3);
                speaker=members[turn%count];pets[speaker].SocialHop(o);--hopsLeft;FaceSpeaker(pets);
                if(hopsLeft>0)nextHop=.4f;
                else {++turn;nextHop=turn%count==0?1.f:.65f;}
            }
        }
    };
}
