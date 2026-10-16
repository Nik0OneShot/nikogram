#pragma once
#include "../../SDK/SDK.h"

// The native player-bullet path has a direct player-clipping recovery after a
// world/miss trace. Preserve Trigger's existing physical filter and mask, add
// that recovery only for Sniper rifles, and retain conservative cover guards.
// This is current collision geometry, never a render/history intersection.
namespace SniperPlayerClip
{
    struct Identity
    {
        uintptr_t pointer=0;int index=-1,classID=-1;unsigned handle=0,scanned=0;
        bool resolved=false,truncated=false;
    };
    struct TraceSnapshot
    {
        Identity entity{};Vec3 start{},end{};float fraction=1;
        int hitbox=-1,hitgroup=0,contents=0;bool startsolid=false,allsolid=false;
    };
    struct ValidationCallbacks
    {
        unsigned observed=0,omitted=0,nulls=0,candidateSeen=0,candidateAccepted=0;
        unsigned staticAccepted=0,otherPlayerRejected=0,otherAccepted=0;
        void Record(int branch)
        {
            // Count the existing decisions only; do not invoke the filter again.
            if(observed>=4096){if(omitted!=~0u)++omitted;return;}
            ++observed;
            if(branch==0)++nulls;
            else if(branch==1){++candidateSeen;++candidateAccepted;}
            else if(branch==2)++staticAccepted;
            else if(branch==3)++otherPlayerRejected;
            else ++otherAccepted;
        }
    };
    struct Stats
    {
        int examined=0,clipped=0,candidate=-1;bool validated=false,recovered=false;const char* reason="not_needed";
        Identity candidateIdentity{};TraceSnapshot original{},direct{},validation{};
        ValidationCallbacks callbacks{};Vec3 validationStart{},validationEnd{};
        unsigned validationMask=0;bool directKnown=false,validationKnown=false;
    };
    inline Identity Identify(const CBaseEntity* pointer)
    {
        Identity out;out.pointer=reinterpret_cast<uintptr_t>(pointer);
        if(!pointer||!I::ClientEntityList)return out;
        const int highest=I::ClientEntityList->GetHighestEntityIndex();
        for(int index=0;index<=std::min(highest,4095);++index)
        {
            ++out.scanned;auto entity=I::ClientEntityList->GetClientEntity(index);
            if(reinterpret_cast<const CBaseEntity*>(entity)!=pointer)continue;
            // Only a known entity-list pointer may receive identity getters.
            // Static-prop/unknown trace pointers remain unresolved raw identities.
            out.index=index;out.handle=unsigned(entity->GetRefEHandle().ToInt());
            const auto cls=entity->GetClientClass();out.classID=cls?cls->m_ClassID:-1;
            out.resolved=true;break;
        }
        out.truncated=!out.resolved&&highest>4095;return out;
    }
    inline TraceSnapshot CaptureTrace(const CGameTrace& trace)
    {
        TraceSnapshot out;out.entity=Identify(trace.m_pEnt);out.start=trace.startpos;out.end=trace.endpos;
        out.fraction=trace.fraction;out.hitbox=trace.hitbox;out.hitgroup=trace.hitgroup;out.contents=trace.contents;
        out.startsolid=trace.startsolid;out.allsolid=trace.allsolid;return out;
    }
    inline bool Finite(const Vec3& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
    inline bool Rifle(int id){return id==TF_WEAPON_SNIPERRIFLE||id==TF_WEAPON_SNIPERRIFLE_DECAP||id==TF_WEAPON_SNIPERRIFLE_CLASSIC;}
    inline Stats Recover(const Vec3& start,const Vec3& direction,float range,unsigned mask,ITraceFilter& filter,CGameTrace& trace,bool capture=false)
    {
        Stats stats;
        if(capture)stats.original=CaptureTrace(trace);
        auto world=I::ClientEntityList->GetClientEntity(0);
        if(trace.startsolid||trace.allsolid||(trace.m_pEnt&&trace.m_pEnt!=world))return stats;
        if(!Finite(start)||!Finite(direction)||!std::isfinite(range)||range<=0||!std::isfinite(trace.fraction)
            ||trace.fraction<0||trace.fraction>1||std::abs(direction.LengthSqr()-1.f)>.0001f){stats.reason="invalid_ray";return stats;}
        stats.reason="no_player_clip";
        const float extended=range+40.f,worldDistance=range*trace.fraction;
        CGameTrace nearest=trace;CTFPlayer* candidate=nullptr;
        Ray_t ray;ray.Init(start,start+direction*extended);
        const int clients=I::GlobalVars->maxClients;
        // Fail closed for an unsupported server size rather than omit blockers.
        if(clients<0||clients>128){stats.reason="unsupported_player_count";return stats;}
        for(int index=1;index<=clients;++index)
        {
            ++stats.examined;auto entity=I::ClientEntityList->GetClientEntity(index);
            if(!entity||!entity->As<CBaseEntity>()->IsPlayer())continue;
            auto player=entity->As<CTFPlayer>();if(!player->IsAlive()||player->IsDormant())continue;
            if(!filter.ShouldHitEntity(player,mask))continue;
            // SOLID_BBOX is axis-aligned; do not invent a transformed center for
            // another collision type. The raw accessor's type is uint8 in TF2.
            const auto collision=player->m_Collision();if(!collision||collision->m_nSolidType!=SOLID_BBOX)continue;
            const Vec3 center=player->GetAbsOrigin()+(collision->m_vecMins+collision->m_vecMaxs)*.5f;
            if(!Finite(center))continue;
            const Vec3 delta=center-start;const float along=delta.Dot(direction);
            if(along<0||along>extended||(delta-direction*along).LengthSqr()>3600.f)continue;
            CGameTrace clipped{};++stats.clipped;I::EngineTrace->ClipRayToEntity(ray,mask|CONTENTS_HITBOX,player,&clipped);
            if(!std::isfinite(clipped.fraction)||clipped.fraction<0||clipped.fraction>=nearest.fraction)continue;
            nearest=clipped;candidate=player;
        }
        if(!candidate)return stats;
        stats.candidate=candidate->entindex();stats.reason="clip_not_validated";
        if(capture){stats.candidateIdentity=Identify(candidate);stats.direct=CaptureTrace(nearest);stats.directKnown=true;}
        // Do not search through a nearer rejected player for a farther target.
        // The extension is native, but acceptance cannot pass the original
        // weapon range or initial world endpoint, even by extension rounding.
        const float distance=nearest.fraction*extended;
        if(nearest.m_pEnt!=candidate||nearest.startsolid||nearest.allsolid||!Finite(nearest.endpos)
            ||distance>range||distance>worldDistance){stats.reason="clip_past_obstruction_or_range";return stats;}
        class ValidationFilter final:public ITraceFilter
        {
        public:
            CTFPlayer* candidate;
            ValidationCallbacks* diagnostics;
            explicit ValidationFilter(CTFPlayer* p,ValidationCallbacks* d):candidate(p),diagnostics(d){}
            bool ShouldHitEntity(IHandleEntity* handle,int) override
            {
                if(!handle){if(diagnostics)diagnostics->Record(0);return false;}
                if(handle==candidate){if(diagnostics)diagnostics->Record(1);return true;}
                if(handle->GetRefEHandle().GetSerialNumber()==(1<<15))
                {if(diagnostics)diagnostics->Record(2);return true;}
                const bool player=reinterpret_cast<CBaseEntity*>(handle)->IsPlayer();
                if(diagnostics)diagnostics->Record(player?3:4);
                return !player;
            }
            TraceType_t GetTraceType() const override{return TRACE_EVERYTHING;}
        } validationFilter(candidate,capture?&stats.callbacks:nullptr);
        const auto absolute=candidate->GetAbsOrigin();if(!Finite(absolute)){stats.reason="invalid_origin";return stats;}
        CGameTrace validation{};
        SDK::Trace(nearest.endpos,Vec3(absolute.x,absolute.y,nearest.endpos.z),mask&~CONTENTS_HITBOX,&validationFilter,&validation);
        stats.validated=true;
        if(capture)
        {
            stats.validationStart=nearest.endpos;stats.validationEnd={absolute.x,absolute.y,nearest.endpos.z};
            stats.validationMask=mask&~CONTENTS_HITBOX;stats.validation=CaptureTrace(validation);stats.validationKnown=true;
        }
        if(validation.m_pEnt!=candidate){stats.reason="validation_did_not_hit_candidate";return stats;}
        trace=nearest;
        // Consumers use the original range. Preserve physical hit distance.
        trace.fraction=distance/range;stats.recovered=true;stats.reason="recovered_player_clip";return stats;
    }
}
