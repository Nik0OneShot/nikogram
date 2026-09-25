#include "../SDK/SDK.h"

static std::unordered_map<uint32_t, ITexture*> s_mFlatTextures = {}; // one 1x1 texture per color, kept alive to avoid creating one on every call

MAKE_HOOK(IMaterialSystem_FindTexture, U::Memory.GetVirtual(I::MaterialSystem, 79), ITexture*,
	void* rcx, char const* pTextureName, const char* pTextureGroupName, bool complain, int nAdditionalCreationFlags)
{
	DEBUG_RETURN(IMaterialSystem_FindTexture, rcx, pTextureName, pTextureGroupName, complain, nAdditionalCreationFlags);

	auto pReturn = CALL_ORIGINAL(rcx, pTextureName, pTextureGroupName, complain, nAdditionalCreationFlags);

	auto uHash = FNV1A::Hash32(Vars::Visuals::World::WorldTexture.Value.c_str());
	if (uHash == FNV1A::Hash32Const("Flat"))
	{
		auto fOverrideTexture = [&]()
		{
			if (!pReturn || pReturn->IsTranslucent() || !pTextureName || !pTextureGroupName)
				return;

			std::string_view sGroup = pTextureGroupName;
			if (!sGroup.starts_with(TEXTURE_GROUP_WORLD))
				return;

			Vec3 vColor; pReturn->GetLowResColorSample(0.5f, 0.5f, &vColor.x);
			Color_t tColor = { byte(vColor.x * 255), byte(vColor.y * 255), byte(vColor.z * 255), 255 };
			uint32_t uColor = uint32_t(tColor.r) | uint32_t(tColor.g) << 8 | uint32_t(tColor.b) << 16 | uint32_t(tColor.a) << 24;
			if (auto it = s_mFlatTextures.find(uColor); it != s_mFlatTextures.end())
			{
				pReturn = it->second;
				return;
			}

			if (auto pTexture = I::MaterialSystem->CreateTextureFromBits(1, 1, 1, IMAGE_FORMAT_RGBA8888, 4, &tColor.r))
			{
				pTexture->IncrementReferenceCount();
				s_mFlatTextures[uColor] = pReturn = pTexture;
			}
		};
		fOverrideTexture();
	}

	return pReturn;
}
