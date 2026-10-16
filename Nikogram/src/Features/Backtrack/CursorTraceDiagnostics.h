#pragma once
#include "../Aimbot/SelfDamageDiagnostics.h"
#include "../Aimbot/ProjectileGuideCamera.h"
#include <array>

// Observes the existing native cursor trace only. Callback order is not ray
// intersection order; TRACE_EVERYTHING does not enumerate all static props.
namespace CursorTraceDiagnostics
{
    inline unsigned long long lastSample=0;
    inline bool sampled=false;
    inline bool Due(CUserCmd* command)
    {
        if (!command || !SelfDamageDiagnostics::TriggerEnabled()) return false;
        const auto now=GetTickCount64();
        if (sampled && now-lastSample<250) return false;
        sampled=true;lastSample=now;return true;
    }
    struct PlayerDecision { int index,handle,team,role; bool allowed; };
    class Filter : public CTraceFilterHitscan
    {
    public:
        bool capture=false;
        std::array<PlayerDecision,8> players{};
        unsigned count=0,dropped=0,duplicates=0,callbacks=0,staticCallbacks=0;
        bool ShouldHitEntity(IHandleEntity* handle,int mask) override
        {
            const bool allowed=CTraceFilterHitscan::ShouldHitEntity(handle,mask);
            if (!capture) return allowed;
            ++callbacks;
            if (!handle || handle==pSkip) return allowed;
            if (handle->GetRefEHandle().GetSerialNumber()==(1<<15))
            { ++staticCallbacks;return allowed; }
            auto entity=reinterpret_cast<CBaseEntity*>(handle);
            if (entity->GetClassID()!=ETFClassID::CTFPlayer) return allowed;
            const int fullHandle=entity->GetRefEHandle().ToInt();
            for (unsigned i=0;i<count;++i)
                if (players[i].handle==fullHandle && players[i].allowed==allowed)
                {++duplicates;return allowed;}
            if (count==players.size()) {++dropped;return allowed;}
            players[count++]={entity->entindex(),fullHandle,
                entity->m_iTeamNum(),entity->As<CTFPlayer>()->m_iClass(),allowed};
            return allowed;
        }
    };
    inline void Write(const Filter& filter,CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* command,
        const Vec3& angles,const Vec3& eye,const Vec3& end,float range,int mask,const CGameTrace& trace) noexcept
    {
        if (!filter.capture) return;
        try
        {
            // Never dereference an unverified trace entity pointer. World uses
            // hitbox for static-prop indexing; preserve that scalar verbatim.
            int entityIndex=-1,entityClass=-1,team=-1,handle=-1,scanned=0;
            const int highest=I::ClientEntityList?I::ClientEntityList->GetHighestEntityIndex():-1;
            if (trace.m_pEnt && I::ClientEntityList)
                for (int i=0;i<=std::min(highest,4095);++i)
                {
                    ++scanned;
                    if (reinterpret_cast<CBaseEntity*>(I::ClientEntityList->GetClientEntity(i))!=trace.m_pEnt) continue;
                    entityIndex=i;entityClass=int(trace.m_pEnt->GetClassID());
                    team=trace.m_pEnt->m_iTeamNum();handle=trace.m_pEnt->GetRefEHandle().ToInt();break;
                }
            const auto engine=I::EngineClient->GetViewAngles(),punch=local->m_vecPunchAngle();
            const auto camera=ProjectileGuideCamera::state;
            const float now=I::GlobalVars->realtime,age=now-camera.realtime;
            const bool identity=camera.owner==unsigned(local->GetRefEHandle().ToInt())
                &&camera.spawn==local->m_iSpawnCounter()&&camera.server==I::ClientState->m_nServerCount;
            std::string text=std::format("cmd={} input_cmd={} tick={} input_tick={} weapon={} local_team={} mask={} range={} "
                "cmd_angles={},{},{} input_angles={},{},{} shot_angles={},{},{} punch={},{},{} engine_angles={},{},{} "
                "eye={},{},{} requested_end={},{},{} trace_start={},{},{} trace_end={},{},{} fraction={} startsolid={} allsolid={} "
                "entity_present={} entity_index={} entity_class={} entity_team={} entity_handle={} entity_scan={} entity_scan_truncated={} "
                "hitgroup={} hitbox_raw={} contents={} surface_flags={} surface_props={} "
                "cached_camera_identity={} cached_camera_fresh={} cached_camera_age={} cached_eye_offset={},{},{} cached_angle_offset={},{},{} "
                "filter_callbacks={} filter_static_callbacks={} filter_players={} filter_player_observations_dropped={} filter_player_duplicates={} filter_order=callback props_not_enumerated=1",
                command->command_number,G::OriginalCmd.command_number,command->tick_count,G::OriginalCmd.tick_count,
                weapon->GetWeaponID(),local->m_iTeamNum(),mask,range,
                command->viewangles.x,command->viewangles.y,command->viewangles.z,
                G::OriginalCmd.viewangles.x,G::OriginalCmd.viewangles.y,G::OriginalCmd.viewangles.z,
                angles.x,angles.y,angles.z,punch.x,punch.y,punch.z,engine.x,engine.y,engine.z,
                eye.x,eye.y,eye.z,end.x,end.y,end.z,trace.startpos.x,trace.startpos.y,trace.startpos.z,
                trace.endpos.x,trace.endpos.y,trace.endpos.z,trace.fraction,trace.startsolid,trace.allsolid,
                trace.m_pEnt!=nullptr,entityIndex,entityClass,team,handle,scanned,highest>4095&&entityIndex<0,
                trace.hitgroup,trace.hitbox,trace.contents,trace.surface.flags,trace.surface.surfaceProps,
                identity,identity&&ProjectileGuidePolicy::FreshTime(now,camera.realtime),age,
                camera.eyeOffset.x,camera.eyeOffset.y,camera.eyeOffset.z,camera.angleOffset.x,camera.angleOffset.y,camera.angleOffset.z,
                filter.callbacks,filter.staticCallbacks,filter.count,filter.dropped,filter.duplicates);
            for (unsigned i=0;i<filter.count;++i)
            {
                const auto& p=filter.players[i];player_info_t info{};
                const bool named=I::EngineClient->GetPlayerInfo(p.index,&info);
                std::string name;
                if (named) for (unsigned j=0;j<std::min(sizeof(info.name),size_t(48))&&info.name[j];++j)
                {const unsigned char c=info.name[j];name+=c<32||c=='"'||c=='\\'?'?':char(c);}
                const bool nameTruncated=named&&sizeof(info.name)>48&&info.name[48]!=0&&name.size()==48;
                text+=std::format(" player{}=[index:{} handle:{} team:{} class:{} allowed:{} named:{} name_truncated:{} name:\"{}\"]",
                    i,p.index,p.handle,p.team,p.role,p.allowed,named,nameTruncated,name);
            }
            SelfDamageDiagnostics::WriteTrigger("trigger_trace",text);
        }
        catch (...) {} // Diagnostic allocation/format errors cannot affect selection.
    }
}
