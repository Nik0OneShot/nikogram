#pragma once
#include <cmath>
namespace MeleePredictionPolicy
{
    inline bool Usable(bool initialized, bool failed, float x, float y, float z)
    { return initialized && !failed && std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
}
