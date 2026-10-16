#pragma once
#include "../../../SDK/SDK.h"

Enum(Model, Visible, Occluded);

class CChams
{
private:

	void DrawModel(CBaseEntity* pEntity, const Chams_t& tChams, IMatRenderContext* pRenderContext, int iModel = ModelEnum::Visible, bool bTwoModel = false);

	void RenderBacktrack(const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo);
	void RenderFakeAngle(const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo);

	struct ChamsInfo_t
	{
		CBaseEntity* m_pEntity;
		Chams_t m_tChams;
		int m_iFlags = 0;
		int m_iIndex, m_iServer;
		uint32_t m_uHandle;
		const model_t* m_pModel;
		void* m_pRenderable;
		ChamsInfo_t(CBaseEntity* entity, const Chams_t* chams, int flags = 0)
			: m_pEntity(entity), m_tChams(*chams), m_iFlags(flags), m_iIndex(entity->entindex()),
			m_iServer(I::ClientState->m_nServerCount), m_uHandle(uint32_t(entity->GetRefEHandle().ToInt())), m_pModel(entity->GetModel()), m_pRenderable(entity->GetClientRenderable()) {}
	};
	std::vector<ChamsInfo_t> m_vEntities = {};
	struct Replacement_t { ChamsInfo_t info; int frame; bool hidden; };
	std::unordered_map<int, Replacement_t> m_mReplacements;
	struct Attempt_t { CBaseEntity* entity; int index; uint32_t handle; const model_t* model; void* renderable; int server; bool forwarded = false; };
	Attempt_t* m_pAttempt = nullptr;
	bool ValidEntity(const ChamsInfo_t& info) const;
	bool Eligible(CBaseEntity* entity, const Chams_t& chams) const;
	void Commit(CBaseEntity* entity, const Chams_t& chams, const Attempt_t* attempt = nullptr);
	void Acknowledge(const ModelRenderInfo_t& info);


	int m_iFlags = false;

public:
	void Store(CTFPlayer* pLocal);
	void RenderMain();
	bool ShouldSuppress(const ModelRenderInfo_t& info);
	void Reset();
	void RenderHandler(const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld, bool localViewmodel=false);

	bool RenderViewmodel(void* rcx, int flags, int* iReturn);
	bool RenderViewmodel(const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld, bool localViewmodel=false);

	bool m_bRendering = false;

};

ADD_FEATURE(CChams, Chams);
