#pragma once
#include <Windows.h>
#include "TerminusData.h"
namespace OneShotFont
{
    // Process-private registration, shared by Surface fonts; no system font install.
    inline const char* SurfaceFamily()
    {
        struct Registration
        {
            HANDLE handle = nullptr;
            Registration() { DWORD count = 0; handle = AddFontMemResourceEx(const_cast<unsigned char*>(Data), DWORD(sizeof(Data)), nullptr, &count); }
            ~Registration() { if (handle) RemoveFontMemResourceEx(handle); }
        };
        static Registration registration;
        return registration.handle ? "Terminus (TTF)" : "Courier New";
    }
}
