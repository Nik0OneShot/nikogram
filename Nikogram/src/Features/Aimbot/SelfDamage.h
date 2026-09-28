#pragma once
#include "../../SDK/SDK.h"
#include "SelfDamagePolicy.h"
#include "SelfDamageDiagnostics.h"
namespace SelfDamage
{
    inline float Estimate(CTFPlayer* local, CTFWeaponBase* weapon, const Vec3& impact, const Vec3& origin, float radius, bool diagnosticPriority=false)
    {
        if(local->IsInvulnerable() || weapon->m_iItemDefinitionIndex()==Soldier_m_RocketJumper
            || weapon->m_iItemDefinitionIndex()==Demoman_s_StickyJumper) return 0.f;
        const auto mins=origin+local->m_vecMins(), maxs=origin+local->m_vecMaxs();
        const Vec3 nearest(std::clamp(impact.x,mins.x,maxs.x),std::clamp(impact.y,mins.y,maxs.y),std::clamp(impact.z,mins.z,maxs.z));
        const float distance=impact.DistTo(nearest);
        auto report=[&](const char* reason,float damage) {
            if(SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("exposure",std::format("final_guard={} reason={} damage={} radius={} distance={} impact={},{},{} local={},{},{}",diagnosticPriority,reason,damage,radius,distance,impact.x,impact.y,impact.z,origin.x,origin.y,origin.z),!diagnosticPriority);
            return damage;
        };
        if(impact.DistToSqr(nearest)>radius*radius) return report("outside_radius",0.f);
        // Multiple hull points avoid treating eye-only occlusion as full-body cover.
        bool exposed=impact.DistToSqr(nearest)<=4.f;
        const Vec3 center=(mins+maxs)*.5f;
        const Vec3 points[]={nearest,center,origin+local->GetViewOffset(),
            Vec3(mins.x,center.y,center.z),Vec3(maxs.x,center.y,center.z),
            Vec3(center.x,mins.y,center.z),Vec3(center.x,maxs.y,center.z),Vec3(center.x,center.y,mins.z+2)};
        for(const auto& point:points)
            if(exposed || SDK::VisPosWorld(local,local,impact,point,MASK_SHOT)) {exposed=true;break;}
        if(!exposed) return report("occluded",0.f);
        // Conservative peak-damage estimate: do not assume falloff or resistance will save us.
        const float base=std::max(weapon->GetDamage(),weapon->GetDamage(false));
        const float multiplier=std::max(1.f,SDK::AttribHookValue(1.f,"blast_dmg_to_self",weapon));
        return report("exposed",std::isfinite(base)&&base>0 ? base*1.5f*multiplier : 150.f);
    }
    inline bool Block(CTFPlayer* local,float damage)
    {return SelfDamagePolicy::Block(damage,float(local->GetMaxHealth()),float(local->m_iHealth()),Vars::Aimbot::Projectile::SelfDamageProtection.Value);}
}
