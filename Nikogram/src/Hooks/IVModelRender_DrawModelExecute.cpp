#include "../SDK/SDK.h"

#include "../Features/Visuals/Chams/Chams.h"
#include "../Features/Visuals/Glow/Glow.h"
#include "../Features/Visuals/Groups/Groups.h"
#include "../Features/Visuals/Materials/Materials.h"
#include "../Features/Visuals/CameraWindow/CameraWindow.h"
#include "../Features/SkinChanger/SkinChanger.h"
#include "../Features/SkinChanger/RenderPolicy.h"
#include "../Features/Visuals/Dapper/WorldPhoto.h"
#include "../Features/Visuals/RenderAudit.h"

MAKE_SIGNATURE(CBaseAnimating_InternalDrawModel, "client.dll", "48 8B C4 55 56 48 8D 6C 24 ? 48 81 EC ? ? ? ? 44 8B 81", 0x0);
MAKE_SIGNATURE(CBaseViewModel_DrawModel, "client.dll", "40 53 55 56 48 83 EC ? 80 B9", 0x0);
MAKE_SIGNATURE(SkinPaint_WeaponSkinBind, "client.dll", "48 8B C4 55 53 48 8D A8 F8 FE FF FF 48 81 EC F8 01 00 00 48 89 70 08 48 8B F2", 0x0);
MAKE_SIGNATURE(Skin_KillstreakSheenBind, "client.dll", "4C 8B DC 49 89 53 10 55 57 41 54 49 8D 6B A1 48 81 EC F0 00 00 00 48 8B 41 08 48 8B FA 4C 8B E1", 0x0);

MAKE_HOOK(Skin_KillstreakSheenBind, S::Skin_KillstreakSheenBind(), void, void* proxy,void* renderable)
{
    HookLifetime::Scope hookLifetimeScope;
    // Native proxy owns the sheen texture, bounds, timing and material values.
    // Attribute answers exist only during this bind, for the verified weapon.
    SkinChanger::KillstreakAttributeScope scope(SkinChanger::CurrentDrawKillstreak());
    CALL_ORIGINAL(proxy,renderable);
}

MAKE_HOOK(SkinPaint_WeaponSkinBind, S::SkinPaint_WeaponSkinBind(), void, void* rcx,void* renderable)
{
    HookLifetime::Scope hookLifetimeScope;
    CALL_ORIGINAL(rcx,renderable);
    // Native inventory binding may restore the stock texture. Reapply only the
    // verified weapon surfaces in the current draw, without reading proxy layout.
    SkinChanger::ApplyPaintDraw();
}

static bool s_bDrawingViewmodel = false;
static thread_local bool s_bDrawingLocalViewmodel = false;
static thread_local bool s_bDrawingViewmodelEffect = false;

MAKE_HOOK(IVModelRender_DrawModelExecute, U::Memory.GetVirtual(I::ModelRender, 19), void,
	void* rcx, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld)
{
	DEBUG_RETURN(IVModelRender_DrawModelExecute, rcx, pState, pInfo, pBoneToWorld);

	const bool gameUI=I::EngineVGui->IsGameUIVisible();
	if (SkinRender::BypassMenuCosmetics(gameUI,Vars::Misc::SkinChanger::Enabled.Value) || SDK::CleanScreenshot()
		|| F::CameraWindow.m_bDrawing || !F::Materials.m_bLoaded || G::Unload)
	{
		SkinChanger::ObserveDraw(pInfo, pBoneToWorld, "normal_render_bypassed_ui_screenshot_camera_materials_or_unload");
		return CALL_ORIGINAL(rcx, pState, pInfo, pBoneToWorld);
	}

    std::array<matrix3x4,MAXSTUDIOBONES> corpseBones;
    std::array<matrix3x4,MAXSTUDIOBONES> playerBones;
    auto playerClient=I::ClientEntityList->GetClientEntity(pInfo.entity_index);
    auto playerEntity=playerClient?playerClient->As<CBaseEntity>():nullptr;
    if(playerEntity&&playerEntity->IsPlayer()&&SkinChanger::PreparePlayerBones(playerEntity->As<CTFPlayer>(),playerBones))
        pBoneToWorld=playerBones.data();
    matrix3x4* corpseDrawBones=pBoneToWorld;IMaterial* corpseMaterial=nullptr;
    SkinChanger::PrepareDeathDraw(pState,pInfo,pBoneToWorld,corpseBones,corpseDrawBones,corpseMaterial,
        F::Chams.m_bRendering||F::Glow.m_bRendering||s_bDrawingViewmodelEffect);
    pBoneToWorld=corpseDrawBones;
    struct CorpseMaterialScope
    {
        IMaterial* previous=nullptr;OverrideType_t type=OVERRIDE_NORMAL;bool changed=false;
        explicit CorpseMaterialScope(IMaterial* material):changed(material!=nullptr)
        {if(changed){I::ModelRender->GetMaterialOverride(&previous,&type);I::ModelRender->ForcedMaterialOverride(material);}}
        ~CorpseMaterialScope(){if(changed)I::ModelRender->ForcedMaterialOverride(previous,type);}
    } corpseScope(corpseMaterial);
	if (!gameUI && F::Chams.m_bRendering)
	{
		SkinChanger::ObserveDraw(pInfo, pBoneToWorld, "chams_pass");
		return F::Chams.RenderHandler(pState, pInfo, pBoneToWorld, s_bDrawingLocalViewmodel);
	}
	if (!gameUI && F::Glow.m_bRendering)
	{
		SkinChanger::ObserveDraw(pInfo, pBoneToWorld, "glow_pass");
		return F::Glow.RenderHandler(pState, pInfo, pBoneToWorld, s_bDrawingLocalViewmodel);
	}

	if (!gameUI && F::Groups.GroupsActive() && F::Chams.m_mEntities.contains(pInfo.entity_index))
	{
		SkinChanger::ObserveDraw(pInfo, pBoneToWorld, "chams_entity_suppressed");
		return;
	}

	auto pClient = I::ClientEntityList->GetClientEntity(pInfo.entity_index);
	auto pEntity = pClient ? pClient->As<CBaseEntity>() : nullptr;
	if (!gameUI && !s_bDrawingViewmodelEffect && pEntity && pEntity->IsWearableVM() /*pEntity->IsViewmodel()*/)
	{
		{
			SkinRender::ViewmodelDrawScope effectScope(s_bDrawingViewmodelEffect, true);
			F::Glow.RenderViewmodel(pState, pInfo, pBoneToWorld, s_bDrawingLocalViewmodel);
		}
		if (F::Chams.RenderViewmodel(pState, pInfo, pBoneToWorld, s_bDrawingLocalViewmodel))
		{
			SkinChanger::ObserveDraw(pInfo, pBoneToWorld, "viewmodel_chams_handled");
			return;
		}
	}

	// Some map fixtures (for example resupply lockers) are dynamic props, not
	// members of CStaticPropMgr's array. Restrict this route by model namespace;
	// never apply the world-photo option to players, weapons or viewmodels.
	const auto mapModel=pInfo.pModel?I::ModelInfoClient->GetModelName(pInfo.pModel):nullptr;
	PhotoRenderPolicy::PropScope<IVModelRender,IMaterial,OverrideType_t> mapPhoto(I::ModelRender,
		DapperWorld::PropMaterial(gameUI||s_bDrawingViewmodelEffect||!mapModel
			||!PhotoRenderPolicy::MapProp(mapModel)||corpseMaterial!=nullptr),OVERRIDE_NORMAL);
	if(mapPhoto.Active()){RenderAudit::WorldPropDraw(true);return CALL_ORIGINAL(rcx,pState,pInfo,pBoneToWorld);}

	if (Vars::Misc::SkinChanger::Enabled.Value && SkinRender::AllowCosmeticRender(F::Chams.m_bRendering,F::Glow.m_bRendering))
	{
		// Keep target headers/meshes resident until the replacement draw finishes.
		SkinRender::ModelCacheScope<IMDLCache> cacheScope(static_cast<IMDLCache*>(I::MDLCache));
		SkinChanger::ObserveDraw(pInfo, pBoneToWorld, "prepare");
		DrawModelState_t skinState{};
		ModelRenderInfo_t skinInfo{};
		std::array<matrix3x4, MAXSTUDIOBONES> skinBones;
		matrix3x4* drawBones = pBoneToWorld;
		{
			const bool replaced=SkinChanger::Prepare(pState, pInfo, pBoneToWorld, skinState, skinInfo, skinBones, drawBones, s_bDrawingLocalViewmodel, false, s_bDrawingViewmodelEffect);
			SkinChanger::PaintDrawScope paintScope(replaced?skinInfo:pInfo,s_bDrawingLocalViewmodel&&!s_bDrawingViewmodelEffect);
			if(replaced)CALL_ORIGINAL(rcx, skinState, skinInfo, drawBones);
			else CALL_ORIGINAL(rcx, pState, pInfo, pBoneToWorld);
		}
		// Botkillers are a separate wearable model; drawing only the unchanged
		// stock weapon model cannot show their head/finish.
		int accessoryCount=1;
		for(int index=0;index<accessoryCount&&index<SkinModel::MaxCosmeticAttachments;++index)
			if (SkinChanger::Prepare(pState, pInfo, pBoneToWorld, skinState, skinInfo, skinBones, drawBones, s_bDrawingLocalViewmodel, true, s_bDrawingViewmodelEffect,index,&accessoryCount))
				CALL_ORIGINAL(rcx, skinState, skinInfo, drawBones);
        if(SkinChanger::PreparePipBoyDraw(pState,pInfo,pBoneToWorld,skinState,skinInfo,skinBones,s_bDrawingLocalViewmodel))
            CALL_ORIGINAL(rcx,skinState,skinInfo,skinBones.data());
		return;
	}
	CALL_ORIGINAL(rcx, pState, pInfo, pBoneToWorld);
    DrawModelState_t pipState{};ModelRenderInfo_t pipInfo{};std::array<matrix3x4,MAXSTUDIOBONES> pipBones;
    SkinRender::ModelCacheScope<IMDLCache> pipCache(static_cast<IMDLCache*>(I::MDLCache));
    if(SkinChanger::PreparePipBoyDraw(pState,pInfo,pBoneToWorld,pipState,pipInfo,pipBones,s_bDrawingLocalViewmodel))
        CALL_ORIGINAL(rcx,pipState,pipInfo,pipBones.data());
}

MAKE_HOOK(CBaseAnimating_InternalDrawModel, S::CBaseAnimating_InternalDrawModel(), int,
	void* rcx, int flags)
{
	DEBUG_RETURN(CBaseAnimating_InternalDrawModel, rcx, flags);

	if (!s_bDrawingViewmodel || I::EngineVGui->IsGameUIVisible() /*|| !(flags & STUDIO_RENDER)*/)
		return CALL_ORIGINAL(rcx, flags);

	int iReturn;
	{
		SkinRender::ViewmodelDrawScope effectScope(s_bDrawingViewmodelEffect, true);
		F::Glow.RenderViewmodel(rcx, flags);
	}
	if (F::Chams.RenderViewmodel(rcx, flags, &iReturn))
		return iReturn;

	return CALL_ORIGINAL(rcx, 1);
}

MAKE_HOOK(CBaseViewModel_DrawModel, S::CBaseViewModel_DrawModel(), int,
	void* rcx, int flags)
{
	DEBUG_RETURN(CBaseViewModel_DrawModel, rcx, flags);

	if (s_bDrawingViewmodel || SkinRender::BypassMenuCosmetics(I::EngineVGui->IsGameUIVisible(),Vars::Misc::SkinChanger::Enabled.Value) || SDK::CleanScreenshot()
		|| F::CameraWindow.m_bDrawing || !F::Materials.m_bLoaded || G::Unload)
		return CALL_ORIGINAL(rcx, flags);

	s_bDrawingViewmodel = true;
	auto local = H::Entities.GetLocal();
	auto viewmodel = local ? local->m_hViewModel().Get() : nullptr;
	auto renderable = viewmodel ? static_cast<IClientRenderable*>(viewmodel) : nullptr;
	const bool isMainViewmodel = local && local->IsAlive()
		&& SkinRender::MatchViewmodel(rcx, viewmodel, renderable) != SkinRender::ViewmodelPointerKind::None;
	const bool isLocalViewmodel=isMainViewmodel;
	SkinChanger::ObserveViewmodelDraw(rcx, isLocalViewmodel);
	SkinRender::ViewmodelDrawScope localScope(s_bDrawingLocalViewmodel, isLocalViewmodel);
	SkinRender::ModelCacheScope<IMDLCache> animationCache(static_cast<IMDLCache*>(I::MDLCache));
	auto handSequence=isMainViewmodel?SkinChanger::RenderHandSequence(viewmodel->As<CBaseAnimating>()):std::nullopt;
    auto handCycle=handSequence?SkinChanger::RenderHandCycle(viewmodel->As<CBaseAnimating>(),*handSequence):std::nullopt;
	SkinRender::TimedSequenceScope handScope(isMainViewmodel?viewmodel->As<CBaseAnimating>():nullptr,
		handSequence.value_or(-1),handCycle,[](CBaseAnimating* entity){entity->InvalidateBoneCache();});
	int iReturn = CALL_ORIGINAL(rcx, flags);
	s_bDrawingViewmodel = false;
	return iReturn;
}
