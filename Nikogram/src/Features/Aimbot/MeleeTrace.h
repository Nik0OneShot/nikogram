#pragma once
#include "MeleeTracePolicy.h"
#include "MeleeDiagnostics.h"
namespace MeleeTrace
{
    inline bool Confirm(CBaseEntity* target, const Vec3& from, const Vec3& to,
        const Vec3& mins, const Vec3& maxs, const CGameTrace& actual, CTraceFilterHitscan filter)
    {
        if (actual.m_pEnt == target) return true;
        const bool allowed = filter.ShouldHitEntity(target, MASK_SOLID);
        Ray_t ray; ray.Init(from, to, mins, maxs);
        CGameTrace clipped{};
        if (allowed) I::EngineTrace->ClipRayToEntity(ray, MASK_SOLID, target, &clipped);
        const bool recovered = MeleeTracePolicy::Recover(allowed, clipped.m_pEnt == target,
            clipped.fraction, clipped.startsolid, clipped.allsolid,
            actual.fraction, actual.startsolid, actual.allsolid);
        MeleeDiagnostics::Compare(target, from, to, mins, maxs, actual, clipped, allowed, recovered);
        if (recovered) MeleeDiagnostics::Event("trace_recovered", target->entindex(), clipped.fraction);
        return recovered;
    }
}
