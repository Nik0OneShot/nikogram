#pragma once
#include <array>
#include <cstdint>
namespace PetMotion
{
    // Resolve random roles once so the preview and the actual game agree.
    class GamePlan
    {
        uint32_t random=0x31AF5789;
        int previousActor=-2,previousVictim=-2,previousMask=-1;
        bool previousChase=false;
        int Pick(const std::array<bool,4>& enabled,int except)
        {
            int list[4],n=0;for(int i=0;i<4;i++)if(enabled[i] && i!=except)list[n++]=i;
            if(!n)return -1;
            random^=random<<13;random^=random>>17;random^=random<<5;
            return list[random%n];
        }
    public:
        int actor=-1,victim=-1;
        void Refresh(bool chase,int actorChoice,int victimChoice,const std::array<bool,4>& enabled)
        {
            int mask=0;for(int i=0;i<4;i++)if(enabled[i])mask|=1<<i;
            if(previousActor==actorChoice && previousVictim==victimChoice && previousMask==mask && previousChase==chase)return;
            previousActor=actorChoice;previousVictim=victimChoice;previousMask=mask;previousChase=chase;
            victim=!chase && victimChoice>0?victimChoice-1:-1;
            actor=actorChoice>0?actorChoice-1:Pick(enabled,victim);
            if(!chase && victimChoice==0)victim=Pick(enabled,actor);
        }
        void Reroll(){previousMask=-1;}
    };
}
