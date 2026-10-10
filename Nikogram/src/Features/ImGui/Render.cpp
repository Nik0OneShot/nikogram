#include "Render.h"
#include "Workspace.h"
#include "Fonts/OneShotFont.h"
#include "SteamDefaultAvatar.h"
#include "NikogramLogo.h"
#include "MoonlitHud.h"
#include "DeviceStatePolicy.h"
#include "../Visuals/Dapper/PhotoAssetPolicy.h"
#include <mutex>

#include "../../Hooks/Direct3DDevice9.h"
#include <ImGui/imgui_impl_win32.h>
#include "Fonts/MaterialDesign/MaterialIcons.h"
#include "Fonts/MaterialDesign/IconDefinitions.h"
#include "Fonts/CascadiaMono/CascadiaMono.h"
#include "Fonts/Roboto/RobotoMedium.h"
#include "Fonts/Roboto/RobotoBlack.h"
#include "Menu/Menu.h"
#include "Menu/Components.h"

void CRender::Render(IDirect3DDevice9* pDevice)
{
	if (!pDevice || pDevice->TestCooperativeLevel() != D3D_OK) return;
    if (SDK::CleanScreenshot()) { MoonlitHud::InvalidateBadge(); return; }
	if (m_pLauncherDevice != pDevice)
	{
		ReleaseLauncherTextures();
		m_pLauncherDevice = pDevice;
	}
	static std::once_flag tFlag; std::call_once(tFlag, [&]
	{
		Initialize(pDevice);
	});

    DeviceStatePolicy::Scope<IDirect3DDevice9, IDirect3DStateBlock9, D3DSTATEBLOCKTYPE> state(pDevice, D3DSBT_ALL);
    if (!state.Ready()) return; // Leave engine state untouched if restoration is unavailable.
	LoadColors();
	{
		static float flStaticScale = Vars::Menu::Scale.Value;
		float flOldScale = flStaticScale;
		float flNewScale = flStaticScale = Vars::Menu::Scale.Value;
		if (flNewScale != flOldScale)
			Reload();
	}

	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	MoonlitHud::DrawBadges();
	F::Menu.Render();

	ImGui::EndFrame();
	ImGui::Render();
	ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

void CRender::LoadColors()
{
	using namespace ImGui;

	Accent = ColorByteToFloat(Vars::Menu::Theme::Accent.Value);
	Background0 = ColorByteToFloat(Vars::Menu::Theme::Background.Value);
	Background0p5 = ColorByteToFloat(Vars::Menu::Theme::Background.Value.Lerp({ 127, 127, 127 }, 0.5f / 9, LerpEnum::NoAlpha));
	Background1 = ColorByteToFloat(Vars::Menu::Theme::Background.Value.Lerp({ 127, 127, 127 }, 1.f / 9, LerpEnum::NoAlpha));
	Background1p5 = ColorByteToFloat(Vars::Menu::Theme::Background.Value.Lerp({ 127, 127, 127 }, 1.5f / 9, LerpEnum::NoAlpha));
	Background1p5L = { Background1p5.Value.x * 1.1f, Background1p5.Value.y * 1.1f, Background1p5.Value.z * 1.1f, Background1p5.Value.w };
	Background2 = ColorByteToFloat(Vars::Menu::Theme::Background.Value.Lerp({ 127, 127, 127 }, 2.f / 9, LerpEnum::NoAlpha));
	Inactive = ColorByteToFloat(Vars::Menu::Theme::Inactive.Value);
	Active = ColorByteToFloat(Vars::Menu::Theme::Active.Value);
	Active = ImColor(Workspace::TextChannel(0), Workspace::TextChannel(1), Workspace::TextChannel(2), 1.f);
	Inactive = ImColor(Workspace::InactiveTextChannel(0), Workspace::InactiveTextChannel(1), Workspace::InactiveTextChannel(2), 1.f);
	// Legacy HUD callers read these effective colours; config defaults live separately.
	Vars::Menu::Theme::Active.Value = Color_t(int(Workspace::TextChannel(0)*255),int(Workspace::TextChannel(1)*255),int(Workspace::TextChannel(2)*255),255);
	Vars::Menu::Theme::Inactive.Value = Color_t(int(Workspace::InactiveTextChannel(0)*255),int(Workspace::InactiveTextChannel(1)*255),int(Workspace::InactiveTextChannel(2)*255),255);
	Accent = ImColor(Workspace::Accent[0], Workspace::Accent[1], Workspace::Accent[2], 1.f);
	Background0 = ImColor(Workspace::Background[0], Workspace::Background[1], Workspace::Background[2], 1.f);
	Background0p5 = Background1 = Background1p5 = Background0;
	Background1p5L = ImColor(Workspace::Accent[0] * 0.2f, Workspace::Accent[1] * 0.2f, Workspace::Accent[2] * 0.2f, 1.f);
	Background2 = ImColor(Workspace::Accent[0] * 0.65f, Workspace::Accent[1] * 0.65f, Workspace::Accent[2] * 0.65f, 1.f);

	ImVec4* colors = GetStyle().Colors;
	const ImVec4 border(Workspace::BorderChannel(0), Workspace::BorderChannel(1), Workspace::BorderChannel(2), 1.f);
	colors[ImGuiCol_Border] = border;
	colors[ImGuiCol_Separator] = border;
	colors[ImGuiCol_TableBorderLight] = border;
	colors[ImGuiCol_TableBorderStrong] = border;
	colors[ImGuiCol_Button] = {};
	colors[ImGuiCol_ButtonHovered] = {};
	colors[ImGuiCol_ButtonActive] = {};
	colors[ImGuiCol_FrameBg] = Background1p5;
	colors[ImGuiCol_FrameBgHovered] = Background1p5L;
	colors[ImGuiCol_FrameBgActive] = Background1p5;
	colors[ImGuiCol_Header] = {};
	colors[ImGuiCol_HeaderHovered] = { Background1p5L.Value.x * 1.1f, Background1p5L.Value.y * 1.1f, Background1p5L.Value.z * 1.1f, Background1p5.Value.w }; // divd by 1.1
	colors[ImGuiCol_HeaderActive] = Background1p5;
	colors[ImGuiCol_ModalWindowDimBg] = { Background0.Value.x, Background0.Value.y, Background0.Value.z, 0.4f };
	colors[ImGuiCol_PopupBg] = Background1p5L;
	colors[ImGuiCol_ResizeGrip] = {};
	colors[ImGuiCol_ResizeGripActive] = {};
	colors[ImGuiCol_ResizeGripHovered] = {};
	// ImGui uses separator colours for hovered/held window resize borders.
	colors[ImGuiCol_SeparatorHovered] = border;
	colors[ImGuiCol_SeparatorActive] = border;
	colors[ImGuiCol_ScrollbarBg] = {};
	colors[ImGuiCol_Text] = Active;
	colors[ImGuiCol_TextDisabled] = Inactive;
	colors[ImGuiCol_WindowBg] = Background0;
	colors[ImGuiCol_TitleBg] = Background0;
	colors[ImGuiCol_TitleBgActive] = Background0;
	colors[ImGuiCol_TitleBgCollapsed] = Background0;
	colors[ImGuiCol_CheckMark] = Accent;
	colors[ImGuiCol_SliderGrab] = Accent;
	colors[ImGuiCol_SliderGrabActive] = Active;
	colors[ImGuiCol_ScrollbarGrab] = Background2;
	colors[ImGuiCol_ScrollbarGrabHovered] = Accent;
	colors[ImGuiCol_ScrollbarGrabActive] = Accent;
	colors[ImGuiCol_ButtonHovered] = Background1p5L;
	colors[ImGuiCol_ButtonActive] = Background2;
	GetIO().FontGlobalScale = Workspace::FontScale;
}

void CRender::LoadFonts()
{
	using namespace ImGui;

	auto& io = GetIO();

	if (static bool bLoaded = false; !bLoaded)
		bLoaded = true;
	else
		io.Fonts->Clear();

	ImFontConfig tFontConfig;
	tFontConfig.OversampleH = 3;
	tFontConfig.OversampleV = 2;
	tFontConfig.PixelSnapH = false;
	auto loadTerminus = [&](float size)
	{
		ImFontConfig config = tFontConfig;
		config.FontDataOwnedByAtlas = false;
		ImFont* font = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(OneShotFont::Data), int(sizeof(OneShotFont::Data)), H::Draw.Scale(size), &config);
		if (!font)
		{
			ImFontConfig fallback = tFontConfig;
			fallback.SizePixels = H::Draw.Scale(size);
			font = io.Fonts->AddFontDefault(&fallback);
		}
		return font;
	};
	FontSmall = loadTerminus(14);
	FontRegular = loadTerminus(14);
	FontBold = loadTerminus(14);
	FontLarge = loadTerminus(16);
	FontMono = loadTerminus(16);
	FontMoonlit = io.Fonts->AddFontFromMemoryCompressedTTF(RobotoMedium_compressed_data, RobotoMedium_compressed_size, H::Draw.Scale(16), &tFontConfig);
	FontMoonlitHeading = io.Fonts->AddFontFromMemoryCompressedTTF(RobotoMedium_compressed_data, RobotoMedium_compressed_size, H::Draw.Scale(25), &tFontConfig);
    wchar_t windowsPath[MAX_PATH]{};GetWindowsDirectoryW(windowsPath,MAX_PATH);
    for(int i=0;i<3;++i)
    {
        const auto path=(std::filesystem::path(windowsPath)/"Fonts"/(i==0?"tahoma.ttf":"georgia.ttf")).string();
        std::error_code error;
        const bool exists=std::filesystem::is_regular_file(path,error);
        FontDapper[i]=exists?io.Fonts->AddFontFromFileTTF(path.c_str(),H::Draw.Scale(i==0?15:16),&tFontConfig):nullptr;
        FontDapperHeading[i]=exists?io.Fonts->AddFontFromFileTTF(path.c_str(),H::Draw.Scale(i==0?22:25),&tFontConfig):nullptr;
        if(!FontDapper[i])FontDapper[i]=FontMoonlit;
        if(!FontDapperHeading[i])FontDapperHeading[i]=FontMoonlitHeading;
    }

	ImFontConfig tIconConfig;
	tIconConfig.PixelSnapH = true;
	IconFont = io.Fonts->AddFontFromMemoryCompressedTTF(MaterialIcons_compressed_data, MaterialIcons_compressed_size, H::Draw.Scale(16), &tIconConfig);
	Workspace::TextIconFont = IconFont;

	io.Fonts->Build();
	io.ConfigDebugHighlightIdConflicts = false;
}

void CRender::LoadStyle()
{
	using namespace ImGui;

	auto& style = GetStyle();
	style.ButtonTextAlign = { 0.5f, 0.5f };
	style.CellPadding = { H::Draw.Scale(4), 0 };
	style.ChildBorderSize = 0.f;
	style.ChildRounding = 0.f;
	style.FrameBorderSize = 1.f;
	style.FramePadding = { 0, 0 };
	style.FrameRounding = 0.f;
	style.ItemInnerSpacing = { 0, 0 };
	style.ItemSpacing = { H::Draw.Scale(8), H::Draw.Scale(8) };
	style.PopupBorderSize = 1.f;
	style.PopupRounding = 0.f;
	style.ScrollbarSize = 6.f + H::Draw.Scale(3);
	style.ScrollbarRounding = 0.f;
	style.WindowBorderSize = 1.f;
	style.WindowPadding = { 0, 0 };
	style.WindowRounding = 0.f;
	style.GrabRounding = 0.f;
	style.TabRounding = 0.f;
}

void CRender::Initialize(IDirect3DDevice9* pDevice)
{
	ImGui::CreateContext();
	ImGui_ImplWin32_Init(WndProc::hwWindow);
	ImGui_ImplDX9_Init(pDevice);

	auto& io = ImGui::GetIO();
	Workspace::Load();
	io.IniFilename = Workspace::LayoutPath.c_str();
	io.ConfigWindowsMoveFromTitleBarOnly = true;
	io.LogFilename = nullptr;

	LoadFonts();
	LoadStyle();

	m_bLoaded = true;
	m_bInitialized = true;
}

void CRender::Unload()
{
	ReleaseLauncherTextures();
	MoonlitHud::ReleaseBadge();
	m_pLauncherDevice = nullptr;
	// release the D3D9 objects and device reference ImGui holds, so load/unload cycles don't leak them
	if (!m_bInitialized)
		return;

	m_bLoaded = m_bInitialized = false;
	ImGui_ImplDX9_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

IDirect3DTexture9* CRender::CreateLauncherTexture(const unsigned int* pixels, unsigned int width, unsigned int height)
{
	if (!m_pLauncherDevice || !pixels || !width || !height || width > 1024 || height > 1024) return nullptr;
	IDirect3DTexture9* texture = nullptr;
	if (FAILED(m_pLauncherDevice->CreateTexture(width, height, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
		D3DPOOL_DEFAULT, &texture, nullptr))) return nullptr;
	D3DLOCKED_RECT locked = {};
	if (FAILED(texture->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD)))
	{
		texture->Release();
		return nullptr;
	}
	for (unsigned int y = 0; y < height; ++y)
		memcpy(static_cast<unsigned char*>(locked.pBits) + y * locked.Pitch, pixels + y * width, width * sizeof(unsigned int));
	texture->UnlockRect(0);
	return texture;
}

#include "NikoIcon.h"

namespace MoonlitHud
{
	namespace
	{
		struct BadgeDraw { int x, y, size, left, top, right, bottom, photo; float opacity; };
		struct BadgeBatch { std::array<BadgeDraw, 256> draws{}; size_t count = 0; double time = 0.; };
		BadgeBatch building, ready;
		std::mutex badgeMutex;
	}
	void BeginBadges()
	{
        Configure();
		std::lock_guard lock(badgeMutex);
		building.count = 0; // Keep the completed snapshot visible during the next Paint.
	}
	void EndBadges()
	{
		std::lock_guard lock(badgeMutex);
		building.time = SDK::PlatFloatTime();
		ready = building;
		building.count = 0;
	}
	void InvalidateBadge()
	{
		std::lock_guard lock(badgeMutex);
		building.count = ready.count = 0;
	}
	void ReleaseBadge() { InvalidateBadge(); }
	bool Badge(int x, int y, int size, bool charged, bool watched)
	{
		if (!Enabled() || (!MenuMode::Dapper(MenuMode::Hud()) && !Workspace::PetEnabled) || size <= 0) return false;
		int left, top, right, bottom; bool clippingDisabled;
		I::MatSystemSurface->GetClippingRect(left, top, right, bottom, clippingDisabled);
		if (clippingDisabled) { left = top = 0; right = H::Draw.m_nScreenW; bottom = H::Draw.m_nScreenH; }
		if (x >= right || y >= bottom || x + size <= left || y + size <= top) return false;
		std::lock_guard lock(badgeMutex);
		if (building.count == building.draws.size()) return false;
        const int style=MenuMode::Hud();
		building.draws[building.count++] = {x,y,size,left,top,right,bottom,
            MenuMode::Dapper(style)?DapperStyle::Photo(style,charged,watched):0,MenuMode::Dapper(style)?DapperStyle::Opacity(style):1.f};
		return true;
	}
	void DrawBadges()
	{
		if (!Enabled() || (!MenuMode::Dapper(MenuMode::Hud()) && !Workspace::PetEnabled) || SDK::CleanScreenshot()
			|| !I::EngineClient->IsInGame() || !H::Entities.GetLocal())
		{
			InvalidateBadge();
			return;
		}
		BadgeBatch batch;
		{
			std::lock_guard lock(badgeMutex);
			batch = ready;
			// Present can run more often than Paint. Reading must not consume the snapshot.
		}
		if (!batch.count) return;
		const double age = SDK::PlatFloatTime() - batch.time;
		if (age < 0. || age > .1) return;
		// Use the same alpha-capable DX9 image as Binds. The background list keeps
		// badges below menu windows; clipping remains the original HUD panel's.
		auto* draw = ImGui::GetBackgroundDrawList();
		for (size_t i = 0; i < batch.count; ++i)
		{
			const auto& b = batch.draws[i];
            const auto texture=b.photo?F::Render.DapperPhoto(b.photo):F::Render.NikoLauncherIcon();
            if(!texture)continue;
			draw->PushClipRect({float(b.left),float(b.top)}, {float(b.right),float(b.bottom)}, true);
			draw->AddImage(texture, {float(b.x),float(b.y)}, {float(b.x+b.size),float(b.y+b.size)}, {0,0},{1,1},IM_COL32(255,255,255,int(255*b.opacity)));
			draw->PopClipRect();
		}
	}
}

ImTextureID CRender::NikoLauncherIcon()
{
	if (!m_pNikoIcon) m_pNikoIcon = CreateLauncherTexture(NikoIconPixels, NikoIconWidth, NikoIconHeight);
	return reinterpret_cast<ImTextureID>(m_pNikoIcon);
}

void CRender::ReleaseLauncherTextures()
{
	MoonlitHud::InvalidateBadge();
	if (m_pNikogramLogo) { m_pNikogramLogo->Release(); m_pNikogramLogo = nullptr; }
    for(auto& texture:m_pDapperPhotos){if(texture)texture->Release();texture=nullptr;}
	for (auto& texture : m_pPetTextures) { if (texture) texture->Release(); texture = nullptr; }
	if (m_pNikoIcon) { m_pNikoIcon->Release(); m_pNikoIcon = nullptr; }
	if (m_pLocalAvatar) { m_pLocalAvatar->Release(); m_pLocalAvatar = nullptr; }
	if (m_pDefaultAvatar) { m_pDefaultAvatar->Release(); m_pDefaultAvatar = nullptr; }
	m_uAvatarOwner = 0;
	m_iAvatarHandle = -1;
	m_uNextAvatarCheck = 0;
}

extern "C" IMAGE_DOS_HEADER __ImageBase;
ImTextureID CRender::DapperPhoto(int selection)
{
    const int index=PhotoAssetPolicy::Index(selection);if(index<0)return 0;
    auto& texture=m_pDapperPhotos[index];
    if(!texture)
    {
        const auto module=reinterpret_cast<HMODULE>(&__ImageBase);
        const auto resource=FindResourceW(module,MAKEINTRESOURCEW(PhotoAssetPolicy::assets[index].resource),MAKEINTRESOURCEW(10));
        if(!resource||SizeofResource(module,resource)!=512*512*4)return 0;
        const auto memory=LoadResource(module,resource);
        const auto rgba=memory?static_cast<const unsigned char*>(LockResource(memory)):nullptr;
        if(!rgba)return 0;
        // Convert the existing resource's RGBA bytes to DX9 ARGB explicitly.
        std::vector<unsigned int> argb(512*512);
        for(size_t i=0;i<argb.size();++i)argb[i]=(unsigned(rgba[i*4+3])<<24)|(unsigned(rgba[i*4])<<16)|(unsigned(rgba[i*4+1])<<8)|unsigned(rgba[i*4+2]);
        texture=CreateLauncherTexture(argb.data(),512,512);
    }
    return reinterpret_cast<ImTextureID>(texture);
}

ImTextureID CRender::PetTexture(int frame, const unsigned int* pixels, unsigned int width, unsigned int height)
{
	if (frame < 1 || frame > 184) return 0;
	auto& texture = m_pPetTextures[frame - 1];
	if (!texture) texture = CreateLauncherTexture(pixels, width, height);
	return reinterpret_cast<ImTextureID>(texture);
}

ImTextureID CRender::NikogramLogo()
{
    if (!m_pNikogramLogo) m_pNikogramLogo = CreateLauncherTexture(NikogramLogoPixels, NikogramLogoWidth, NikogramLogoHeight);
    return reinterpret_cast<ImTextureID>(m_pNikogramLogo);
}

ImTextureID CRender::LauncherAvatar(bool privacy)
{
	if (!m_pDefaultAvatar) m_pDefaultAvatar = CreateLauncherTexture(SteamDefaultAvatar, 64, 64);
	const auto fallback = reinterpret_cast<ImTextureID>(m_pDefaultAvatar);
	// This branch must precede Steam identity/avatar calls and cached-avatar returns.
	if (privacy)
	{
		if (m_pLocalAvatar) { m_pLocalAvatar->Release(); m_pLocalAvatar = nullptr; }
		m_uAvatarOwner = 0; m_iAvatarHandle = -1; m_uNextAvatarCheck = 0;
		return fallback;
	}
	if (!I::SteamUser || !I::SteamFriends || !I::SteamUtils) return fallback;
	const auto steamID = I::SteamUser->GetSteamID();
	const auto owner = steamID.ConvertToUint64();
	if (owner != m_uAvatarOwner)
	{
		if (m_pLocalAvatar) { m_pLocalAvatar->Release(); m_pLocalAvatar = nullptr; }
		m_uAvatarOwner = owner; m_iAvatarHandle = -1; m_uNextAvatarCheck = 0;
	}
	const auto now = GetTickCount64();
	if (now >= m_uNextAvatarCheck)
	{
		m_uNextAvatarCheck = now + 2000;
		const int handle = I::SteamFriends->GetMediumFriendAvatar(steamID);
		if (handle != m_iAvatarHandle || !m_pLocalAvatar)
		{
			if (m_pLocalAvatar) { m_pLocalAvatar->Release(); m_pLocalAvatar = nullptr; }
			m_iAvatarHandle = handle;
			uint32 width = 0, height = 0;
			if (handle > 0 && I::SteamUtils->GetImageSize(handle, &width, &height) &&
				width && height && width <= 256 && height <= 256)
			{
				std::vector<unsigned char> rgba(width * height * 4);
				if (I::SteamUtils->GetImageRGBA(handle, rgba.data(), static_cast<int>(rgba.size())))
				{
					std::vector<unsigned int> argb(width * height);
					for (size_t i = 0; i < argb.size(); ++i)
						argb[i] = (unsigned(rgba[i * 4 + 3]) << 24) | (unsigned(rgba[i * 4]) << 16) |
							(unsigned(rgba[i * 4 + 1]) << 8) | unsigned(rgba[i * 4 + 2]);
					m_pLocalAvatar = CreateLauncherTexture(argb.data(), width, height);
				}
			}
		}
	}
	return m_pLocalAvatar ? reinterpret_cast<ImTextureID>(m_pLocalAvatar) : fallback;
}

void CRender::Reload()
{
	m_bLoaded = false;

	LoadFonts();
	LoadStyle();

	m_bLoaded = true;
}
