#define NOMINMAX
#include <cassert>
#include <cstring>
#include <iostream>
#include "../../Nikogram/src/Features/ImGui/Fonts/OneShotFont.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../Nikogram/include/ImGui/imstb_truetype.h"
#pragma comment(lib, "gdi32.lib")
int main()
{
    assert(sizeof(OneShotFont::Data)==308732);
    stbtt_fontinfo info{};
    assert(stbtt_InitFont(&info,OneShotFont::Data,0));
    for(int c=32;c<127;++c) assert(stbtt_FindGlyphIndex(&info,c)!=0);
    assert(std::strcmp(OneShotFont::SurfaceFamily(),"Terminus (TTF)")==0);
    HDC dc=CreateCompatibleDC(nullptr); assert(dc);
    HFONT font=CreateFontA(-14,0,0,0,700,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,OneShotFont::SurfaceFamily());
    assert(font);
    HGDIOBJ old=SelectObject(dc,font);
    char actual[128]{}; assert(GetTextFaceA(dc,128,actual)>0);
    assert(std::strcmp(actual,"Terminus (TTF)")==0);
    SelectObject(dc,old); DeleteObject(font); DeleteDC(dc);
    std::cout<<"OneShotFont: embedded size, TrueType parsing, 95 ASCII glyphs, private registration and actual GDI font selection passed\n";
}
