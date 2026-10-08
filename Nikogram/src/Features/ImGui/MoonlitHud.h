#pragma once
#include "../../SDK/SDK.h"
#include "MenuMode.h"
#include "Workspace.h"

// Layout is drawn by Surface. Badge calls queue bounded image positions only;
// the Present hook draws their shared texture, never ImGui from engine Paint.
namespace MoonlitHud
{
    inline bool Enabled() { return MenuMode::Active == MenuMode::Moonlit; }
    inline const Color_t Panel{33,27,48,245}, Raised{43,36,60,255}, Border{72,59,92,255};
    inline const Color_t Ink{241,234,250,255}, Muted{187,175,202,255};
    inline const Color_t Lavender{192,163,243,255}, Gold{242,204,127,255}, Green{168,221,193,255};
    inline int S(float value) { return std::max(1, int(H::Draw.Scale(value, Scale_Round))); }
    bool Badge(int x, int y, int size);
    void BeginBadges();
    void EndBadges();
    void DrawBadges();
    void InvalidateBadge();
    void ReleaseBadge();
    inline int BadgeSpace() { return Workspace::PetEnabled ? S(24) : 0; }
    inline const Font_t& Label() { return H::Fonts.GetFont(FONT_MOONLIT_LABEL); }
    inline const Font_t& Detail() { return H::Fonts.GetFont(FONT_MOONLIT_DETAIL); }
    inline const Font_t& Count() { return H::Fonts.GetFont(FONT_MOONLIT_COUNT); }
    inline int Measure(const std::string& text, const Font_t& font) { return int(std::ceil(H::Draw.GetTextSize(text.c_str(), font).x)); }
    inline std::string Fit(std::string text, const Font_t& font, int width)
    {
        for (char& c : text) if (static_cast<unsigned char>(c) < 32) c = ' ';
        if (Measure(text, font) <= width) return text;
        if (Measure("...", font) > width) return {};
        while (!text.empty() && Measure(text + "...", font) > width)
        {
            size_t end = text.size() - 1;
            while (end && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
            text.resize(end);
        }
        return text + "...";
    }
    inline void Text(int x, int y, const std::string& value, Color_t colour, const Font_t& font, int width, EAlign align = ALIGN_TOPLEFT)
    {
        Workspace::ScopedTextColour preserve;
        const auto text = Fit(value, font, width);
        H::Draw.String(font, x, y, colour, align, text.c_str());
    }
    struct Box { int x, y, w, h; };
    inline Box Bounds(int cx, int y, int w, int h)
    {
        w = std::clamp(w, 1, std::max(1, H::Draw.m_nScreenW));
        h = std::clamp(h, 1, std::max(1, H::Draw.m_nScreenH));
        return { std::clamp(cx - w / 2, 0, std::max(0, H::Draw.m_nScreenW - w)),
            std::clamp(y, 0, std::max(0, H::Draw.m_nScreenH - h)), w, h };
    }
    inline void Frame(const Box& box)
    {
        const int radius = std::min({S(12), box.w / 2, box.h / 2});
        H::Draw.FillRoundRect(box.x, box.y, box.w, box.h, radius, Panel);
        H::Draw.LineRoundRect(box.x, box.y, box.w, box.h, radius, Border);
    }
    inline void Bar(int x, int y, int width, int height, float ratio, Color_t colour)
    {
        H::Draw.FillRoundRect(x, y, width, height, height / 2, Raised);
        const int filled = int(width * (std::isfinite(ratio) ? std::clamp(ratio, 0.f, 1.f) : 0.f));
        if (filled > 0) H::Draw.FillRoundRect(x, y, filled, height, std::min(filled, height) / 2, colour);
    }
    using Rows = std::vector<std::pair<std::string, std::string>>;
    inline void Row(const Box& b, int y, const std::string& label, const std::string& value, Color_t colour = Ink)
    {
        const int pad = S(14), inner = std::max(0, b.w - pad * 2);
        const int right = std::min(Measure(value, Detail()), inner * 3 / 5);
        Text(b.x + pad, y, label, Muted, Detail(), std::max(0, inner - right - S(10)));
        Text(b.x + b.w - pad, y, value, colour, Detail(), right, ALIGN_TOPRIGHT);
    }
    inline Vec2 Meter(int cx, int top, const std::string& title, const std::string& status,
        const std::string& count, const std::string& unit, float ratio, Color_t accent,
        const Rows& rows)
    {
        const int pad = S(14), gap = S(10), bar = S(5);
        int width = std::max(S(232), Measure(title, Label()) + Measure(status, Detail()) + pad * 2 + gap + BadgeSpace());
        width = std::max(width, Measure(count, Count()) + Measure(unit, Detail()) + pad * 2 + gap);
        for (const auto& [a,b] : rows) width = std::max(width, Measure(a, Detail()) + Measure(b, Detail()) + pad * 2 + gap);
        const int height = pad * 2 + Label().m_nTall + gap + Count().m_nTall + gap + bar + int(rows.size()) * (gap + Detail().m_nTall);
        auto box = Bounds(cx, top, width, height); Frame(box);
        H::Draw.StartClipping(box.x, box.y, box.w, box.h);
        int y = box.y + pad;
        const int statusW = std::min(Measure(status, Detail()), box.w / 2);
        Badge(box.x + pad, y - S(3), S(20));
        Text(box.x + pad + BadgeSpace(), y, title, Ink, Label(), box.w - pad * 2 - statusW - gap - BadgeSpace());
        Text(box.x + box.w - pad, y + S(1), status, accent, Detail(), statusW, ALIGN_TOPRIGHT);
        y += Label().m_nTall + gap;
        const int unitW = Measure(unit, Detail());
        Text(box.x + pad, y, count, Ink, Count(), box.w - pad * 2 - unitW - gap);
        Text(box.x + box.w - pad, y + Count().m_nTall - Detail().m_nTall, unit, Muted, Detail(), unitW, ALIGN_TOPRIGHT);
        y += Count().m_nTall + gap;
        Bar(box.x + pad, y, std::max(1, box.w - pad * 2), bar, ratio, accent);
        y += bar + gap;
        for (const auto& [a,b] : rows) { Row(box, y, a, b); y += gap + Detail().m_nTall; }
        H::Draw.EndClipping();
        return {float(box.w), float(box.h)};
    }
    inline Vec2 Info(int cx, int top, const std::string& title, const std::string& status, const Rows& rows, Color_t accent = Lavender)
    {
        const int pad = S(14), gap = S(8);
        int width = std::max(S(224), Measure(title, Label()) + Measure(status, Detail()) + pad * 2 + S(18) + BadgeSpace());
        for (const auto& [a,b] : rows) width = std::max(width, Measure(a, Detail()) + Measure(b, Detail()) + pad * 2 + S(18));
        auto box = Bounds(cx, top, width, pad * 2 + Label().m_nTall + int(rows.size()) * (gap + Detail().m_nTall)); Frame(box);
        H::Draw.StartClipping(box.x, box.y, box.w, box.h);
        const int statusW = std::min(Measure(status, Detail()), box.w / 2);
        Badge(box.x + pad, box.y + pad - S(3), S(20));
        Text(box.x + pad + BadgeSpace(), box.y + pad, title, Ink, Label(), box.w - pad * 2 - statusW - gap - BadgeSpace());
        Text(box.x + box.w - pad, box.y + pad + S(1), status, accent, Detail(), statusW, ALIGN_TOPRIGHT);
        int y = box.y + pad + Label().m_nTall + gap;
        for (const auto& [a,b] : rows) { Row(box, y, a, b); y += gap + Detail().m_nTall; }
        H::Draw.EndClipping();
        return {float(box.w),float(box.h)};
    }
    inline void Pill(int x, int y, const std::string& value, Color_t colour, const Font_t& font, bool badge = false)
    {
        const int iconSpace = badge ? BadgeSpace() : 0;
        const int pad = S(6), width = std::min(Measure(value, font) + pad * 2 + iconSpace, H::Draw.m_nScreenW);
        const int height = font.m_nTall + pad * 2;
        auto box = Bounds(x, y, width, height);
        H::Draw.FillRoundRect(box.x, box.y, box.w, box.h, std::min(S(8), height / 2), Panel);
        if (iconSpace) Badge(box.x + pad, box.y + (height - S(18)) / 2, S(18));
        Text(box.x + pad + iconSpace, box.y + pad, value, colour, font, box.w - pad * 2 - iconSpace);
    }
    inline void RadarCaption(int x, int y, const std::string& label, const std::string& value, Color_t colour)
    {
        const int pad = S(7), gap = S(10), iconSpace = BadgeSpace();
        const int labelW = Measure(label, Detail()), valueW = Measure(value, Label());
        auto box = Bounds(x, y, pad * 2 + iconSpace + labelW + gap + valueW, Label().m_nTall + pad * 2);
        Frame(box);
        if (iconSpace) Badge(box.x + pad, box.y + (box.h - S(18)) / 2, S(18));
        Text(box.x + pad + iconSpace, box.y + pad + S(1), label, Muted, Detail(), labelW);
        Text(box.x + box.w - pad, box.y + pad, value, colour, Label(), valueW, ALIGN_TOPRIGHT);
    }
}
