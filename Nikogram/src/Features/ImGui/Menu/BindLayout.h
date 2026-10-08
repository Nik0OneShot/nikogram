#pragma once
#include <algorithm>
#include <cmath>

namespace BindLayout
{
    inline float MoonlitEditorHeight(float dropdownHeight, float rowGap, float paddingY, float scale)
    {
        // Each of the two rows reserves the full control, its footer and spacing.
        return 2.f * (dropdownHeight + 3.f * scale + rowGap) + 2.f * paddingY;
    }
    inline float MoonlitRowHeight(float textHeight, float scale)
    {
        return std::max(38.f * scale, textHeight + 16.f * scale);
    }
    // Clamp the live window position, not the previous frame's saved drag position.
    inline float ClampAxis(float position, float extent, float minimum, float maximum)
    {
        return std::clamp(position, minimum, std::max(minimum, maximum - extent));
    }
    inline int Columns(int count, bool horizontal, float screenWidth, float stride, float margin)
    {
        if (count <= 0) return 0;
        if (!horizontal || !std::isfinite(screenWidth) || !std::isfinite(stride) || stride <= 0) return 1;
        return std::max(1, int(std::min(float(count), std::max(0.f, (screenWidth - margin) / stride))));
    }
    inline int Rows(int count, int columns)
    {
        return count > 0 && columns > 0 ? 1 + (count - 1) / columns : 0;
    }
}
