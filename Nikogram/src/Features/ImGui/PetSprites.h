#pragma once
#include <vector>
#include <Windows.h>
#include <wincodec.h>
#include <wincrypt.h>
#include <wrl/client.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "crypt32.lib")
namespace NikoPet
{
    struct Asset { const char* base64; unsigned width,height,left,top,right,bottom; };
#include "PetAssets0.h"
#include "PetAssets1.h"
#include "PetAssets2.h"
#include "PetAssets3.h"
    inline constexpr Asset Assets[] = {
        Frame1,Frame2,Frame3,Frame4,Frame5,Frame6,Frame7,Frame8,Frame9,Frame10,Frame11,Frame12,
        Frame13,Frame14,Frame15,Frame16,Frame17,Frame18,Frame19,Frame20,Frame21,Frame22,Frame23,Frame24,
        Frame25,Frame26,Frame27,Frame28,Frame29,Frame30,Frame31,Frame32,Frame33,Frame34,Frame35,Frame36,
        Frame37,Frame38,Frame39,Frame40,Frame41,Frame42,Frame43,Frame44,Frame45,Frame46 };
    namespace Alula
    {
#include "AlulaAssets0.h"
#include "AlulaAssets1.h"
#include "AlulaAssets2.h"
#include "AlulaAssets3.h"
        inline constexpr Asset Frames[]={Frame1,Frame2,Frame3,Frame4,Frame5,Frame6,Frame7,Frame8,Frame9,Frame10,Frame11,Frame12,Frame13,Frame14,Frame15,Frame16,Frame17,Frame18,Frame19,Frame20,Frame21,Frame22,Frame23,Frame24,Frame25,Frame26,Frame27,Frame28,Frame29,Frame30,Frame31,Frame32,Frame33,Frame34,Frame35,Frame36,Frame37,Frame38,Frame39,Frame40,Frame41,Frame42,Frame43,Frame44,Frame45,Frame46};
    }
    inline const Asset& AlulaAsset(int frame) { return Alula::Frames[frame-1]; }
    namespace Calamus
    {
#include "CalamusAssets0.h"
#include "CalamusAssets1.h"
#include "CalamusAssets2.h"
#include "CalamusAssets3.h"
        inline constexpr Asset Frames[]={Frame1,Frame2,Frame3,Frame4,Frame5,Frame6,Frame7,Frame8,Frame9,Frame10,Frame11,Frame12,Frame13,Frame14,Frame15,Frame16,Frame17,Frame18,Frame19,Frame20,Frame21,Frame22,Frame23,Frame24,Frame25,Frame26,Frame27,Frame28,Frame29,Frame30,Frame31,Frame32,Frame33,Frame34,Frame35,Frame36,Frame37,Frame38,Frame39,Frame40,Frame41,Frame42,Frame43,Frame44,Frame45,Frame46};
    }
    inline const Asset& CalamusAsset(int frame) { return Calamus::Frames[frame-1]; }
    namespace WorldMachine
    {
#include "WorldMachineAssets0.h"
#include "WorldMachineAssets1.h"
#include "WorldMachineAssets2.h"
#include "WorldMachineAssets3.h"
        inline constexpr Asset Frames[]={Frame1,Frame2,Frame3,Frame4,Frame5,Frame6,Frame7,Frame8,Frame9,Frame10,Frame11,Frame12,Frame13,Frame14,Frame15,Frame16,Frame17,Frame18,Frame19,Frame20,Frame21,Frame22,Frame23,Frame24,Frame25,Frame26,Frame27,Frame28,Frame29,Frame30,Frame31,Frame32,Frame33,Frame34,Frame35,Frame36,Frame37,Frame38,Frame39,Frame40,Frame41,Frame42,Frame43,Frame44,Frame45,Frame46};
    }
    inline const Asset& GetAsset(int frame, int character=0)
    { return character==3?WorldMachine::Frames[frame-1]:character==1?AlulaAsset(frame):character==2?CalamusAsset(frame):Assets[frame-1]; }
    // Per-character source-canvas contact coordinates; sprite art differs.
    inline float SideGrip(int character) { return character==1?48.f:character==2?46.f:52.f; }
    inline float HandLine(int character) { return character==1?51.f:character==2?45.f:48.f; }
    inline float FootAnchorX(int character) { return character==1?60.f:63.f; }
    // Frames 12-14 share a left-facing grip at canvas x=52. Anchor that
    // contact point, not the opaque bounding-box edge (hat/tail included).
    // The window intentionally occludes the part extending past the grip.
    inline float ClimbSpriteLeft(int frame, float size, float edge, bool mirrored, int character=0)
    {
        const auto& asset=GetAsset(frame,character);
        const float grip=SideGrip(character);
        const float offset=mirrored ? float(asset.right)-grip : grip-float(asset.left);
        return edge-offset*(size/128.f);
    }
    // Raised hanging contact line, lowered 14 canvas pixels after in-game review.
    // The bar hides the head/hat above this line instead of touching the hat.
    inline float HangSpriteTop(int frame, float size, float border, int character=0)
    {
        return border-(HandLine(character)-float(GetAsset(frame,character).top))*(size/128.f);
    }
    struct Decoded { std::vector<unsigned int> pixels; bool attempted=false; };
    inline Decoded DecodedFrames[184];

    inline const std::vector<unsigned int>& Pixels(int frame, int character=0)
    {
        auto& decoded=DecodedFrames[character*46+frame-1]; const auto& asset=GetAsset(frame,character);
        if(decoded.attempted) return decoded.pixels;
        decoded.attempted=true;
        DWORD count=0;
        if(!CryptStringToBinaryA(asset.base64,0,CRYPT_STRING_BASE64,nullptr,&count,nullptr,nullptr)) return decoded.pixels;
        std::vector<BYTE> bytes(count);
        if(!CryptStringToBinaryA(asset.base64,0,CRYPT_STRING_BASE64,bytes.data(),&count,nullptr,nullptr)) return decoded.pixels;
        const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        {
            using Microsoft::WRL::ComPtr;
            ComPtr<IWICImagingFactory> factory; ComPtr<IWICStream> stream;
            ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> bitmap; ComPtr<IWICFormatConverter> converter;
            if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))
                && SUCCEEDED(factory->CreateStream(&stream))
                && SUCCEEDED(stream->InitializeFromMemory(bytes.data(),count))
                && SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder))
                && SUCCEEDED(decoder->GetFrame(0,&bitmap)) && SUCCEEDED(factory->CreateFormatConverter(&converter))
                && SUCCEEDED(converter->Initialize(bitmap.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))
            {
                UINT width=0,height=0; converter->GetSize(&width,&height);
                if(width==asset.width && height==asset.height)
                {
                    decoded.pixels.resize(width*height);
                    if(FAILED(converter->CopyPixels(nullptr,width*4,width*height*4,reinterpret_cast<BYTE*>(decoded.pixels.data())))) decoded.pixels.clear();
                }
            }
        }
        if(SUCCEEDED(init)) CoUninitialize();
        return decoded.pixels;
    }
}
