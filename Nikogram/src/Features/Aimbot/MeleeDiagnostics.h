#pragma once
#include "SelfDamageDiagnostics.h"
#include <map>
namespace MeleeDiagnostics
{
    struct Capture { int command=0, details=0, traces=0, geometry=0, backstabs=0, lastTarget=-1, lastResult=-1; std::map<std::string,int> reasons; };
    inline thread_local Capture* current=nullptr;
    inline void Event(const char* reason,int target=-1,float value=0)
    {
        if(!current) return;
        ++current->reasons[reason];
        if(std::string_view(reason)=="can_hit_result") {current->lastTarget=target;current->lastResult=int(value);}
        if(current->details++>=20) return;
        SelfDamageDiagnostics::Write("melee_stage",std::format("cmd={} reason={} target={} value={}",current->command,reason,target,value));
    }
    inline void Geometry(const char* stage,int target,float angle,float distance,float range,float recordAge=0,int point=-1)
    {
        if(!current || current->geometry++>=8) return;
        SelfDamageDiagnostics::Write("melee_geometry",std::format(
            "cmd={} stage={} target={} point={} angle={} fov={} distance={} range={} record_age={}",
            current->command,stage,target,point,angle,Vars::Aimbot::Melee::AimFOV.Value,distance,range,recordAge));
    }
    inline void Backstab(int target,float behind,float facing,float alignment,float behindLimit,float facingLimit,float alignmentLimit)
    {
        if(!current || current->backstabs++>=4) return;
        SelfDamageDiagnostics::Write("melee_backstab",std::format(
            "cmd={} target={} behind={} behind_required={} facing={} facing_required={} alignment={} alignment_required={}",
            current->command,target,behind,behindLimit,facing,facingLimit,alignment,alignmentLimit));
    }
    struct Command
    {
        Capture capture{}; Capture* previous=current; const CUserCmd* cmd;
        Command(CTFWeaponBase* weapon,const CUserCmd* command):cmd(command)
        {
            current=nullptr;
            if(!SelfDamageDiagnostics::Enabled() || !weapon || (G::PrimaryWeaponType!=EWeaponType::MELEE && G::SecondaryWeaponType!=EWeaponType::MELEE)) return;
            static unsigned long long last=0; const auto now=GetTickCount64();
            const bool active=Vars::Aimbot::General::AimType.Value || (G::OriginalCmd.buttons&IN_ATTACK) || weapon->m_flSmackTime()>0.f;
            if(last && now>=last && now-last<(active?100:500)) return;
            last=now; capture.command=cmd->command_number; current=&capture;
            SelfDamageDiagnostics::Write("melee_begin",std::format("cmd={} weapon={} item={} aim={} autoshoot={} can_fire={} smack={} prediction={} original_buttons={}",capture.command,
                weapon->GetWeaponID(),weapon->m_iItemDefinitionIndex(),Vars::Aimbot::General::AimType.Value,Vars::Aimbot::General::AutoShoot.Value,
                G::CanPrimaryAttack,weapon->m_flSmackTime(),Vars::Aimbot::Melee::SwingPrediction.Value,G::OriginalCmd.buttons));
            SelfDamageDiagnostics::Write("melee_settings",std::format(
                "cmd={} melee_fov={} general_fov={} lead_restrict={} auto_backstab={} backstab_flags={} swing_ticks={} validate={} tick_tolerance={} ignore={} target_flags={} angles={},{},{}",
                capture.command,Vars::Aimbot::Melee::AimFOV.Value,Vars::Aimbot::General::AimFOV.Value,Vars::Aimbot::General::LeadAndRestrict.Value,
                Vars::Aimbot::Melee::AutoBackstab.Value,Vars::Aimbot::Melee::BackstabFlags.Value,Vars::Aimbot::Melee::SwingTicks.Value,
                Vars::Aimbot::Melee::SwingValidateMode.Value,Vars::Aimbot::General::TickTolerance.Value,Vars::Aimbot::General::Ignore.Value,
                Vars::Aimbot::General::Target.Value,cmd->viewangles.x,cmd->viewangles.y,cmd->viewangles.z));
        }
        ~Command()
        {
            if(current==&capture)
            {
                std::string counts;
                for(const auto& [reason,count] : capture.reasons) counts+=std::format(" {}={}",reason,count);
                SelfDamageDiagnostics::Write("melee_end",std::format("cmd={} buttons={} attacking={} last_target={} last_result={} details_omitted={} geometry_omitted={} traces_omitted={} backstabs_omitted={}{}",
                    capture.command,cmd->buttons,G::Attacking,capture.lastTarget,capture.lastResult,std::max(capture.details-20,0),
                    std::max(capture.geometry-8,0),std::max(capture.traces-4,0),std::max(capture.backstabs-4,0),counts));
            }
            current=previous;
        }
    };
    inline void Compare(CBaseEntity* target,const Vec3& from,const Vec3& to,const Vec3& mins,const Vec3& maxs,
        const CGameTrace& actual,const CGameTrace& clipped,bool allowed,bool recovered)
    {
        if(!current || current->traces++>=4) return;
        SelfDamageDiagnostics::Write("melee_trace",std::format("cmd={} target={} filter_allowed={} live_entity={} live_fraction={} live_startsolid={} clip_entity={} clip_fraction={} clip_startsolid={} from={},{},{} to={},{},{} hull={},{},{} recovered={} live_allsolid={} clip_allsolid={} record_origin={},{},{} record_mins={},{},{} record_maxs={},{},{}",
            current->command,target->entindex(),allowed,actual.m_pEnt?actual.m_pEnt->entindex():-1,actual.fraction,actual.startsolid,
            clipped.m_pEnt?clipped.m_pEnt->entindex():-1,clipped.fraction,clipped.startsolid,from.x,from.y,from.z,to.x,to.y,to.z,maxs.x,maxs.y,maxs.z,
            recovered,actual.allsolid,clipped.allsolid,target->GetAbsOrigin().x,target->GetAbsOrigin().y,target->GetAbsOrigin().z,
            target->m_vecMins().x,target->m_vecMins().y,target->m_vecMins().z,target->m_vecMaxs().x,target->m_vecMaxs().y,target->m_vecMaxs().z));
    }
}
