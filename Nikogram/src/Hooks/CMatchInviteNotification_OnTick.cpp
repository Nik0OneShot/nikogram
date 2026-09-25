#include "../SDK/SDK.h"

MAKE_SIGNATURE(CMatchInviteNotification_OnTick, "client.dll", "40 53 48 83 EC ? 48 8B D9 E8 ? ? ? ? F7 83", 0x0);

#define JOIN_TIME (10.0)
#define FULL_TIME (180.0 - JOIN_TIME)

static std::unordered_map<void*, double> s_mLastAutoJoinTime = {};

MAKE_HOOK(CMatchInviteNotification_OnTick, S::CMatchInviteNotification_OnTick(), void,
	void* rcx)
{
	DEBUG_RETURN(CMatchInviteNotification_OnTick, rcx);

	if (Vars::Misc::Queueing::ExtendQueue.Value)
	{
		auto dFloatTime = SDK::PlatFloatTime();
		auto& dAutoJoinTime = *reinterpret_cast<double*>(uintptr_t(rcx) + 616);
		auto& dLastAutoJoinTime = s_mLastAutoJoinTime[rcx];
		double dExtend = dAutoJoinTime != dLastAutoJoinTime ? FULL_TIME : 0.0;
		dLastAutoJoinTime = dAutoJoinTime = std::max(dAutoJoinTime + dExtend, dFloatTime + 1.0);

		for (auto it = s_mLastAutoJoinTime.begin(); it != s_mLastAutoJoinTime.end();)
		{
			if (dFloatTime > it->second)
				it = s_mLastAutoJoinTime.erase(it);
			else
				++it;
		}
	}

	CALL_ORIGINAL(rcx);
}