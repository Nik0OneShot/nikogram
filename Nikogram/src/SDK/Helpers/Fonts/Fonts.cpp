#include "Fonts.h"

#include "../../Definitions/Interfaces/IMatSystemSurface.h"
#include <ranges>
#include "../../../Features/ImGui/Fonts/OneShotFont.h"

void CFonts::Reload(float flDPI, bool bOutline)
{
	int iFlags = !bOutline ? FONTFLAG_ANTIALIAS : FONTFLAG_ANTIALIAS | FONTFLAG_DROPSHADOW;
	const char* family = OneShotFont::SurfaceFamily();

	// keep existing handles, the surface never frees them so creating new ones on every reload leaks
	m_mFonts[FONT_ESP] = { family, int(14.f * flDPI), iFlags, 700, m_mFonts[FONT_ESP].m_dwFont };
	m_mFonts[FONT_INDICATORS] = { family, int(14.f * flDPI), iFlags, 700, m_mFonts[FONT_INDICATORS].m_dwFont };
	// Dedicated clean HUD typography, independent of the outlined/cheap-text option.
	m_mFonts[FONT_TICK_DETAIL] = { family, (std::max)(1, int(12.f * flDPI)), FONTFLAG_ANTIALIAS, 700, m_mFonts[FONT_TICK_DETAIL].m_dwFont };
	m_mFonts[FONT_CRIT_LABEL] = { family, int(14.f * flDPI), FONTFLAG_ANTIALIAS, 700, m_mFonts[FONT_CRIT_LABEL].m_dwFont };
	m_mFonts[FONT_CRIT_COUNT] = { family, int(16.f * flDPI), FONTFLAG_ANTIALIAS, 700, m_mFonts[FONT_CRIT_COUNT].m_dwFont };

	for (auto& fFont : m_mFonts | std::views::values)
	{
		if (!fFont.m_dwFont)
			fFont.m_dwFont = I::MatSystemSurface->CreateFont();
		if (fFont.m_dwFont)
			I::MatSystemSurface->SetFontGlyphSet(fFont.m_dwFont, fFont.m_szName, fFont.m_nTall, fFont.m_nWeight, 0, 0, fFont.m_nFlags);
	}
	// A bounded size pool keeps radar text scalable without leaking surface font handles.
	for(size_t n=0;n<m_aRadarFonts.size();++n)
	{
		auto& font=m_aRadarFonts[n];
		font={family,int(n)+6,FONTFLAG_ANTIALIAS,700,font.m_dwFont};
		if(!font.m_dwFont)font.m_dwFont=I::MatSystemSurface->CreateFont();
		if(font.m_dwFont)I::MatSystemSurface->SetFontGlyphSet(font.m_dwFont,font.m_szName,font.m_nTall,font.m_nWeight,0,0,font.m_nFlags);
	}
}

const Font_t& CFonts::GetFont(EFonts eFont)
{
	return m_mFonts[eFont];
}

const Font_t& CFonts::GetRadarFont(int height)
{
	return m_aRadarFonts[std::clamp(height,6,48)-6];
}
