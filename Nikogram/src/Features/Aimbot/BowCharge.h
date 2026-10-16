#pragma once
#include "../../SDK/SDK.h"
#include "BowChargePolicy.h"

namespace BowCharge
{
    inline float MaximumTime(CTFWeaponBase* weapon, CTFPlayer* owner)
    {
        // CTFCompoundBow::GetChargeMaxTime applies fire delay followed by the
        // bow's ApplyRefireSpeedModifications (fast_reload, master, then Haste).
        const float delay=weapon->ApplyFireDelay(1.f);
        const float reloaded=SDK::AttribHookValue(delay,"fast_reload",weapon);
        const int master=owner?int(SDK::AttribHookValue(0.f,"ability_master_sniper",owner)):0;
        return BowChargePolicy::RefireTime(reloaded,master,owner && owner->InCond(TF_COND_RUNE_HASTE));
    }
}
