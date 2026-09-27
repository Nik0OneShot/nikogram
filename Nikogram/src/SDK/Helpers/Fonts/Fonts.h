#pragma once
#include "../../Vars.h"
#include <unordered_map>

enum EFonts
{
	FONT_ESP,
	FONT_INDICATORS,
	FONT_CRIT_LABEL,
	FONT_CRIT_COUNT,
	FONT_TICK_DETAIL
};

struct Font_t
{
	const char* m_szName;
	int m_nTall, m_nFlags, m_nWeight;
	unsigned long m_dwFont;
};

class CFonts
{
private:
	std::unordered_map<EFonts, Font_t> m_mFonts = {};

public:
	void Reload(float flDPI = Vars::Menu::Scale[DEFAULT_BIND], bool bOutline = Vars::Menu::CheapText[DEFAULT_BIND]);
	const Font_t& GetFont(EFonts eFont);
};

ADD_FEATURE_CUSTOM(CFonts, Fonts, H);
