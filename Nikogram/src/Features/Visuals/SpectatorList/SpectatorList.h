#pragma once
#include "../../../SDK/SDK.h"
#include "SpectatorStyle.h"

class CSpectatorList
{
private:
    struct Spectator_t
    {
        std::string name, target;
        SpectatorStyle::State state;
        SpectatorStyle::View view;
        int targetIndex = 0;
        int respawn = -1;
    };
    std::vector<Spectator_t> m_vSpectators;
    void GetSpectators(CTFPlayer* local);
    void DrawMoonlit(CTFPlayer* local);
public:
    Vec2 m_vIndicatorSize = { 520, 70 };
    Vec2 m_vMinimumSize = { 200, 100 };
    int m_iPageCount = 1, m_iCurrentPage = 1, m_iEntries = 0;
    void Draw(CTFPlayer* local);
};
ADD_FEATURE(CSpectatorList, SpectatorList);
