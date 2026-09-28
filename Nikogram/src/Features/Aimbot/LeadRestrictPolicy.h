#pragma once
#include <cmath>
namespace LeadRestrictPolicy
{
    // Exceptions are limited to close targets, nearby impact geometry, and a
    // small angle outside the configured cone. Strict solutions always win.
    inline const char* Exception(float targetFov,float shotFov,float limit,float distance,float surfaceDistance,bool retained)
    {
        if(!std::isfinite(targetFov)||!std::isfinite(shotFov)||!std::isfinite(limit)||!std::isfinite(distance)||!std::isfinite(surfaceDistance)) return "invalid_geometry";
        if(targetFov<0 || shotFov<0 || distance<0 || surfaceDistance<0 || limit<=0 || !(targetFov<limit)) return "target_outside_fov";
        if(distance>(retained?128.f:112.f)) return "target_too_far";
        if(surfaceDistance>64.f) return "impact_too_far_from_target";
        if(shotFov>60.f || shotFov>limit+(retained?12.f:10.f)) return "exception_angle_too_large";
        return "close_range_exception";
    }
}
