#pragma once
#include "../../SDK/SDK.h"
#include <ImGui/imgui_impl_dx9.h>
#include <ImGui/imgui.h>

class CRender
{
public:
	void Render(IDirect3DDevice9* pDevice);
	void Initialize(IDirect3DDevice9* pDevice);
	void Reload();
	void Unload();

	void LoadColors();
	void LoadFonts();
	void LoadStyle();
	ImTextureID LauncherAvatar(bool privacy);
	ImTextureID NikoLauncherIcon();
	ImTextureID PetTexture(int frame, const unsigned int* pixels, unsigned int width, unsigned int height);
	void ReleaseLauncherTextures();

private:
	IDirect3DDevice9* m_pLauncherDevice = nullptr; // borrowed; owned by the game
	IDirect3DTexture9* m_pDefaultAvatar = nullptr;
	IDirect3DTexture9* m_pNikoIcon = nullptr;
	IDirect3DTexture9* m_pPetTextures[184] = {};
	IDirect3DTexture9* m_pLocalAvatar = nullptr;
	uint64_t m_uAvatarOwner = 0;
	int m_iAvatarHandle = -1;
	ULONGLONG m_uNextAvatarCheck = 0;
	IDirect3DTexture9* CreateLauncherTexture(const unsigned int* pixels, unsigned int width, unsigned int height);

public:

	int Cursor = 2;

	// Colors
	ImColor Accent = {};
	ImColor Background0 = {};
	ImColor Background0p5 = {};
	ImColor Background1 = {};
	ImColor Background1p5 = {};
	ImColor Background1p5L = {};
	ImColor Background2 = {};
	ImColor Inactive = {};
	ImColor Active = {};

	// Fonts
	ImFont* FontSmall = nullptr;
	ImFont* FontRegular = nullptr;
	ImFont* FontBold = nullptr;
	ImFont* FontLarge = nullptr;
	ImFont* FontMono = nullptr;

	ImFont* IconFont = nullptr;

	bool m_bLoaded = false;
	bool m_bInitialized = false;
};

ADD_FEATURE(CRender, Render);
