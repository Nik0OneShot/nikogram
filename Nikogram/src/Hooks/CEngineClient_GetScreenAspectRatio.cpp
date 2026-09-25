#include "../SDK/SDK.h"

MAKE_HOOK(CEngineClient_GetScreenAspectRatio, U::Memory.GetVirtual(I::EngineClient, 95), float,
	void* rcx)
{
	DEBUG_RETURN(CEngineClient_GetScreenAspectRatio, rcx);

	if (Vars::Visuals::UI::AspectRatio.Value)
		return Vars::Visuals::UI::AspectRatio.Value;

	return CALL_ORIGINAL(rcx);
}