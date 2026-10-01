#pragma once
#include <algorithm>
#include <cmath>

namespace AimFOVVisualPolicy
{
    struct Boundary { double radius=0; bool visible=false,fullViewport=false; };
    inline Boundary Project(double aim,double camera,int width,int height)
    {
        if (!std::isfinite(aim) || !std::isfinite(camera) || aim<=0 || camera<=0 || camera>=180 || width<=0 || height<=0) return {};
        constexpr double radians=3.14159265358979323846/180.;
        const double corner=std::hypot(width*.5,height*.5);
        // A >=90-degree targeting cone has no forward perspective boundary.
        // Show a viewport outline rather than inventing a smaller circle.
        if (aim>=90) return {corner,true,true};
        // TF2's base camera FOV is expressed for a 4:3 viewport. Preserve that
        // projection, but remove the old 48%-of-screen-height visual cap.
        const double radius=std::tan(aim*radians)/std::tan(camera*radians*.5)*height*(2./3.);
        if (!std::isfinite(radius) || radius<=0) return {};
        return {radius,true,radius>=corner};
    }
}
