#pragma once

namespace AutoDetonateCandidates
{
    // Player discovery must not depend on spatial partition membership. The
    // supplied predicate retains the normal filters and exact collision checks.
    template<class Range, class Evaluate>
    bool CheckPlayers(const Range& players, Evaluate&& evaluate)
    {
        for (auto player : players)
            if (evaluate(player)) return true;
        return false;
    }
}
