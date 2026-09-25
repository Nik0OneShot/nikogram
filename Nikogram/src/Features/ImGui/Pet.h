#pragma once
#include "PetSocial.h"
#include "PetGames.h"
#include "PetGamePlan.h"
#include "PetClock.h"
#include "Workspace.h"
#include "Render.h"
#include "PetAppearance.h"
#include "PetLayer.h"

namespace NikoPet
{
    inline std::array<PetMotion::Pet,4> Pets;
    inline PetMotion::Conversation Social;
    inline PetMotion::Games Games;
    inline PetMotion::World World;
    inline bool Hovered=false;
    inline int Dragged=-1;
    inline void GameControls()
    {
        using namespace ImGui;
        static PetMotion::GamePlan plan;
        const std::array<bool,4> enabled={Workspace::PetNiko,Workspace::PetAlula,Workspace::PetCalamus,Workspace::PetWorldMachine};
        const char* choices="Random\0Niko\0Alula\0Calamus\0The World Machine\0";
        const auto name=[](int i){const char* names[]={"Niko","Alula","Calamus","The World Machine"};return i>=0 && i<4?names[i]:"Unavailable";};
        Separator();TextUnformatted("Play a game");
        SetNextItemWidth(160);ImGui::Combo("Game##Pet",&Workspace::PetGameChoice,"Scare\0Chase\0");
        const bool chase=Workspace::PetGameChoice==1;
        SetNextItemWidth(160);
        ImGui::Combo(chase?"Chaser##Pet":"Scarer##Pet",chase?&Workspace::PetChaser:&Workspace::PetScarer,choices);
        if(!chase){SetNextItemWidth(160);ImGui::Combo("Scaree##Pet",&Workspace::PetScaree,choices);}
        plan.Refresh(chase,chase?Workspace::PetChaser:Workspace::PetScarer,Workspace::PetScaree,enabled);
        if(Games.Active())
        {
            if(Games.IsScare())TextWrapped("Current scarer: %s | Current scaree: %s",name(Games.Actor()),name(Games.Victim()));
            else TextWrapped("Current chaser: %s",name(Games.Actor()));
        }
        else if(chase)TextWrapped("Current chaser: %s",name(plan.actor));
        else TextWrapped("Current scarer: %s | Current scaree: %s",name(plan.actor),name(plan.victim));
        PetMotion::Options options;options.size=float(Workspace::PetSize);options.jump=Workspace::PetJump;options.run=Workspace::PetRun;
        auto world=World;world.top=Workspace::TopTaskbar;
        const char* reason=!Workspace::PetEnabled?"Enable pets first.":Games.ManualReason(chase,plan.actor,plan.victim,Pets,enabled,world,options);
        BeginDisabled(reason!=nullptr);
        if(Button("Start game"))
        {
            if(Games.StartManual(chase,plan.actor,plan.victim,Pets,enabled,world,options)){Social.Cancel(Pets);plan.Reroll();}
        }
        EndDisabled();SameLine();BeginDisabled(!Games.Active());
        if(Button("Stop game")){Games.Finish(Pets);plan.Reroll();}
        EndDisabled();
        if(!Games.Active() && (chase?Workspace::PetChaser==0:(Workspace::PetScarer==0 || Workspace::PetScaree==0)))
        {SameLine();if(Button("Reroll##PetGame"))plan.Reroll();}
        if(reason)TextWrapped("%s",reason);
        else TextWrapped("Skips grace / cooldown. Scare includes revenge.");
    }
    inline void Close() { if(Games.Active())Games.Finish(Pets);Social.Cancel(Pets);for(auto& p:Pets)p.Close();Dragged=-1;Hovered=false;World.windows.clear(); }
    inline void Suspend() { if(Social.Active())Social.Cancel(Pets);for(auto& p:Pets)if(!p.game)p.Suspend();Dragged=-1;Hovered=false;World.windows.clear(); }
    inline void Draw()
    {
        using namespace ImGui;
        Hovered=false;
        if(!Workspace::PetEnabled){Close();return;}
        const std::array<bool,4> enabled={Workspace::PetNiko,Workspace::PetAlula,Workspace::PetCalamus,Workspace::PetWorldMachine};
        PetMotion::Options options;
        options.size=float(Workspace::PetSize);options.rest=float(Workspace::PetRest);options.sleep=float(Workspace::PetSleep);
        options.run=Workspace::PetRun;options.jump=Workspace::PetJump;options.climb=Workspace::PetClimb;
        const float dt=std::clamp(GetIO().DeltaTime,.0001f,.05f);
        for(int i=0;i<4;i++)
        {
            auto& p=Pets[i];
            if(!enabled[i]){p.Close();if(Dragged==i)Dragged=-1;continue;}
            if(!p.active && World.width>=options.size && World.height>=options.size)
            {
                p.Seed(0xA512DE31u+0x351239u*i);p.Start(World,options);
                const float half=options.size*.44f;
                p.x=std::clamp(World.barWidth*(.2f+.2f*i),half,std::max(half,World.barWidth-half));
            }
        }
        Games.Update(dt,std::max(0.f,GetIO().DeltaTime),PetClock::Uptime(),true,Pets,enabled,World,options,Workspace::PetScares,Workspace::PetChase,Social.Active());
        if(!Games.Active())Social.Update(dt,Pets,enabled,World,options,Workspace::PetSocial);
        for(int i=0;i<4;i++)if(enabled[i])Pets[i].Update(dt,World,options);
        Social.FaceSpeaker(Pets);Games.FaceSpeaker(Pets);
        const ImVec2 mouse=GetIO().MousePos;
        bool blocked=IsWindowHovered(ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
            || IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || IsAnyItemActive();
        for(const auto& r:World.windows)if(mouse.x>=r.left && mouse.x<=r.right && mouse.y>=r.top && mouse.y<=r.bottom)blocked=true;
        // Reverse render order: only the uppermost opaque pet can be picked up.
        if(Dragged<0 && Workspace::PetDrag && !blocked)
            for(int i=3;i>=0;i--)
            {
                auto& p=Pets[i];if(!enabled[i] || !p.active)continue;
                const int f=CharacterFrame(p,i);const auto r=CharacterRect(p,i,f,World,options.size);
                if(mouse.x<r.left || mouse.x>=r.right || mouse.y<r.top || mouse.y>=r.bottom)continue;
                const auto& a=GetAsset(f,i);const auto& pixels=Pixels(f,i);if(pixels.empty())continue;
                float u=(mouse.x-r.left)/(r.right-r.left);if(p.faceRight)u=1-u;
                const int px=std::clamp(int(a.left+u*(a.right-a.left)),0,int(a.width)-1);
                const int py=std::clamp(int(a.top+(mouse.y-r.top)/(r.bottom-r.top)*(a.bottom-a.top)),0,int(a.height)-1);
                if((pixels[py*a.width+px]>>24)<=32)continue;
                Hovered=true;
                if(IsMouseClicked(ImGuiMouseButton_Left)){if(Games.Active())Games.Finish(Pets);Social.Cancel(Pets);p.Grab(mouse.x,mouse.y,p.x,r.bottom);Dragged=i;}
                break;
            }
        if(Dragged>=0)
        {
            auto& p=Pets[Dragged];
            if(!Workspace::PetDrag || !IsMouseDown(ImGuiMouseButton_Left)){p.Release(World,options);Dragged=-1;}
            else p.Drag(mouse.x,mouse.y,dt);
            Hovered=true;SetNextFrameWantCaptureMouse(true);
        }
        if(Hovered)SetMouseCursor(ImGuiMouseCursor_Hand);
        for(int i=0;i<4;i++)
        {
            const auto& p=Pets[i];if(!enabled[i] || !p.active)continue;
            const int f=CharacterFrame(p,i);const auto& a=GetAsset(f,i);const auto& pixels=Pixels(f,i);if(pixels.empty())continue;
            const auto texture=F::Render.PetTexture(i*46+f,pixels.data(),a.width,a.height);if(!texture)continue;
            const auto r=CharacterRect(p,i,f,World,options.size);
            ImVec2 uv0(float(a.left)/a.width,float(a.top)/a.height),uv1(float(a.right)/a.width,float(a.bottom)/a.height);
            if(p.faceRight)std::swap(uv0.x,uv1.x);
            DrawLayer(texture,{r.left,r.top},{r.right,r.bottom},uv0,uv1);
        }
    }
}
