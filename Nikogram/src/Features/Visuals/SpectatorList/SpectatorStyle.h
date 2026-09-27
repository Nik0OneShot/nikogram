#pragma once

namespace SpectatorStyle
{
    constexpr bool WatchingLocal(int targetIndex, int localIndex)
    {
        return localIndex > 0 && targetIndex == localIndex;
    }
}
#include <algorithm>
#include <array>
#include <vector>
#include <utility>
namespace SpectatorStyle
{
    enum State { Dead = 1, Freeze = 2, Spectating = 4, Presumed = 8 };
    enum View { First = 1, Third = 2, Free = 4, Fixed = 8, DeathCamera = 16, Unknown = 32 };
    struct Classification { State state; View view; bool targeted; };
    inline Classification Classify(int mode, bool known)
    {
        if (!known) return { Presumed, Unknown, false };
        switch (mode)
        {
        case 1: return { Dead, DeathCamera, true };
        case 2: return { Freeze, DeathCamera, true };
        case 3: return { Spectating, Fixed, false };
        case 4: return { Spectating, First, true };
        case 5: return { Spectating, Third, true };
        case 6: return { Spectating, Free, false };
        case 0: return { Dead, Unknown, false };
        default: return { Presumed, Unknown, false };
        }
    }
    inline bool Include(State state, View view, int states, int views) { return (states & state) && (views & view); }
    inline const char* Label(State state)
    {
        switch (state) { case Dead: return "DEAD"; case Freeze: return "FREEZETIME"; case Spectating: return "SPECTATING"; default: return "PRESUMED"; }
    }
    inline const char* Label(View view)
    {
        switch (view) { case First: return "FIRST PERSON"; case Third: return "THIRD PERSON"; case Free: return "FREE CAMERA"; case Fixed: return "FIXED CAMERA"; case DeathCamera: return "DEATH / FREEZE"; default: return "UNKNOWN"; }
    }
    inline int Columns(int available, int card, int gap) { return std::max(1, (available + gap) / std::max(1, card + gap)); }
    struct HorizontalSize { int width, columns; };
    inline std::pair<float, float> ResizeAxis(float start, float size, float delta, bool farEdge, float minimum, float low, float high)
    {
        const float anchor = std::clamp(farEdge ? start : start + size, low, high);
        const float maximum = farEdge ? high - anchor : anchor - low;
        const float resized = std::clamp(size + (farEdge ? delta : -delta), std::min(minimum, maximum), maximum);
        return { farEdge ? anchor : anchor - resized, resized };
    }
    inline HorizontalSize SizeHorizontal(int available, int card, int gap, int entries, int minimum)
    {
        const int columns = std::min(std::max(1, entries), Columns(available, card, gap));
        return { std::min(available, std::max(minimum, columns * card + (columns - 1) * gap)), columns };
    }
    inline std::array<int, 4> FitColumns(std::array<int, 4> widths, int available)
    {
        int total = 0;
        for (int& width : widths) { width = std::max(0, width); total += width; }
        if (total > available && total > 0)
            for (int& width : widths) width = int(double(width) * std::max(0, available) / total);
        return widths;
    }
    struct Placement { int index, x, y; bool header; };
    inline std::vector<std::vector<Placement>> Paginate(const std::vector<int>& targets, bool grouped,
        int columns, int card, int gap, int pad, int line, int rowHeight, int startY, int maxHeight, bool footer = true)
    {
        std::vector<std::vector<Placement>> pages(1);
        int y = startY, column = 0, lastTarget = -1;
        for (int i = 0; i < int(targets.size()); ++i)
        {
            bool header = grouped && targets[i] != lastTarget;
            if (header && column) { y += rowHeight + gap; column = 0; }
            if (y + (header ? line : 0) + rowHeight + (footer ? line : 0) + pad > maxHeight)
            {
                pages.emplace_back(); y = startY; column = 0; header = grouped;
            }
            if (header) { pages.back().push_back({ i, pad, y, true }); y += line; }
            pages.back().push_back({ i, pad + column * (card + gap), y, false });
            lastTarget = targets[i];
            if (++column >= columns) { column = 0; y += rowHeight + gap; }
        }
        return pages;
    }
}
