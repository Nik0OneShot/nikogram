#include "Render.h"
#include "Workspace.h"
#include "SteamDefaultAvatar.h"

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
	if (m_pLauncherDevice != pDevice)
	{
		ReleaseLauncherTextures();
		m_pLauncherDevice = pDevice;
	}
	static std::once_flag tFlag; std::call_once(tFlag, [&]
	{
		Initialize(pDevice);
	});

	LoadColors();
	{
		static float flStaticScale = Vars::Menu::Scale.Value;
		float flOldScale = flStaticScale;
		float flNewScale = flStaticScale = Vars::Menu::Scale.Value;
		if (flNewScale != flOldScale)
			Reload();
	}

	DWORD dwOldRGB; pDevice->GetRenderState(D3DRS_SRGBWRITEENABLE, &dwOldRGB);
	pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, false);
	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	F::Menu.Render();

	ImGui::EndFrame();
	ImGui::Render();
	ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
	pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, dwOldRGB);
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
	Accent = ImColor(Workspace::Accent[0], Workspace::Accent[1], Workspace::Accent[2], 1.f);
	Background0 = ImColor(Workspace::Background[0], Workspace::Background[1], Workspace::Background[2], 1.f);
	Background0p5 = Background1 = Background1p5 = Background0;
	Background1p5L = ImColor(Workspace::Accent[0] * 0.2f, Workspace::Accent[1] * 0.2f, Workspace::Accent[2] * 0.2f, 1.f);
	Background2 = ImColor(Workspace::Accent[0] * 0.65f, Workspace::Accent[1] * 0.65f, Workspace::Accent[2] * 0.65f, 1.f);

	ImVec4* colors = GetStyle().Colors;
	colors[ImGuiCol_Border] = Background2;
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
	colors[ImGuiCol_SeparatorHovered] = Accent;
	colors[ImGuiCol_SeparatorActive] = Accent;
	colors[ImGuiCol_ScrollbarBg] = {};
	colors[ImGuiCol_Text] = Active;
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
	tFontConfig.OversampleH = 2;
#ifndef NIKOGRAM_CUSTOM_FONTS
	FontSmall = io.Fonts->AddFontFromFileTTF(R"(C:\Windows\Fonts\verdana.ttf)", H::Draw.Scale(11), &tFontConfig);
	FontRegular = io.Fonts->AddFontFromFileTTF(R"(C:\Windows\Fonts\verdana.ttf)", H::Draw.Scale(13), &tFontConfig);
	FontBold = io.Fonts->AddFontFromFileTTF(R"(C:\Windows\Fonts\verdanab.ttf)", H::Draw.Scale(13), &tFontConfig);
	FontLarge = io.Fonts->AddFontFromFileTTF(R"(C:\Windows\Fonts\verdana.ttf)", H::Draw.Scale(14), &tFontConfig);
	FontMono = io.Fonts->AddFontFromFileTTF(R"(C:\Windows\Fonts\cour.ttf)", H::Draw.Scale(16), &tFontConfig); // windows mono font installed by default
#else
	FontSmall = io.Fonts->AddFontFromMemoryCompressedTTF(RobotoMedium_compressed_data, RobotoMedium_compressed_size, H::Draw.Scale(12), &tFontConfig);
	FontRegular = io.Fonts->AddFontFromMemoryCompressedTTF(RobotoMedium_compressed_data, RobotoMedium_compressed_size, H::Draw.Scale(13), &tFontConfig);
	FontBold = io.Fonts->AddFontFromMemoryCompressedTTF(RobotoBlack_compressed_data, RobotoBlack_compressed_size, H::Draw.Scale(13), &tFontConfig);
	FontLarge = io.Fonts->AddFontFromMemoryCompressedTTF(RobotoMedium_compressed_data, RobotoMedium_compressed_size, H::Draw.Scale(15), &tFontConfig);
	FontMono = io.Fonts->AddFontFromMemoryCompressedTTF(CascadiaMono_compressed_data, CascadiaMono_compressed_size, H::Draw.Scale(15), &tFontConfig);
#endif

	ImFontConfig tIconConfig;
	tIconConfig.PixelSnapH = true;
	IconFont = io.Fonts->AddFontFromMemoryCompressedTTF(MaterialIcons_compressed_data, MaterialIcons_compressed_size, H::Draw.Scale(16), &tIconConfig);

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
	if (!m_pLauncherDevice || !pixels || !width || !height || width > 256 || height > 256) return nullptr;
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

ImTextureID CRender::NikoLauncherIcon()
{
	if (!m_pNikoIcon) m_pNikoIcon = CreateLauncherTexture(NikoIconPixels, NikoIconWidth, NikoIconHeight);
	return reinterpret_cast<ImTextureID>(m_pNikoIcon);
}

void CRender::ReleaseLauncherTextures()
{
	for (auto& texture : m_pPetTextures) { if (texture) texture->Release(); texture = nullptr; }
	if (m_pNikoIcon) { m_pNikoIcon->Release(); m_pNikoIcon = nullptr; }
	if (m_pLocalAvatar) { m_pLocalAvatar->Release(); m_pLocalAvatar = nullptr; }
	if (m_pDefaultAvatar) { m_pDefaultAvatar->Release(); m_pDefaultAvatar = nullptr; }
	m_uAvatarOwner = 0;
	m_iAvatarHandle = -1;
	m_uNextAvatarCheck = 0;
}

ImTextureID CRender::PetTexture(int frame, const unsigned int* pixels, unsigned int width, unsigned int height)
{
	if (frame < 1 || frame > 184) return 0;
	auto& texture = m_pPetTextures[frame - 1];
	if (!texture) texture = CreateLauncherTexture(pixels, width, height);
	return reinterpret_cast<ImTextureID>(texture);
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
