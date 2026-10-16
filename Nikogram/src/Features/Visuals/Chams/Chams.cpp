#include "../../Backtrack/NamedPlayerTraceDiagnostics.h"
#include "Chams.h"
#include "../RenderState.h"

using ModelStateScope = RenderState::ModelScope<IVRenderView, IVModelRender, IMaterial, OverrideType_t>;

#include "../Groups/Groups.h"
#include "../Materials/Materials.h"
#include "../FakeAngle/FakeAngle.h"
#include "../../Backtrack/Backtrack.h"
#include "../../SkinChanger/SkinChanger.h"
#include "../../SkinChanger/RenderPolicy.h"

namespace
{
    bool IntentionallyHidden(const Chams_t& chams)
    {
        return std::all_of(chams.Visible.begin(), chams.Visible.end(), [](const auto& layer)
        { return layer.first == "None" || layer.second.a == 0; });
    }
}

bool CChams::ValidEntity(const ChamsInfo_t& info) const
{
    if (info.m_iServer != I::ClientState->m_nServerCount) return false;
    auto client = I::ClientEntityList->GetClientEntity(info.m_iIndex);
    auto entity = client ? client->As<CBaseEntity>() : nullptr;
    return entity && entity == info.m_pEntity
        && uint32_t(entity->GetRefEHandle().ToInt()) == info.m_uHandle
        && entity->GetModel() == info.m_pModel && entity->GetClientRenderable() == info.m_pRenderable && !entity->IsDormant();
}

bool CChams::Eligible(CBaseEntity* entity, const Chams_t& chams) const
{
    auto local = H::Entities.GetLocal(); Group_t* group = nullptr;
    // Resolve against the current group vector, not cached group pointers that
    // a menu edit may have invalidated since the last network update.
    return local && entity->ShouldDraw() && F::Groups.GetGroup(entity, local, group, true)
        && group->m_tChams() && group->m_tChams == chams;
}

void CChams::Commit(CBaseEntity* entity, const Chams_t& chams, const Attempt_t* attempt)
{
    if (attempt)
    {
        auto client = I::ClientEntityList->GetClientEntity(attempt->index);
        auto current = client ? client->As<CBaseEntity>() : nullptr;
        if (!current || current != entity || current != attempt->entity
            || I::ClientState->m_nServerCount != attempt->server
            || uint32_t(current->GetRefEHandle().ToInt()) != attempt->handle
            || current->GetModel() != attempt->model || current->GetClientRenderable() != attempt->renderable) return;
    }
    m_mReplacements.insert_or_assign(entity->entindex(), Replacement_t{
        ChamsInfo_t(entity, &chams), I::GlobalVars->framecount, IntentionallyHidden(chams) });
}

void CChams::Acknowledge(const ModelRenderInfo_t& info)
{
    if (!m_pAttempt || m_iFlags || info.entity_index != m_pAttempt->index
        || info.pModel != m_pAttempt->model || info.pRenderable != m_pAttempt->renderable) return;
    auto client = I::ClientEntityList->GetClientEntity(info.entity_index);
    auto entity = client ? client->As<CBaseEntity>() : nullptr;
    if (entity && entity == m_pAttempt->entity && uint32_t(entity->GetRefEHandle().ToInt()) == m_pAttempt->handle
        && entity->GetModel() == m_pAttempt->model && entity->GetClientRenderable() == m_pAttempt->renderable)
        m_pAttempt->forwarded = true;
}

bool CChams::ShouldSuppress(const ModelRenderInfo_t& info)
{
    auto found = m_mReplacements.find(info.entity_index);
    if (found == m_mReplacements.end()) return false;
    const auto& replacement = found->second;
    const auto age = int64_t(I::GlobalVars->framecount) - replacement.frame;
    // The ordinary scene precedes post-effects replacement. Keep the preceding
    // frame's acknowledgement, never an unbounded or recycled-entity marker.
    if (!F::Groups.GroupsActive() || age < 0 || age > 1 || !ValidEntity(replacement.info)
        || info.pModel != replacement.info.m_pModel
        || info.pRenderable != replacement.info.m_pRenderable
        || !Eligible(replacement.info.m_pEntity, replacement.info.m_tChams))
    { m_mReplacements.erase(found); return false; }
    return true;
}

void CChams::Reset()
{
    m_mReplacements.clear(); m_vEntities.clear();
    m_pAttempt = nullptr; m_iFlags = 0; m_bRendering = false;
}

void CChams::DrawModel(CBaseEntity* pEntity, const Chams_t& tChams, IMatRenderContext* pRenderContext, int iModel, bool bTwoModel)
{
    NamedPlayerTraceDiagnostics::RenderScope diagnosticRoute(m_iFlags?3:1,iModel==ModelEnum::Occluded);
	if (!m_iFlags && iModel == ModelEnum::Visible && IntentionallyHidden(tChams))
		Commit(pEntity, tChams);

	bool bOccluded = !tChams.Occluded.empty();
	bool bSame = tChams.Visible == tChams.Occluded;
	bTwoModel &= bOccluded && !bSame;

	ModelStateScope renderState(I::RenderView, I::ModelRender);
	switch (iModel)
	{
	case ModelEnum::Visible:
	{
		if (!bTwoModel)
		{
			if (bSame)
				return;
		}
		else
		{
			pRenderContext->SetStencilEnable(true);
			pRenderContext->SetStencilCompareFunction(STENCILCOMPARISONFUNCTION_ALWAYS);
			pRenderContext->SetStencilPassOperation(STENCILOPERATION_REPLACE);
			pRenderContext->SetStencilFailOperation(STENCILOPERATION_KEEP);
			pRenderContext->SetStencilZFailOperation(STENCILOPERATION_KEEP);
			pRenderContext->SetStencilReferenceValue(1);
			pRenderContext->SetStencilWriteMask(0xFF);
			pRenderContext->SetStencilTestMask(0x0);
		}

		auto& vMaterials = tChams.GetVisible();
		for (auto& [sName, tColor] : vMaterials)
		{
			auto pMaterial = F::Materials.GetMaterial(FNV1A::Hash32(sName.c_str()));

			F::Materials.SetColor(pMaterial, tColor);
			I::ModelRender->ForcedMaterialOverride(pMaterial ? pMaterial->m_pMaterial : nullptr);
			if (pMaterial)
			{
				if (pMaterial->m_bInvertCull)
					pRenderContext->CullMode(MATERIAL_CULLMODE_CW);
				if (pMaterial->m_bBlockOccluded)
					pRenderContext->SetStencilZFailOperation(STENCILOPERATION_REPLACE);
			}

			{
				SkinRender::ViewmodelDrawScope drawScope(m_bRendering, true);
				Attempt_t attempt{pEntity, pEntity->entindex(), uint32_t(pEntity->GetRefEHandle().ToInt()), pEntity->GetModel(), pEntity->GetClientRenderable(), I::ClientState->m_nServerCount};
				SkinRender::DrawStateScope attemptScope(m_pAttempt, &attempt);
				const int drawn = pEntity->DrawModel(STUDIO_RENDER);
				if (!m_iFlags && drawn > 0 && attempt.forwarded && sName != "None" && tColor.a > 0)
					Commit(pEntity, tChams, &attempt);
			}

			if (pMaterial)
			{
				if (pMaterial->m_bInvertCull)
					pRenderContext->CullMode(MATERIAL_CULLMODE_CCW);
				if (pMaterial->m_bBlockOccluded)
					pRenderContext->SetStencilZFailOperation(STENCILOPERATION_KEEP);
			}
		}

		if (bTwoModel)
			pRenderContext->SetStencilEnable(false);
		break;
	}
	case ModelEnum::Occluded:
	{
		if (!bTwoModel)
		{
			if (!bOccluded)
				return;
		}
		else
		{
			pRenderContext->SetStencilEnable(true);
			pRenderContext->SetStencilCompareFunction(STENCILCOMPARISONFUNCTION_EQUAL);
			pRenderContext->SetStencilPassOperation(STENCILOPERATION_KEEP);
			pRenderContext->SetStencilFailOperation(STENCILOPERATION_KEEP);
			pRenderContext->SetStencilZFailOperation(STENCILOPERATION_KEEP);
			pRenderContext->SetStencilReferenceValue(0);
			pRenderContext->SetStencilWriteMask(0x0);
			pRenderContext->SetStencilTestMask(0xFF);
		}
		pRenderContext->DepthRange(0.f, 0.2f);

		auto& vMaterials = tChams.GetOccluded();
		for (auto& [sName, tColor] : vMaterials)
		{
			auto pMaterial = F::Materials.GetMaterial(FNV1A::Hash32(sName.c_str()));

			F::Materials.SetColor(pMaterial, tColor);
			I::ModelRender->ForcedMaterialOverride(pMaterial ? pMaterial->m_pMaterial : nullptr);
			if (pMaterial && pMaterial->m_bInvertCull)
				pRenderContext->CullMode(MATERIAL_CULLMODE_CW);

			{
				SkinRender::ViewmodelDrawScope drawScope(m_bRendering, true);
				Attempt_t attempt{pEntity, pEntity->entindex(), uint32_t(pEntity->GetRefEHandle().ToInt()), pEntity->GetModel(), pEntity->GetClientRenderable(), I::ClientState->m_nServerCount};
				SkinRender::DrawStateScope attemptScope(m_pAttempt, &attempt);
				const int drawn = pEntity->DrawModel(STUDIO_RENDER);
				if (!m_iFlags && drawn > 0 && attempt.forwarded && sName != "None" && tColor.a > 0)
					Commit(pEntity, tChams, &attempt);
			}

			if (pMaterial && pMaterial->m_bInvertCull)
				pRenderContext->CullMode(MATERIAL_CULLMODE_CCW);
		}

		if (bTwoModel)
			pRenderContext->SetStencilEnable(false);
		pRenderContext->DepthRange(0.f, 1.f);
	}
	}
}



void CChams::Store(CTFPlayer* pLocal)
{
	m_vEntities.clear();
	if (!pLocal || !F::Groups.GroupsActive())
		return;

	for (auto& [pEntity, pGroup] : F::Groups.GetGroup())
	{
		if (pEntity->IsDormant() || !pEntity->ShouldDraw())
			continue;

		if (pGroup->m_tChams() && !pEntity->IsWearableVM()
			&& SDK::IsOnScreen(pEntity, pEntity->IsBaseCombatWeapon() || pEntity->IsWearable()))
			m_vEntities.emplace_back(pEntity, &pGroup->m_tChams);

		if (pEntity->IsPlayer() && pEntity != pLocal && pGroup->m_iBacktrack & BacktrackEnum::Enabled && pGroup->m_tBacktrackChams(false)
			&& (F::Backtrack.GetFakeLatency() || F::Backtrack.GetFakeInterp() > G::Lerp || F::Backtrack.GetWindow()))
		{	// backtrack
			auto pWeapon = H::Entities.GetWeapon();
			if (pWeapon && (pGroup->m_iBacktrack & BacktrackEnum::Always || G::PrimaryWeaponType != EWeaponType::PROJECTILE))
			{
				bool bShowFriendly = false, bShowEnemy = true;
				if (G::PrimaryWeaponType == EWeaponType::MELEE && SDK::AttribHookValue(0, "speed_buff_ally", pWeapon) > 0)
					bShowFriendly = true;
				else if (pWeapon->GetWeaponID() == TF_WEAPON_MEDIGUN)
					bShowFriendly = true, bShowEnemy = false;

				if (bShowEnemy && pEntity->m_iTeamNum() != pLocal->m_iTeamNum() || bShowFriendly && pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
					m_vEntities.emplace_back(pEntity, &pGroup->m_tBacktrackChams, pGroup->m_iBacktrack);
			}
		}
	}

	Group_t* pGroup = nullptr;
	if (F::FakeAngle.bDrawChams && F::FakeAngle.bBonesSetup
		&& F::Groups.GetGroup(TargetsEnum::FakeAngle, pGroup) && pGroup->m_tChams(false))
	{	// fakeangle
		m_vEntities.emplace_back(pLocal, &pGroup->m_tChams, 1);
	}
}

void CChams::RenderMain()
{
	m_mReplacements.clear();
	if (!F::Groups.GroupsActive()) return;
	RenderState::ContextScope context(I::MaterialSystem->GetRenderContext());
	auto pRenderContext = context.Get();
	if (!pRenderContext)
		return;

	if (m_vEntities.empty())
		return;

	// Store runs on network updates. Keep its entity identities, but refresh the
	// owned policy for this render frame so menu edits do not skip chams until
	// another network update. Both passes use the same safe snapshot.
	auto frameEntities = m_vEntities;
	std::erase_if(frameEntities, [&](ChamsInfo_t& info)
	{
		if (!ValidEntity(info)) return true;
		if (info.m_iFlags) return false;
		auto local = H::Entities.GetLocal(); Group_t* group = nullptr;
		if (!local || !info.m_pEntity->ShouldDraw()
			|| !F::Groups.GetGroup(info.m_pEntity, local, group, true) || !group->m_tChams()) return true;
		info.m_tChams = group->m_tChams;
		return false;
	});

	pRenderContext->ClearBuffers(false, false, true);

	for (auto& tInfo : frameEntities)
	{
		if (!ValidEntity(tInfo) || (!tInfo.m_iFlags && !Eligible(tInfo.m_pEntity, tInfo.m_tChams))) continue;
		if (!tInfo.m_iFlags)
			DrawModel(tInfo.m_pEntity, tInfo.m_tChams, pRenderContext, ModelEnum::Visible, true);
		else
		{
			m_iFlags = tInfo.m_iFlags;

			auto pPlayer = tInfo.m_pEntity->As<CTFPlayer>();
			const float flOldInvisibility = pPlayer->m_flInvisibility();
			pPlayer->m_flInvisibility() = 0.f;
			DrawModel(tInfo.m_pEntity, tInfo.m_tChams, pRenderContext, ModelEnum::Visible, true);
			pPlayer->m_flInvisibility() = flOldInvisibility;

			m_iFlags = false;
		}
	}
	for (auto& tInfo : frameEntities)
	{
		if (!ValidEntity(tInfo) || (!tInfo.m_iFlags && !Eligible(tInfo.m_pEntity, tInfo.m_tChams))) continue;
		if (!tInfo.m_iFlags)
			DrawModel(tInfo.m_pEntity, tInfo.m_tChams, pRenderContext, ModelEnum::Occluded, true);
		else
		{
			m_iFlags = tInfo.m_iFlags;

			auto pPlayer = tInfo.m_pEntity->As<CTFPlayer>();
			const float flOldInvisibility = pPlayer->m_flInvisibility();
			pPlayer->m_flInvisibility() = 0.f;
			DrawModel(tInfo.m_pEntity, tInfo.m_tChams, pRenderContext, ModelEnum::Occluded, true);
			pPlayer->m_flInvisibility() = flOldInvisibility;

			m_iFlags = false;
		}
	}

	pRenderContext->ClearBuffers(false, false, true);
}

void CChams::RenderBacktrack(const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo)
{
	RenderState::ContextScope context(I::MaterialSystem->GetRenderContext());
	auto pRenderContext = context.Get();
	if (!pRenderContext)
		return;

	auto pEntity = I::ClientEntityList->GetClientEntity(pInfo.entity_index)->As<CTFPlayer>();
	if (!pEntity || !pEntity->IsPlayer())
		return;

	std::vector<TickRecord*> vRecords = {};
	if (!F::Backtrack.GetRecords(pEntity, vRecords))
		return;
	vRecords = F::Backtrack.GetValidRecords(vRecords);
	if (!vRecords.size())
		return;

	bool bDrawLast = m_iFlags & BacktrackEnum::Last;
	bool bDrawFirst = m_iFlags & BacktrackEnum::First;

	float flOriginalBlend = I::RenderView->GetBlend();
	auto fDrawModel = [&](Vec3& vOrigin, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld, float flBlend)
	{
		if (!SDK::IsOnScreen(pEntity, vOrigin))
			return;

		I::RenderView->SetBlend(flBlend * flOriginalBlend);
		static auto IVModelRender_DrawModelExecute = U::Hooks.m_mHooks["IVModelRender_DrawModelExecute"];
		NamedPlayerTraceDiagnostics::ObserveDraw(pState,pInfo,pBoneToWorld);
		IVModelRender_DrawModelExecute->Call<void>(I::ModelRender, pState, pInfo, pBoneToWorld);
	};
	if (!bDrawLast && !bDrawFirst)
	{
		for (auto pRecord : vRecords)
		{
			if (float flBlend = Math::RemapVal(pEntity->GetAbsOrigin().DistToSqr(pRecord->m_vOrigin), 1.f, 576.f, 0.f, 1.f))
				fDrawModel(pRecord->m_vOrigin, pState, pInfo, pRecord->m_aBones, flBlend);
		}
	}
	else
	{
		if (bDrawLast)
		{
			auto pRecord = vRecords.back();
			if (float flBlend = Math::RemapVal(pEntity->GetAbsOrigin().DistToSqr(pRecord->m_vOrigin), 1.f, 576.f, 0.f, 1.f))
				fDrawModel(pRecord->m_vOrigin, pState, pInfo, pRecord->m_aBones, flBlend);
		}
		if (bDrawFirst)
		{
			auto pRecord = vRecords.front();
			if (float flBlend = Math::RemapVal(pEntity->GetAbsOrigin().DistToSqr(pRecord->m_vOrigin), 1.f, 576.f, 0.f, 1.f))
				fDrawModel(pRecord->m_vOrigin, pState, pInfo, pRecord->m_aBones, flBlend);
		}
	}
	I::RenderView->SetBlend(flOriginalBlend);
}
void CChams::RenderFakeAngle(const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo)
{
	static auto IVModelRender_DrawModelExecute = U::Hooks.m_mHooks["IVModelRender_DrawModelExecute"];
	if (auto bones = F::FakeAngle.RenderBones(pInfo.pModel))
		IVModelRender_DrawModelExecute->Call<void>(I::ModelRender, pState, pInfo, bones);
}
void CChams::RenderHandler(const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld, bool localViewmodel)
{
	if (!m_iFlags)
	{
		SkinChanger::DrawEffectGeometry(pState,pInfo,pBoneToWorld,localViewmodel,"chams_cosmetic_geometry");
		Acknowledge(pInfo);
	}
	else
	{
		if (pInfo.entity_index != I::EngineClient->GetLocalPlayer())
			RenderBacktrack(pState, pInfo);
		else
			RenderFakeAngle(pState, pInfo);
	}
}

bool CChams::RenderViewmodel(void* rcx, int flags, int* iReturn)
{
	if (!F::Groups.GroupsActive())
		return false;

	RenderState::ContextScope context(I::MaterialSystem->GetRenderContext());
	auto pRenderContext = context.Get();
	if (!pRenderContext)
		return false;

	Group_t* pGroup = nullptr;
	if (!F::Groups.GetGroup(reinterpret_cast<CBaseAnimating*>(rcx)->IsValid() ? TargetsEnum::ViewmodelHands : TargetsEnum::ViewmodelWeapon, pGroup) || !pGroup->m_tChams(true))
		return false;

	ModelStateScope renderState(I::RenderView, I::ModelRender);
	for (auto& [sName, tColor] : pGroup->m_tChams.Visible)
	{
		auto pMaterial = F::Materials.GetMaterial(FNV1A::Hash32(sName.c_str()));

		F::Materials.SetColor(pMaterial, tColor);
		I::ModelRender->ForcedMaterialOverride(pMaterial ? pMaterial->m_pMaterial : nullptr);

		bool bFlip = pMaterial && pMaterial->m_bInvertCull ? !G::FlipViewmodels : G::FlipViewmodels;
		pRenderContext->CullMode(bFlip ? MATERIAL_CULLMODE_CW : MATERIAL_CULLMODE_CCW);

		static auto CBaseAnimating_InternalDrawModel = U::Hooks.m_mHooks["CBaseAnimating_InternalDrawModel"];
		{
			// Protect the selected material only while the engine draws. Enabling
			// this guard before setup also blocks our own ForcedMaterialOverride.
			SkinRender::ViewmodelDrawScope effectScope(m_bRendering, true);
			*iReturn = CBaseAnimating_InternalDrawModel->Call<int>(rcx, flags);
		}
	}
	pRenderContext->CullMode(G::FlipViewmodels ? MATERIAL_CULLMODE_CW : MATERIAL_CULLMODE_CCW);

	return true;
}
bool CChams::RenderViewmodel(const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld, bool localViewmodel)
{
	if (!F::Groups.GroupsActive())
		return false;

	RenderState::ContextScope context(I::MaterialSystem->GetRenderContext());
	auto pRenderContext = context.Get();
	if (!pRenderContext)
		return false;

	Group_t* pGroup = nullptr;
	if (!F::Groups.GetGroup(TargetsEnum::ViewmodelWeapon, pGroup) || !pGroup->m_tChams(true))
		return false;

	ModelStateScope renderState(I::RenderView, I::ModelRender);
	for (auto& [sName, tColor] : pGroup->m_tChams.Visible)
	{
		auto pMaterial = F::Materials.GetMaterial(FNV1A::Hash32(sName.c_str()));

		F::Materials.SetColor(pMaterial, tColor);
		I::ModelRender->ForcedMaterialOverride(pMaterial ? pMaterial->m_pMaterial : nullptr);

		bool bFlip = pMaterial && pMaterial->m_bInvertCull ? !G::FlipViewmodels : G::FlipViewmodels;
		pRenderContext->CullMode(bFlip ? MATERIAL_CULLMODE_CW : MATERIAL_CULLMODE_CCW);

		{
			SkinRender::ViewmodelDrawScope effectScope(m_bRendering, true);
			SkinChanger::DrawEffectGeometry(pState,pInfo,pBoneToWorld,localViewmodel,"viewmodel_chams_cosmetic_geometry");
		}
	}
	pRenderContext->CullMode(MATERIAL_CULLMODE_CCW);

	return true;
}
