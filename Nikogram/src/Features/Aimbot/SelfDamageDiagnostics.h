#pragma once
#include "../../SDK/SDK.h"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include "RocketSafetyGeometry.h"
#include "ProjectilePerformancePolicy.h"

namespace SelfDamageDiagnostics
{
    inline int LastShotCommand=-1;
    inline int LastProjectileSampleCommand=-1;
    inline bool Enabled() { return Vars::Aimbot::Projectile::SelfDamageDiagnostics.Value; }
    inline void Write(const char* event,const std::string& message,bool detail=false) noexcept
    {
        if(!Enabled()) return;
        try
        {
            static std::mutex mutex; std::lock_guard lock(mutex);
            static std::ofstream file; static std::filesystem::path path;
            static std::uintmax_t fileBytes=0;
            static bool started=false; static unsigned long long detailWindow=0; static int count=0;
            static ProjectilePerformancePolicy::FlushWindow flush;
            const int tick=I::GlobalVars?I::GlobalVars->tickcount:0;
            const auto now=GetTickCount64();
            if(now-detailWindow>=100) {detailWindow=now;count=0;}
            if(detail && count++>=12) return;
            if(path.empty())
            {
                wchar_t executable[32768]={};
                const DWORD length=GetModuleFileNameW(nullptr,executable,32768);
                if(!length || length>=32768) return;
                path=std::filesystem::path(executable).parent_path()/L"Nikogram"/L"Logs"/L"self-damage-diagnostic.log";
                std::error_code ec; std::filesystem::create_directories(path.parent_path(),ec);
                if(ec) return;
            }
            if(!file.is_open())
            {
                file.open(path,std::ios::app);
                std::error_code ec;
                fileBytes=std::filesystem::file_size(path,ec);
                if(ec) fileBytes=0;
            }
            if(!file) return;
            // tellp() can synchronize a file buffer. Track bytes instead of
            // seeking on every record, so batching is not accidentally defeated.
            if(fileBytes>=2*1024*1024)
            {
                file.close(); std::error_code ec;
                auto one=path; one+=L".1"; auto two=path; two+=L".2";
                std::filesystem::remove(two,ec); ec.clear();
                if(std::filesystem::exists(one,ec)) {ec.clear();std::filesystem::rename(one,two,ec);if(ec)return;}
                ec.clear();std::filesystem::rename(path,one,ec);if(ec)return;
                file.open(path,std::ios::app); started=false; flush.Reset(); fileBytes=0;
            }
            if(!started)
            {
                const auto header=std::format("SESSION projectile-diag-v136-cooldown-preview build={} {} pid={}\n",__DATE__,__TIME__,GetCurrentProcessId());
                file<<header; fileBytes+=header.size()+1; started=true;
            }
            const auto line=std::format("ms={} tick={} event={} {}\n",now,tick,event,message);
            file<<line; fileBytes+=line.size()+1; // Windows text mode also writes CR.
            // Keep stream buffering and batch explicit flushes. close()/rotation
            // still flushes normally; a crash may lose the most recent buffer.
            if(flush.Due(now,line.size()+1)) file.flush();
        }
        catch(...) {} // Diagnostics must not interfere with commands or crash handling.
    }
    inline void Snapshot(const char* stage,CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd)
    {
        if(!Enabled() || !local || !weapon || !cmd) return;
        const bool attempting=(cmd->buttons&IN_ATTACK) || (G::OriginalCmd.buttons&IN_ATTACK);
        if(attempting && G::CanPrimaryAttack) LastShotCommand=cmd->command_number;
        static std::unordered_map<std::string,RocketSafetyGeometry::Heartbeat> stages;
        if(!stages[stage].Due(GetTickCount64(),cmd->command_number==LastShotCommand || cmd->command_number==LastProjectileSampleCommand)) return;
        const auto p=local->GetAbsOrigin(),v=local->m_vecVelocity();
        Write(stage,std::format("cmd={} buttons={} original={} attacking={} can_fire={} aim={} autoshoot={} modifiers={} protection={} autodet={} weapon={} item={} clip={} hp={}/{} invuln={} pos={},{},{} vel={},{},{} angles={},{},{} choke={}",
            cmd->command_number,cmd->buttons,G::OriginalCmd.buttons,G::Attacking,G::CanPrimaryAttack,
            Vars::Aimbot::General::AimType.Value,Vars::Aimbot::General::AutoShoot.Value,
            Vars::Aimbot::Projectile::Modifiers.Value,Vars::Aimbot::Projectile::SelfDamageProtection.Value,
            Vars::Aimbot::Projectile::AutoDetonate.Value,weapon->GetWeaponID(),weapon->m_iItemDefinitionIndex(),weapon->m_iClip1(),
            local->m_iHealth(),local->GetMaxHealth(),local->IsInvulnerable(),p.x,p.y,p.z,v.x,v.y,v.z,
            cmd->viewangles.x,cmd->viewangles.y,cmd->viewangles.z,I::ClientState->chokedcommands));
    }
}
