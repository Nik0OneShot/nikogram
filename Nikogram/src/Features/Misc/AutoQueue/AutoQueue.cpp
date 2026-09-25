#include "AutoQueue.h"

void CAutoQueue::Run()
{
	if (Vars::Misc::Queueing::AutoCasualQueue.Value && !I::TFPartyClient->BInQueueForMatchGroup(k_eTFMatchGroup_Casual_Default))
	{
		static Timer tTimer = {};
		if (!tTimer.Run(5.f))
			return;

		if (!I::TFPartyClient->AnySelected())
			I::TFPartyClient->LoadSavedCasualCriteria();
		I::TFPartyClient->RequestQueueForMatch(k_eTFMatchGroup_Casual_Default);
	}
}