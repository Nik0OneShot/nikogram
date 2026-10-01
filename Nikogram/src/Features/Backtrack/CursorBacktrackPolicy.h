#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace CursorBacktrackPolicy
{
    using Point = std::array<double, 3>;
    using Matrix = std::array<std::array<double, 4>, 3>;

    inline bool ManualShot(bool enabled, bool rawAttack, bool finalAttack, bool firing,
        bool hitscan, bool assisted, bool usingAction, bool switching)
    {
        return enabled && rawAttack && finalAttack && firing && hitscan
            && !assisted && !usingAction && !switching;
    }

    // Invert the complete affine bone transform, including model scale. The
    // ray parameter remains a world distance for a normalized world direction.
    inline bool HitDistance(const Point& eye, const Point& direction, const Point& mins,
        const Point& maxs, const Matrix& bone, double range, double& distance)
    {
        if (!std::isfinite(range) || range <= 0) return false;
        double length = 0;
        for (int i = 0; i < 3; ++i)
        {
            if (!std::isfinite(eye[i]) || !std::isfinite(direction[i])
                || !std::isfinite(mins[i]) || !std::isfinite(maxs[i]) || mins[i] > maxs[i]) return false;
            for (double value : bone[i]) if (!std::isfinite(value)) return false;
            length += direction[i] * direction[i];
        }
        if (std::abs(length - 1.) > 1e-4) return false;
        const double a=bone[0][0], b=bone[0][1], c=bone[0][2];
        const double d=bone[1][0], e=bone[1][1], f=bone[1][2];
        const double g=bone[2][0], h=bone[2][1], i=bone[2][2];
        const double det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
        if (!std::isfinite(det) || std::abs(det) < 1e-12) return false;
        const std::array<Point, 3> inverse = {{
            { (e*i-f*h)/det, (c*h-b*i)/det, (b*f-c*e)/det },
            { (f*g-d*i)/det, (a*i-c*g)/det, (c*d-a*f)/det },
            { (d*h-e*g)/det, (b*g-a*h)/det, (a*e-b*d)/det }
        }};
        Point origin{}, ray{};
        for (int row=0; row<3; ++row)
            for (int col=0; col<3; ++col)
            {
                origin[row] += inverse[row][col] * (eye[col]-bone[col][3]);
                ray[row] += inverse[row][col] * direction[col];
            }
        double enter=0, leave=range;
        for (int axis=0; axis<3; ++axis)
        {
            if (!std::isfinite(origin[axis]) || !std::isfinite(ray[axis])) return false;
            if (std::abs(ray[axis]) < 1e-12)
            {
                if (origin[axis] < mins[axis] || origin[axis] > maxs[axis]) return false;
                continue;
            }
            double entry=(mins[axis]-origin[axis])/ray[axis], exitDistance=(maxs[axis]-origin[axis])/ray[axis];
            if (entry > exitDistance) std::swap(entry,exitDistance);
            enter=std::max(enter,entry); leave=std::min(leave,exitDistance);
            if (enter > leave) return false;
        }
        if (!std::isfinite(enter) || enter <= 0 || enter > range) return false;
        distance=enter;
        return true;
    }
}
