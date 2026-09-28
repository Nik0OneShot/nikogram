#pragma once
#include "SelfDamageDiagnostics.h"
#include <map>
namespace MeleeDiagnostics
{
    struct Capture { int command=0, details=0, traces=0; std::map<std::string,int> reasons; };
    inline thread_local Capture* current=nullptr;
    inline void Event(const char* reason,int target=-1,float value=0)
    {
        if(!current) return;
        ++current->reasons[reason];
        if(current->details++>=20) return;
        SelfDamageDiagnostics::Write("melee_stage",std::format("cmd={} reason={} target={} value={}",current->command,reason,target,value));
    }
    struct Command
    {
        Capture capture{}; Capture* previous=current; const CUserCmd* cmd;
        Command(CTFWeaponBase* weapon,const CUserCmd* command):cmd(command)
        {
            current=nullptr;
            if(!SelfDamageDiagnostics::Enabled() || !weapon || (G::PrimaryWeaponType!=EWeaponType::MELEE && G::SecondaryWeaponType!=EWeaponType::MELEE)) return;
            static unsigned long long last=0; const auto now=GetTickCount64();
            if(last && now>=last && now-last<500) return;
            last=now; capture.command=cmd->command_number; current=&capture;
            SelfDamageDiagnostics::Write("melee_begin",std::format("cmd={} weapon={} item={} aim={} autoshoot={} can_fire={} smack={} prediction={} original_buttons={}",capture.command,
                weapon->GetWeaponID(),weapon->m_iItemDefinitionIndex(),Vars::Aimbot::General::AimType.Value,Vars::Aimbot::General::AutoShoot.Value,
                G::CanPrimaryAttack,weapon->m_flSmackTime(),Vars::Aimbot::Melee::SwingPrediction.Value,G::OriginalCmd.buttons));
        }
        ~Command()
        {
            if(current==&capture)
            {
                std::string counts;
                for(const auto& [reason,count] : capture.reasons) counts+=std::format(" {}={}",reason,count);
                SelfDamageDiagnostics::Write("melee_end",std::format("cmd={} buttons={} attacking={} details_omitted={}{}",capture.command,cmd->buttons,G::Attacking,std::max(capture.details-20,0),counts));
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
