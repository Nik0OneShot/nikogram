#pragma once
#include <windows.h>
#include <d3d9.h>
#include <type_traits>

namespace Direct3DContract
{
    // Check the complete argument list against the Windows SDK's actual COM
    // method, including the receiver. Missing the HWND must not compile.
    using Present=HRESULT(__fastcall*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
    static_assert(std::is_invocable_r_v<HRESULT,decltype(&IDirect3DDevice9::Present),
        IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*>);
    static_assert(!std::is_invocable_v<decltype(&IDirect3DDevice9::Present),
        IDirect3DDevice9*,const RECT*,const RECT*,const RGNDATA*>);
    inline HRESULT ForwardPresent(Present original,IDirect3DDevice9* device,
        const RECT* source,const RECT* destination,HWND window,const RGNDATA* dirty)
    {return original(device,source,destination,window,dirty);}
}

namespace WndProc
{
	inline HWND hwWindow;
	inline WNDPROC Original;
	LONG __stdcall Func(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	void Initialize();
	void Unload();
}
