#pragma once
#include "../../../SDK/SDK.h"

class CPlayerConditions
{
public:
	Vec2 m_vIndicatorSize = {100, 40};
	std::vector<std::string> Get(CTFPlayer* pEntity);
	void Draw(CTFPlayer* pLocal);
};

ADD_FEATURE(CPlayerConditions, PlayerConditions);
