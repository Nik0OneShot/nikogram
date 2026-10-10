#include "../SDK/SDK.h"
#include "../Features/Visuals/CapturePolicy.h"
#include "../Features/Visuals/Visuals.h"
#include "../Features/Visuals/Dapper/WorldBindPolicy.h"
#include "../Features/Visuals/Dapper/WorldPhoto.h"
#include "../Features/Visuals/RenderAudit.h"

#include "../Features/Visuals/CameraWindow/CameraWindow.h"

MAKE_HOOK(CViewRender_RenderView, U::Memory.GetVirtual(I::ViewRender, 6), void,
	void* rcx, const CViewSetup& view, ClearFlags_t nClearFlags, RenderViewInfo_t whatToDraw)
{
	DEBUG_RETURN(CViewRender_RenderView, rcx, view, nClearFlags, whatToDraw);

    // Never change a material pass to screenshot bypass part way through a scene.
    CapturePolicy::SceneScope capture(SDK::CleanScreenshot());
    if (!G::Unload && !F::CameraWindow.m_bDrawing) F::Visuals.Modulate();
    if(!G::Unload&&!F::CameraWindow.m_bDrawing)
    {
        RenderAudit::WorldCache(Vars::Visuals::World::DapperPhoto.Value,DapperWorld::enabled,DapperWorld::candidates.size(),DapperWorld::brush,DapperWorld::wrapperMatches);
        RenderAudit::WorldModels(DapperWorld::props,DapperWorld::prop,DapperWorld::terrain,DapperWorld::unlit);
    }
    {
        auto context=I::MaterialSystem->GetRenderContext();
        auto target=context?context->GetRenderTarget():nullptr;
        if(context)context->Release();
        WorldBindPolicy::SceneScope world(target);
        RenderAudit::Sample("scene.before",nullptr,true);
        CALL_ORIGINAL(rcx, view, nClearFlags, whatToDraw);
        RenderAudit::Sample("scene.after");
    }
	if (SDK::CleanScreenshot() || G::Unload)
		return;

	F::CameraWindow.RenderView(rcx, view);
}
