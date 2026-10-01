#include "../SDK/SDK.h"
#include "../Features/SkinChanger/SkinChanger.h"

MAKE_SIGNATURE(CAttributeManager_AttribHookInt, "client.dll", "4C 8B DC 49 89 5B ? 49 89 6B ? 49 89 73 ? 57 41 54 41 55 41 56 41 57 48 83 EC ? 48 8B 3D ? ? ? ? 4C 8D 35", 0x0);
MAKE_SIGNATURE(CTFPlayer_FireEvent_AttribHookValue_Call, "client.dll", "8B F8 83 BE", 0x0);
// Same verified native float hook used by SDK::AttribHookValue.
MAKE_SIGNATURE(Skin_CosmeticAttribFloat, "client.dll", "4C 8B DC 49 89 5B ? 49 89 6B ? 56 57 41 54 41 56 41 57 48 83 EC ? 48 8B 3D ? ? ? ? 4C 8D 35", 0x0);
// CalcCustomBuildMenuLayout reads static item-definition attributes directly,
// bypassing the global attribute hook. Verified native client-only HUD reader.
MAKE_SIGNATURE(Skin_PipBoyBuildLayout, "client.dll", "41 56 48 81 EC 80 00 00 00 E8 ? ? ? ? 4C 8B F0 48 85 C0 75 0A 48 81 C4 80 00 00 00 41 5E C3",0x0);
MAKE_HOOK(Skin_PipBoyBuildLayout,S::Skin_PipBoyBuildLayout(),int)
{
    HookLifetime::Scope hookLifetimeScope;
    if(SkinChanger::BuildingMenuPipBoy(H::Entities.GetLocal()))return 1;
    return CALL_ORIGINAL();
}
// The installed client inlines the layout query in SetVisible. Its verified
// panel readers cache the layout at +0x304 / building-card +0x248.
MAKE_SIGNATURE(Skin_BuildMenuScheme, "client.dll", "48 8B C4 48 89 50 10 48 89 48 08 53 56 41 54 41 56 48 83 EC 48 48 89 68 18 4C 8B F1",0x0);
MAKE_SIGNATURE(Skin_BuildCardScheme, "client.dll", "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 30 44 8B 81 48 02 00 00",0x0);
MAKE_SIGNATURE(Skin_BuildMenuVisible, "client.dll", "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 20 57 41 54 41 55 41 56 41 57 48 83 EC 60 4C 8D 61 B8 44 0F B6 EA",0x0);
MAKE_HOOK(Skin_BuildMenuScheme,S::Skin_BuildMenuScheme(),void,void* panel,void* scheme)
{
    HookLifetime::Scope hookLifetimeScope;
    auto& layout=*reinterpret_cast<int*>(uintptr_t(panel)+0x304);const int saved=layout;
    if(SkinChanger::BuildingMenuPipBoy(H::Entities.GetLocal()))layout=1;
    CALL_ORIGINAL(panel,scheme);layout=saved;
}
MAKE_HOOK(Skin_BuildCardScheme,S::Skin_BuildCardScheme(),void,void* panel,void* scheme)
{
    HookLifetime::Scope hookLifetimeScope;
    auto& layout=*reinterpret_cast<int*>(uintptr_t(panel)+0x248);const int saved=layout;
    if(SkinChanger::BuildingMenuPipBoy(H::Entities.GetLocal()))layout=1;
    CALL_ORIGINAL(panel,scheme);layout=saved;
}
MAKE_HOOK(Skin_BuildMenuVisible,S::Skin_BuildMenuVisible(),void,void* panel,bool visible)
{
    HookLifetime::Scope hookLifetimeScope;
    auto& layout=*reinterpret_cast<int*>(uintptr_t(panel)+0x304);
    const bool pip=SkinChanger::BuildingMenuPipBoy(H::Entities.GetLocal());
    if(pip&&visible)layout=1; // Force the native inline comparison to reload its scheme.
    CALL_ORIGINAL(panel,visible);
    if(pip&&visible){layout=1;SkinChanger::ObserveCosmeticEffect("pipboy_menu","native_build_menu_and_cards_layout=1");}
}
MAKE_HOOK(Skin_CosmeticAttribFloat, S::Skin_CosmeticAttribFloat(), float,
    float value,const char* name,void* entity,void* buffer,bool globalString)
{
    HookLifetime::Scope hookLifetimeScope;
    if(auto voice=SkinChanger::CosmeticVoiceAttribute(name,entity))return *voice;
    return CALL_ORIGINAL(value,name,entity,buffer,globalString);
}

static inline int ColorToInt(Color_t col)
{
    return col.r << 16 | col.g << 8 | col.b;
}

MAKE_HOOK(CAttributeManager_AttribHookInt, S::CAttributeManager_AttribHookInt(), int,
	int value, const char* name, void* econent, void* buffer, bool isGlobalConstString)
{
	DEBUG_RETURN(CAttributeManager_AttribHookInt, value, name, econent, buffer, isGlobalConstString);
	// Only active within a verified weapon sheen bind or native eye-effect update.
	// No inventory attribute list, network field or gameplay query is changed.
	if(auto cosmetic=SkinChanger::KillstreakAttribute(name,econent))return *cosmetic;
    if(auto voice=SkinChanger::CosmeticVoiceAttribute(name,econent))return int(*voice);
    // Valve's build HUD reads this UI-only attribute to choose its resource
    // layout. Never override building replacement, costs or weapon attributes.
    if(name&&!strcmp(name,"set_custom_buildmenu")&&SkinChanger::BuildingMenuPipBoy(econent))return 1;

	const auto dwRetAddr = uintptr_t(_ReturnAddress());
	const auto dwDesired = S::CTFPlayer_FireEvent_AttribHookValue_Call();

	if (dwRetAddr == dwDesired && Vars::Visuals::Effects::SpellFootsteps.Value
		&& econent == H::Entities.GetLocal() && FNV1A::Hash32(name) == FNV1A::Hash32Const("halloween_footstep_type"))
	{
		switch (Vars::Visuals::Effects::SpellFootsteps.Value)
		{
		case Vars::Visuals::Effects::SpellFootstepsEnum::Color: return ColorToInt(Vars::Colors::SpellFootstep.Value);
		case Vars::Visuals::Effects::SpellFootstepsEnum::Team: return 1;
		case Vars::Visuals::Effects::SpellFootstepsEnum::Halloween: return 2;
		}
	}

	return CALL_ORIGINAL(value, name, econent, buffer, isGlobalConstString);
}
