#include "Notifications.h"
#include "NotificationStyle.h"

#include "../Easings/Easings.h"
#include "../Menu/Components.h"
#include <ImGui/imgui_internal.h>

#define EASE_IN Ease::OutCubic
#define EASE_OUT Ease::OutCubic
#define EASE_Y Ease::InOutCubic

void CNotifications::Add(const std::string& sText, const char* sIcon, Color_t tColor, float flLifeTime, float flPanTime)
{
	flLifeTime = std::isfinite(flLifeTime) ? std::max(0.f, flLifeTime) : 0.f;
	flPanTime = std::isfinite(flPanTime) ? std::max(0.f, flPanTime) : 0.f;
	float flTime = SDK::PlatFloatTime();
	std::lock_guard tLock(m_tMutex);
	const Color_t info = INFO_COLOR;
	const bool useAccent = !tColor.a || (tColor.r == info.r && tColor.g == info.g && tColor.b == info.b && tColor.a == info.a);
	m_vNotifications.emplace_back(sText, sIcon, flTime, flLifeTime, flPanTime, tColor, useAccent);
	if (m_vNotifications.size() > Vars::Logging::MaxNotifications.Value)
	{
		for (int i = 0; i < m_vNotifications.size() - Vars::Logging::MaxNotifications.Value; i++)
		{
			auto& tNotification = m_vNotifications[i];
			if (!tNotification.m_flLifeTime)
				continue;

			tNotification.m_flCreateTime = flTime - tNotification.m_flPanTime;
			tNotification.m_flLifeTime = 0.f;
		}
		//m_vNotifications.pop_front();
	}
}

void CNotifications::Add(const std::string& sText, Color_t tColor, float flLifeTime, float flPanTime)
{
	Add(sText, nullptr, tColor, flLifeTime, flPanTime);
}

static inline bool ShouldReverseX()
{
	switch (Vars::Logging::NotificationPosition.Value)
	{
	case Vars::Logging::NotificationPositionEnum::TopLeft:
	case Vars::Logging::NotificationPositionEnum::BottomLeft:
		return false;
	case Vars::Logging::NotificationPositionEnum::TopRight:
	case Vars::Logging::NotificationPositionEnum::BottomRight:
		return true;
	}
	return false;
}
static inline bool ShouldReverseY()
{
	switch (Vars::Logging::NotificationPosition.Value)
	{
	case Vars::Logging::NotificationPositionEnum::TopLeft:
	case Vars::Logging::NotificationPositionEnum::TopRight:
		return false;
	case Vars::Logging::NotificationPositionEnum::BottomLeft:
	case Vars::Logging::NotificationPositionEnum::BottomRight:
		return true;
	}
	return false;
}
static inline int X()
{
	return ShouldReverseX() ? -1 : 1;
}
static inline int Y()
{
	return ShouldReverseY() ? -1 : 1;
}

void CNotifications::Draw()
{
	using namespace ImGui;

	std::lock_guard tLock(m_tMutex);
	for (auto it = m_vNotifications.begin(); it != m_vNotifications.end();)
	{
		if (it->m_flCreateTime + it->m_flLifeTime + it->m_flPanTime * 3 < SDK::PlatFloatTime())
			it = m_vNotifications.erase(it);
		else
			++it;
	}
	if (m_vNotifications.empty())
		return;

	ImDrawList* pDrawList = GetForegroundDrawList();

	const float scale = std::max(0.1f, H::Draw.Scale());
	const float margin = H::Draw.Scale(8), padding = H::Draw.Scale(10);
	const float barHeight = H::Draw.Scale(12), textGap = H::Draw.Scale(7);
	const float maxWidth = GetIO().DisplaySize.x - margin * 2;
	if (maxWidth <= padding * 2 || GetIO().DisplaySize.y <= margin * 2) return;
	float y = ShouldReverseY() ? GetIO().DisplaySize.y - margin : margin;
	for (auto& tNotification : m_vNotifications)
	{
		// Text and countdown share the same inset. Long messages wrap rather
		// than forcing the toast beyond the screen edge. Icons are intentionally omitted.
		const float w = std::min(maxWidth, std::max(H::Draw.Scale(224), CalcTextSize(tNotification.m_sText.c_str()).x + padding * 2));
		const float wrapWidth = w - padding * 2;
		std::vector<std::string> textLines;
		const char* cursor = tNotification.m_sText.c_str();
		const char* end = cursor + tNotification.m_sText.size();
		do
		{
			const char* paragraphEnd = std::find(cursor, end, '\n');
			do
			{
				const char* lineEnd = GetFont()->CalcWordWrapPosition(GetFontSize(), cursor, paragraphEnd, wrapWidth);
				if (lineEnd == cursor && cursor < paragraphEnd)
				{
					unsigned int character;
					lineEnd += std::max(1, ImTextCharFromUtf8(&character, cursor, paragraphEnd));
				}
				textLines.emplace_back(cursor, lineEnd);
				cursor = lineEnd;
				while (cursor < paragraphEnd && (*cursor == ' ' || *cursor == '\t')) ++cursor;
			} while (cursor < paragraphEnd);
			if (cursor == end) break;
			++cursor;
		} while (cursor <= end);
		const float textHeight = GetFontSize() * textLines.size();
		const float h = padding * 2 + textHeight + textGap + barHeight;
		float x = ShouldReverseX() ? GetIO().DisplaySize.x - margin - w : margin;

		float flEaseX = 1.f, flEaseY = 1.f;
		float flTime = SDK::PlatFloatTime();
		float flCreate = tNotification.m_flCreateTime;
		float flPan = tNotification.m_flPanTime;
		float flLife = tNotification.m_flLifeTime + flPan * 2;
		if (flPan)
		{
			if (float flDelta = flTime - flCreate; flDelta < flPan)
				flEaseX = EASE_IN(Math::RemapVal(flDelta, 0.f, flPan, 0.f, 1.f));
			else if (float flDelta = flCreate + flLife - flTime; flDelta < flPan)
				flEaseX = EASE_OUT(Math::RemapVal(flDelta, 0.f, flPan, 0.f, 1.f));
			if (float flDelta = flCreate + flLife + flPan - flTime; flDelta < flPan)
				flEaseY = EASE_Y(Math::RemapVal(flDelta, 0.f, flPan, 0.f, 1.f));
		}
		const float remaining = NotificationStyle::Remaining(flTime - flCreate, tNotification.m_flLifeTime, flPan);
		flEaseX = std::clamp(flEaseX, 0.f, 1.f);
		flEaseY = std::clamp(flEaseY, 0.f, 1.f);

		x -= (w + H::Draw.Scale(8)) * (1.f - flEaseX) * X();

		const ImVec4 accent = F::Render.Accent.Value; // resolve every frame, including already-visible toasts
		auto tint = [&](float multiplier, float highlight = 0.f)
		{
			return ColorConvertFloat4ToU32(ImVec4(
				std::clamp(accent.x * multiplier + (1.f - accent.x) * highlight, 0.f, 1.f),
				std::clamp(accent.y * multiplier + (1.f - accent.y) * highlight, 0.f, 1.f),
				std::clamp(accent.z * multiplier + (1.f - accent.z) * highlight, 0.f, 1.f), 1.f));
		};
		const ImVec2 pos(x, ShouldReverseY() ? y - h : y);
		const float stroke = std::max(1.f, scale);
		pDrawList->PushClipRect(ImVec2(0, 0), GetIO().DisplaySize, true);
		pDrawList->AddRectFilled(pos, pos + ImVec2(w, h), IM_COL32(0, 0, 0, 255));
		pDrawList->AddRect(pos + ImVec2(stroke * .5f, stroke * .5f), pos + ImVec2(w - stroke * .5f, h - stroke * .5f), tint(1.f), 0.f, ImDrawFlags_None, stroke);
		float lineY = pos.y + padding;
		for (const auto& line : textLines)
		{
			const float lineWidth = CalcTextSize(line.c_str()).x;
			pDrawList->AddText(GetFont(), GetFontSize(), ImVec2(pos.x + (w - lineWidth) * .5f, lineY), tint(1.f), line.c_str());
			lineY += GetFontSize();
		}
		const ImVec2 bar = pos + ImVec2(padding, padding + textHeight + textGap);
		const auto layout = NotificationStyle::Layout(wrapWidth, scale);
		const int filled = NotificationStyle::Filled(remaining, layout.count);
		for (int i = 0; i < layout.count; ++i)
		{
			const ImVec2 lo = bar + ImVec2(layout.Left(i), 0), hi = bar + ImVec2(layout.Right(i), barHeight);
			if (i < filled)
			{
				pDrawList->AddRectFilledMultiColor(lo, hi, tint(1.f, .25f), tint(1.f, .25f), tint(.55f), tint(.55f));
				pDrawList->AddLine(lo + ImVec2(stroke * .5f, stroke * .5f), ImVec2(hi.x - stroke * .5f, lo.y + stroke * .5f), tint(1.f, .4f), stroke);
			}
			else
			{
				pDrawList->AddRectFilled(lo, hi, IM_COL32(9, 9, 9, 255));
				pDrawList->AddRect(lo, hi, IM_COL32(48, 48, 48, 255), 0.f, ImDrawFlags_None, std::min(stroke, layout.cell * .25f));
			}
		}
		pDrawList->PopClipRect();

		y += (h + H::Draw.Scale(8, Scale_Round)) * (flEaseY) * Y();
	}
}
