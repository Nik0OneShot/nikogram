#include "../SDK/SDK.h"

#include "../Features/Visuals/Visuals.h"
#include "../Features/Aimbot/ProjectileGuideCamera.h"
#include "../Features/Visuals/CameraWindow/CameraWindow.h"

MAKE_HOOK(CClientModeShared_OverrideView, U::Memory.GetVirtual(I::ClientModeShared, 16), void,
	void* rcx, CViewSetup* pView)
{
	DEBUG_RETURN(CClientModeShared_OverrideView, rcx, pView);

	CALL_ORIGINAL(rcx, pView);

	if (auto pLocal = H::Entities.GetLocal(); pLocal && pView)
	{
		F::Visuals.FOV(pLocal, pView);
		F::Visuals.ApplyFreelook(pView);
		F::Visuals.ThirdPerson(pLocal, pView);
	}
	F::Visuals.ApplyOptifineZoom(H::Entities.GetLocal(), pView);
    if(!F::CameraWindow.m_bDrawing)ProjectileGuideCamera::Capture(H::Entities.GetLocal(),pView);
}
