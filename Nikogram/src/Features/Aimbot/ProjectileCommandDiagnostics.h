#pragma once
#include "SelfDamageDiagnostics.h"
#include "AimModes.h"
#include <array>

// Read-only command provenance. No trace, prediction, random call or angle write.
namespace ProjectileCommandDiagnostics
{
    struct Stage
    {
        const char* name=nullptr;
        Vec3 angles{};
        int command=0,buttons=0,attacking=0,tick=0;
        bool silent=false,psilent=false,aa=false;
    };
    class Command;
    inline Command* current=nullptr;
    class Command
    {
        std::array<Stage,16> stages{};
        size_t count=0;
        bool active=false,ready=false,selected=false;
        int sequence=0,inputCommand=0,owner=0,weaponHandle=0,item=0,weaponID=0,spawn=0,server=0;
        int selectedHandle=0,selectedCommand=0,selectedTick=0;
        Vec3 raw{},solution{},target{},predicted{};
        float flight=0.f;
        int rawButtons=0;
    public:
        Command(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd,int slot)
        {
            if(!SelfDamageDiagnostics::Enabled() || !local || !weapon || !cmd
                || G::PrimaryWeaponType!=EWeaponType::PROJECTILE) return;
            active=true;current=this;ready=G::CanPrimaryAttack;sequence=slot;inputCommand=G::OriginalCmd.command_number;
            owner=local->GetRefEHandle().ToInt();weaponHandle=weapon->GetRefEHandle().ToInt();
            item=weapon->m_iItemDefinitionIndex();weaponID=weapon->GetWeaponID();
            spawn=local->m_iSpawnCounter();server=I::ClientState->m_nServerCount;
            raw=G::OriginalCmd.viewangles;rawButtons=G::OriginalCmd.buttons;
            Sample("before_aim",cmd);
        }
        ~Command(){if(current==this)current=nullptr;}
        bool Active()const{return active;}
        void Sample(const char* name,const CUserCmd* cmd)
        {
            if(!active || !cmd || count==stages.size())return;
            stages[count++]={name,cmd->viewangles,cmd->command_number,cmd->buttons,G::Attacking,
                I::GlobalVars->tickcount,G::SilentAngles,G::PSilentAngles,G::AntiAim};
        }
        void Select(const CUserCmd* cmd,CBaseEntity* entity,const Vec3& angle,const Vec3& point,const Vec3& future,float time)
        {
            if(!active || !cmd || !entity)return;
            selected=true;selectedCommand=cmd->command_number;selectedTick=I::GlobalVars->tickcount;
            selectedHandle=entity->GetRefEHandle().ToInt();solution=angle;target=point;predicted=future;flight=time;
        }
        void Finish(const CUserCmd* cmd,bool send,bool aaEnabled,bool compatibility)
        {
            if(!active || !cmd || count<2)return;
            bool attempted=bool(rawButtons&IN_ATTACK),shot=false,changed=false,abnormal=false;
            const auto different=[](const Vec3&a,const Vec3&b){return a.x!=b.x||a.y!=b.y||a.z!=b.z;};
            for(size_t i=0;i<count;++i)
            {
                const auto&s=stages[i];attempted|=bool(s.buttons&IN_ATTACK);shot|=s.attacking==1;
                if(i)changed|=different(s.angles,stages[i-1].angles);
                abnormal|=!std::isfinite(s.angles.x)||!std::isfinite(s.angles.y)||!std::isfinite(s.angles.z)
                    || std::abs(s.angles.x)>80.f || std::abs(s.angles.z)>.001f;
            }
            if(!(shot || ready&&attempted) || !(changed||abnormal))return;
            // Hard bound, including shifted commands. Existing snapshots provide
            // the unchanged-shot heartbeat. Report dropped eligible records later.
            static unsigned long long window=0;static unsigned emitted=0,dropped=0;
            const auto now=GetTickCount64();if(now-window>=1000){window=now;emitted=0;}
            if(emitted>=8){++dropped;return;}++emitted;
            try
            {
                std::string writers;
                for(size_t i=0;i<count;++i)
                {
                    const auto&s=stages[i];
                    writers+=std::format("{}:{}:{},{},{}:b{}:a{}:s{}:p{}:aa{}:t{};",
                        s.name,s.command,s.angles.x,s.angles.y,s.angles.z,s.buttons,s.attacking,s.silent,s.psilent,s.aa,s.tick);
                }
                SelfDamageDiagnostics::Write("projectile_command_angles",std::format(
                    "sequence={} cmd={} input_cmd={} owner={} weapon_handle={} weapon={} item={} spawn={} server={} mode={} aim={} raw={},{},{} raw_buttons={} ready={} send={} nospread={} aa_enabled={} compatibility={} selected={} selected_cmd={} selected_tick={} selected_handle={} solver={},{},{} target={},{},{} predicted={},{},{} flight={} changed={} abnormal={} dropped={} stages={}",
                    sequence,cmd->command_number,inputCommand,owner,weaponHandle,weaponID,item,spawn,server,AimModes::Active,
                    Vars::Aimbot::General::AimType.Value,raw.x,raw.y,raw.z,rawButtons,ready,send,
                    Vars::Aimbot::General::NoSpread.Value,aaEnabled,compatibility,selected,selectedCommand,selectedTick,
                    selectedHandle,solution.x,solution.y,solution.z,target.x,target.y,target.z,predicted.x,predicted.y,predicted.z,
                    flight,changed,abnormal,dropped,writers));
                dropped=0;
            }
            catch(...){} // Optional diagnostics cannot interrupt the command.
        }
    };
    inline void Selected(const CUserCmd* cmd,CBaseEntity* entity,const Vec3& angle,const Vec3& point,const Vec3& future,float time)
    {if(current)current->Select(cmd,entity,angle,point,future,time);}
}
