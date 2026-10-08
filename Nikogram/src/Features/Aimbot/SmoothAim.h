#pragma once
#include "SmoothPolicy.h"
#include "AimModes.h"

namespace SmoothAim
{
    inline SmoothPolicy::Controller controller;
    inline std::uint32_t candidate=0;
    inline bool previewOnly=true;
    inline SmoothPolicy::AssistChoice assistChoice;
    inline bool Combined(){return AimModes::Active==AimModes::Legit&&Vars::Aimbot::General::CombinedGuidance.Value;}
    inline bool VisibleGuidance()
    {return Vars::Aimbot::General::AimType.Value==Vars::Aimbot::General::AimTypeEnum::Smooth
        ||Vars::Aimbot::General::AimType.Value==Vars::Aimbot::General::AimTypeEnum::Assistive;}
    inline bool Region()
    {
        namespace A=Vars::Aimbot::General;
        return AimModes::Active==AimModes::Legit&&A::AimDestination.Value==A::AimDestinationEnum::Region
            &&(A::AimType.Value==A::AimTypeEnum::Smooth||A::AimType.Value==A::AimTypeEnum::Assistive);
    }
    inline std::uint32_t Handle(CBaseEntity* entity){return entity?entity->GetRefEHandle().ToInt():0;}
    inline bool HitboxAssistance(){return Combined()&&Region()&&Vars::Aimbot::General::AssistAmount.Value>0.f;}
    struct GuideScope
    {
        SmoothPolicy::AssistChoice old=assistChoice;
        GuideScope(const SmoothPolicy::AssistChoice& choice){assistChoice=choice;}
        ~GuideScope(){assistChoice=old;}
    };
    struct CandidateScope
    {
        std::uint32_t old=candidate;
        bool oldPreview=previewOnly;
        CandidateScope(CBaseEntity* entity,bool probe=false){candidate=Handle(entity);previewOnly=probe;}
        ~CandidateScope(){candidate=old;previewOnly=oldPreview;}
    };
    inline void Begin(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd,bool allowed)
    {
        namespace A=Vars::Aimbot::General;
        const bool active=allowed&&local&&local->IsAlive()&&weapon&&cmd
            &&A::AimType.Value==A::AimTypeEnum::Smooth&&(Combined()||A::SmoothFormula.Value==A::SmoothFormulaEnum::Damped);
        SmoothPolicy::Context context;
        context.local=Handle(local);context.weapon=Handle(weapon);
        context.playerClass=local?local->m_iClass():0;context.bank=AimModes::Active;
        context.settings={A::SmoothTime.Value,A::SmoothSpeed.Value,A::SmoothAcceleration.Value};
        if(Combined())
        {
            context.settings.smoothAmount=A::SmoothAmount.Value;context.settings.assistAmount=A::AssistAmount.Value;
            context.settings.responsiveAssist=true;context.assistPreference=A::AssistHitbox.Value;
        }
        assistChoice={};
        context.destination=Region()?1:0;
        const Vec3 view=cmd?cmd->viewangles:Vec3{};
        Vec3 mouse;
        if(cmd&&(cmd->mousedx||cmd->mousedy)&&G::LastUserCmd)mouse=view.DeltaAngle(G::LastUserCmd->viewangles);
        controller.Begin(cmd?cmd->command_number:-1,context,active,{view.x,view.y},TICK_INTERVAL,{mouse.x,mouse.y});
    }
    inline bool Preview(const Vec3& goal,Vec3& out)
    {
        if(!controller.Enabled()||!candidate)return false;
        const auto angle=assistChoice.valid&&HitboxAssistance()?controller.Preview(candidate,{goal.x,goal.y},assistChoice.goal)
            :controller.Preview(candidate,{goal.x,goal.y});out={angle[0],angle[1],0.f};return true;
    }
    inline void Select(const Vec3& out)
    {if(!previewOnly)controller.Select(candidate,{out.x,out.y});}
    inline void Finish(){controller.Finish();}
}
