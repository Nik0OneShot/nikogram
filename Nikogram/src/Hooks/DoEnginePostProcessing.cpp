#include "../SDK/SDK.h"
#include "../Features/Visuals/CapturePolicy.h"
#include "../Features/Visuals/RenderAudit.h"

MAKE_SIGNATURE(DoEnginePostProcessing, "client.dll", "48 8B C4 44 89 48 ? 44 89 40 ? 89 50 ? 89 48", 0x0);

MAKE_HOOK(DoEnginePostProcessing, S::DoEnginePostProcessing(), void,
	int x, int y, int w, int h, bool bFlashlightIsOn, bool bPostVGui)
{
	DEBUG_RETURN(DoEnginePostProcessing, x, y, w, h, bFlashlightIsOn, bPostVGui);

	// Skipping the whole function skips HDR exposure/histogram upkeep, not
	// just cosmetic effects. Maintain the native pipeline on ALL frames while
	// clean capture is enabled, never only during the screenshot itself.
	if (CapturePolicy::NativePostProcessing(Vars::Visuals::UI::CleanScreenshots.Value,Vars::Visuals::Removals::PostProcessing.Value,G::Unload))
    {
        RenderAudit::Sample(bPostVGui?"post.vgui.before":"post.world.before");
		CALL_ORIGINAL(x, y, w, h, bFlashlightIsOn, bPostVGui);
        RenderAudit::Sample(bPostVGui?"post.vgui.after":"post.world.after");
    }
}
