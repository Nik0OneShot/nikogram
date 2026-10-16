#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cstring>
namespace IndicatorTextRaster
{
    struct Mask {int width=0,height=0;std::vector<uint32_t> pixels;};
    // Use the exact registered Surface family/weight. GDI runs only on the
    // Present thread; white coverage masks can be tinted without rerasterizing.
    inline Mask Build(const std::string& family,int height,int weight,const std::wstring& text)
    {
        Mask result;if(text.empty()||text.size()>2048||height<1||height>256)return result;
        HDC dc=CreateCompatibleDC(nullptr);if(!dc)return result;
        HFONT font=CreateFontA(-height,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,family.c_str());
        if(!font){DeleteDC(dc);return result;}
        auto oldFont=SelectObject(dc,font);SIZE extent{};TEXTMETRICW metrics{};
        const bool measured=GetTextExtentPoint32W(dc,text.data(),int(text.size()),&extent)&&GetTextMetricsW(dc,&metrics);
        if(measured&&extent.cx>0&&extent.cx<=4096&&metrics.tmHeight>0&&metrics.tmHeight<=512)
        {
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=extent.cx;info.bmiHeader.biHeight=-metrics.tmHeight;
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
            if(bitmap&&pixels)
            {
                auto oldBitmap=SelectObject(dc,bitmap);
                const size_t count=size_t(extent.cx)*metrics.tmHeight;std::memset(pixels,0,count*4);
                SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
                if(TextOutW(dc,0,0,text.data(),int(text.size())))
                {
                    GdiFlush();result.width=extent.cx;result.height=metrics.tmHeight;result.pixels.resize(count);
                    auto input=static_cast<const uint32_t*>(pixels);
                    for(size_t i=0;i<count;++i){const uint32_t v=input[i];const uint32_t a=std::max({v&255,(v>>8)&255,(v>>16)&255});result.pixels[i]=(a<<24)|0xFFFFFF;}
                }
                SelectObject(dc,oldBitmap);DeleteObject(bitmap);
            }
        }
        SelectObject(dc,oldFont);DeleteObject(font);DeleteDC(dc);return result;
    }
}
