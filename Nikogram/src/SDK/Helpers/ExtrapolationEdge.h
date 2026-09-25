#pragma once
#include <algorithm>
#include <cmath>
#include <span>

namespace ExtrapolationEdge
{
	struct Point { float x = 0.f, y = 0.f; };
	struct Layout { Point start, end, normal; bool valid = false; };
	inline bool Finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y); }

	// Offset the complete indicator outside the projected model envelope.
	// Preserve projected displacement even when the real endpoint is inside it.
	inline Layout Place(Point anchor, Point displacement, std::span<const Point> outline)
	{
		if (!Finite(anchor) || !Finite(displacement) || outline.empty()) return {};
		const float length = std::hypot(displacement.x, displacement.y);
		if (!std::isfinite(length)) return {};
		const Point direction = length > 0.0001f ? Point{ displacement.x / length, displacement.y / length } : Point{ 1.f, 0.f };
		float extent = 0.f;
		for (Point point : outline)
		{
			if (!Finite(point)) return {};
			extent = std::max(extent, (point.x - anchor.x) * direction.x + (point.y - anchor.y) * direction.y);
		}
		const Point start{ anchor.x + direction.x * (extent + 5.f), anchor.y + direction.y * (extent + 5.f) };
		const Point end{ start.x + displacement.x, start.y + displacement.y };
		if (!Finite(start) || !Finite(end)) return {};
		return { start, end, { -direction.y, direction.x }, true };
	}
}
