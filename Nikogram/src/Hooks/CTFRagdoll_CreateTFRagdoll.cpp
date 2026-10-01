#include "../SDK/SDK.h"
#include "../Features/SkinChanger/SkinChanger.h"

MAKE_SIGNATURE(CTFRagdoll_CreateTFRagdoll, "client.dll", "48 89 4C 24 ? 55 53 56 57 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 8B 91", 0x0);

MAKE_HOOK(CTFRagdoll_CreateTFRagdoll, S::CTFRagdoll_CreateTFRagdoll(), void,
	void* rcx)
{
	DEBUG_RETURN(CTFRagdoll_CreateTFRagdoll, rcx);

	if (Vars::Visuals::Removals::Ragdolls.Value)
		return;

	auto pRagdoll = reinterpret_cast<CTFRagdoll*>(rcx);
    const int cosmetics=SkinChanger::RagdollEffectsFor(pRagdoll);
    const bool cosmeticGold=cosmetics & SkinModel::DeathGold;
    auto applyCosmetics=[&]
    {
        if(!cosmetics)return;
        pRagdoll->m_bGib()=false;
        if(cosmeticGold){pRagdoll->m_bGoldRagdoll()=true;pRagdoll->m_bIceRagdoll()=false;}
        else if(cosmetics & SkinModel::DeathIce){pRagdoll->m_bGoldRagdoll()=false;pRagdoll->m_bIceRagdoll()=true;}
        if(cosmetics & SkinModel::DeathAsh)pRagdoll->m_bBecomeAsh()=true;
        if(cosmetics & SkinModel::DeathPlasma)pRagdoll->m_bDissolving()=true;
    };
    if (!Vars::Visuals::Effects::RagdollEffects.Value)
    {
        applyCosmetics();
        CALL_ORIGINAL(rcx);
        SkinChanger::RagdollCreated(pRagdoll,cosmetics);
        return;
    }
	pRagdoll->m_bGib() = false;
	pRagdoll->m_bBurning() = Vars::Visuals::Effects::RagdollEffects.Value & Vars::Visuals::Effects::RagdollEffectsEnum::Burning;
	pRagdoll->m_bElectrocuted() = Vars::Visuals::Effects::RagdollEffects.Value & Vars::Visuals::Effects::RagdollEffectsEnum::Electrocuted;
	pRagdoll->m_bBecomeAsh() = Vars::Visuals::Effects::RagdollEffects.Value & Vars::Visuals::Effects::RagdollEffectsEnum::Ash;
	pRagdoll->m_bDissolving() = Vars::Visuals::Effects::RagdollEffects.Value & Vars::Visuals::Effects::RagdollEffectsEnum::Dissolve;
	pRagdoll->m_bGoldRagdoll() = cosmeticGold || (Vars::Visuals::Effects::RagdollEffects.Value & Vars::Visuals::Effects::RagdollEffectsEnum::Gold);
	pRagdoll->m_bIceRagdoll() = Vars::Visuals::Effects::RagdollEffects.Value & Vars::Visuals::Effects::RagdollEffectsEnum::Ice;
    applyCosmetics();

	/*
	pRagdoll->m_vecForce() *= Vars::Visuals::Ragdolls::Force.Value;
	pRagdoll->m_vecForce().x *= Vars::Visuals::Ragdolls::ForceHorizontal.Value;
	pRagdoll->m_vecForce().y *= Vars::Visuals::Ragdolls::ForceHorizontal.Value;
	pRagdoll->m_vecForce().z *= Vars::Visuals::Ragdolls::ForceVertical.Value;
	*/

	CALL_ORIGINAL(rcx);
    SkinChanger::RagdollCreated(pRagdoll,cosmetics);
}
