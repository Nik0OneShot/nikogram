#include "../SDK/SDK.h"
#include "../Features/Visuals/Dapper/WorldPhoto.h"
#include "../Features/Visuals/Chams/Chams.h"
#include "../Features/Visuals/Glow/Glow.h"
#include "../Features/Visuals/CameraWindow/CameraWindow.h"
#include "../Features/Visuals/RenderAudit.h"

MAKE_SIGNATURE(CStaticPropMgr_DrawStaticProps, "engine.dll", "4C 8B DC 49 89 5B ? 49 89 6B ? 49 89 73 ? 57 41 54 41 55 41 56 41 57 48 83 EC ? 4C 8B 3D", 0x0);

static thread_local bool s_bDrawingProps = false;

MAKE_HOOK(CStaticPropMgr_DrawStaticProps, S::CStaticPropMgr_DrawStaticProps(), void,
	void* rcx, IClientRenderable** pProps, int count, bool bShadowDepth, bool drawVCollideWireframe)
{
	DEBUG_RETURN(CStaticPropMgr_DrawStaticProps, rcx, pProps, count, bShadowDepth, drawVCollideWireframe);

	struct DrawingScope
	{
		bool saved=s_bDrawingProps;
		DrawingScope(){s_bDrawingProps=true;}
		~DrawingScope(){s_bDrawingProps=saved;}
	} drawing;
	PhotoRenderPolicy::PropScope<IVModelRender,IMaterial,OverrideType_t> photo(I::ModelRender,
		DapperWorld::PropMaterial(bShadowDepth||drawVCollideWireframe||F::Chams.m_bRendering
			||F::Glow.m_bRendering||F::CameraWindow.m_bDrawing||SDK::CleanScreenshot()),OVERRIDE_NORMAL);
	if(photo.Active())RenderAudit::WorldPropDraw(false);
	CALL_ORIGINAL(rcx, pProps, count, bShadowDepth, drawVCollideWireframe);
}

MAKE_HOOK(CStudioRender_SetColorModulation, U::Memory.GetVirtual(I::StudioRender, 27), void,
	void* rcx, const float* pColor)
{
	DEBUG_RETURN(CStudioRender_SetColorModulation, rcx, pColor);
	if(PhotoRenderPolicy::propDepth){const float white[3]{1,1,1};return CALL_ORIGINAL(rcx,white);}

	if (!s_bDrawingProps || !(Vars::Visuals::World::Modulations.Value & Vars::Visuals::World::ModulationsEnum::Prop) || SDK::CleanScreenshot())
		return CALL_ORIGINAL(rcx, pColor);

	float flColor[3] = {
		Vars::Colors::PropModulation.Value.r / 255.f,
		Vars::Colors::PropModulation.Value.g / 255.f,
		Vars::Colors::PropModulation.Value.b / 255.f
	};
	CALL_ORIGINAL(rcx, flColor);
}

MAKE_HOOK(CStudioRender_SetAlphaModulation, U::Memory.GetVirtual(I::StudioRender, 28), void,
	void* rcx, float flAlpha)
{
	DEBUG_RETURN(CStudioRender_SetAlphaModulation, rcx, flAlpha);
	if(PhotoRenderPolicy::propDepth)return CALL_ORIGINAL(rcx,1.f);

	if (!s_bDrawingProps || !(Vars::Visuals::World::Modulations.Value & Vars::Visuals::World::ModulationsEnum::Prop) || SDK::CleanScreenshot())
		return CALL_ORIGINAL(rcx, flAlpha);

	CALL_ORIGINAL(rcx, Vars::Colors::PropModulation.Value.a / 255.f * flAlpha);
}
