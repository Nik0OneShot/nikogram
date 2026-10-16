#pragma once
#include "NamedPlayerTraceDiagnostics.h"

namespace NativeBulletDiagnostics
{
    namespace N=NamedPlayerTraceDiagnostics;
    struct Record
    {
        Vec3 start{},delta{},offset{},extents{},end{};float fraction=1;
        uintptr_t caller=0,tested=0,hit=0;unsigned mask=0;int kind=0,contents=0,hitbox=-1,group=0;
        bool startSolid=false,allSolid=false,isRay=false;
    };
    struct Buffer {std::array<Record,64> records{};unsigned count=0,dropped=0;};
    inline thread_local Buffer* active=nullptr;
    // Observes only calls already made by the native bullet path. Never invokes
    // traces, clipping, filter callbacks, or entity/cache updates itself.
    inline void Observe(const Ray_t& ray,unsigned mask,const void* tested,const CGameTrace* trace,uintptr_t caller,int kind) noexcept
    {
        if(!active||!trace)return;
        if(active->count>=active->records.size()){++active->dropped;return;}
        auto& r=active->records[active->count++];
        r.start={ray.m_Start.x,ray.m_Start.y,ray.m_Start.z};r.delta={ray.m_Delta.x,ray.m_Delta.y,ray.m_Delta.z};
        r.offset={ray.m_StartOffset.x,ray.m_StartOffset.y,ray.m_StartOffset.z};r.extents={ray.m_Extents.x,ray.m_Extents.y,ray.m_Extents.z};
        r.isRay=ray.m_IsRay;r.mask=mask;r.tested=reinterpret_cast<uintptr_t>(tested);r.hit=reinterpret_cast<uintptr_t>(trace->m_pEnt);
        r.end=trace->endpos;r.fraction=trace->fraction;r.contents=trace->contents;r.hitbox=trace->hitbox;r.group=trace->hitgroup;
        r.startSolid=trace->startsolid;r.allSolid=trace->allsolid;r.caller=caller;r.kind=kind;
    }
    struct Scope
    {
        static inline thread_local Buffer buffer;
        Buffer* previous=nullptr;bool capture=false;const char* path="unknown";
        Scope(CTFPlayer* player,CBaseCombatWeapon* weapon,const FireBulletsInfo_t& info,bool effects,int damageType,int custom) noexcept
        {
            if(!N::enabled.load(std::memory_order_relaxed)||GetCurrentThreadId()!=N::mainThread.load()||active
                ||!player||player!=H::Entities.GetLocal()||!weapon)return;
            try
            {
                const int id=weapon->As<CTFWeaponBase>()->GetWeaponID();if(id!=17&&id!=77&&id!=99)return;
                static uint64_t last=0;const auto now=GetTickCount64();if(last&&now>=last&&now-last<250)return;last=now;
                const auto& setting=damageType&DMG_CRITICAL?Vars::Visuals::Effects::CritTracer.Value:Vars::Visuals::Effects::BulletTracer.Value;
                path=setting=="Default"?"native_original":setting=="None"?"cosmetic_suppressed":"cosmetic_replacement";
                SelfDamageDiagnostics::WriteTrigger("trigger_native_bullet",std::format(
                    "schema=1 last_command={} command_is_temporal_only=1 weapon={} src={} direction={} spread={} distance={} shots={} effects={} damage_type={} custom={} primary={} server_seed={} client_side_only=1 path={}",
                    N::lastCommand,id,N::VectorText(info.m_vecSrc),N::VectorText(info.m_vecDirShooting),N::VectorText(info.m_vecSpread),info.m_flDistance,info.m_iShots,effects,damageType,custom,info.m_bPrimaryAttack,info.m_bUseServerRandomSeed,path));
                buffer.count=buffer.dropped=0;previous=active;active=&buffer;capture=true;
            }catch(...){}
        }
        ~Scope() noexcept
        {
            if(!capture)return;active=previous;
            try
            {
                std::array<uintptr_t,4> entities{};std::array<unsigned,3> handles{};
                entities[0]=reinterpret_cast<uintptr_t>(I::ClientEntityList->GetClientEntity(0));
                {std::unique_lock lock(N::mutex,std::try_to_lock);if(lock.owns_lock())for(int n=0;n<3;++n)
                {const auto& t=N::targets[n];if(t.matches==1&&t.handle){auto e=I::ClientEntityList->GetClientEntityFromHandle(CBaseHandle(t.handle));entities[n+1]=reinterpret_cast<uintptr_t>(e);handles[n]=t.handle;}}}
                auto label=[&](uintptr_t ptr)->const char*{if(!ptr)return "none";for(int n=0;n<4;++n)if(ptr==entities[n])return n?N::Names[n-1]:"world";return "other";};
                const auto module=reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll"));
                SelfDamageDiagnostics::WriteTrigger("trigger_native_summary",std::format("schema=1 last_command={} count={} dropped={} Bot09_handle={} Bot03_handle={} Bot07_handle={} client_base={} path={}",N::lastCommand,buffer.count,buffer.dropped,handles[0],handles[1],handles[2],module,path));
                for(unsigned n=0;n<buffer.count;++n)
                {const auto& r=buffer.records[n];SelfDamageDiagnostics::WriteTrigger("trigger_native_trace",std::format(
                    "schema=1 last_command={} sequence={} operation={} caller={} caller_minus_client_base={} mask={} tested={} hit={} fraction={} end={} start={} delta={} offset={} extents={} is_ray={} startsolid={} allsolid={} contents={} hitbox={} hitgroup={}",
                    N::lastCommand,n,r.kind?"ClipRayToEntity":"TraceRay",r.caller,int64_t(r.caller)-int64_t(module),r.mask,label(r.tested),label(r.hit),r.fraction,N::VectorText(r.end),N::VectorText(r.start),N::VectorText(r.delta),N::VectorText(r.offset),N::VectorText(r.extents),r.isRay,r.startSolid,r.allSolid,r.contents,r.hitbox,r.group));}
            }catch(...){}
        }
    };
    inline void Hurt(IGameEvent* event) noexcept
    {
        if(!N::enabled.load(std::memory_order_relaxed)||GetCurrentThreadId()!=N::mainThread.load())return;
        try
        {
            if(I::EngineClient->GetPlayerForUserID(event->GetInt("attacker"))!=I::EngineClient->GetLocalPlayer())return;
            const int victim=I::EngineClient->GetPlayerForUserID(event->GetInt("userid"));
            std::unique_lock lock(N::mutex,std::try_to_lock);if(!lock.owns_lock())return;
            for(int n=0;n<3;++n)if(N::targets[n].matches==1&&N::targets[n].index==victim&&N::targets[n].userID==event->GetInt("userid"))
            {
                static uint64_t window=0;static unsigned count=0;const auto now=GetTickCount64()/1000;if(now!=window){window=now;count=0;}if(count++>=16)return;
                SelfDamageDiagnostics::WriteTrigger("trigger_named_hurt",std::format("schema=1 name={} victim={} user_id={} attacker={} damage={} health={} weaponid={} custom={} crit={} minicrit={} last_command={} temporal_correlation_only=1",
                    N::Names[n],victim,event->GetInt("userid"),event->GetInt("attacker"),event->GetInt("damageamount"),event->GetInt("health"),event->GetInt("weaponid"),event->GetInt("custom"),event->GetBool("crit"),event->GetBool("minicrit"),N::lastCommand));return;
            }
        }catch(...){}
    }
}
