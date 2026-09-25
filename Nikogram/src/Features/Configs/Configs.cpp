#include "Configs.h"
#include "BundledDefault.h"
#include "ConfigColours.h"

#include "../Binds/Binds.h"
#include "../Visuals/Groups/Groups.h"
#include "../Visuals/Materials/Materials.h"

template <class T> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const T& v)
{
	t.put(s, v);
}

template <> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const IntRange_t& v)
{
	boost::property_tree::ptree tChild;
	SaveJson(tChild, "Min", v.Min);
	SaveJson(tChild, "Max", v.Max);

	t.put_child(s, tChild);
}

template <> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const FloatRange_t& v)
{
	boost::property_tree::ptree tChild;
	SaveJson(tChild, "Min", v.Min);
	SaveJson(tChild, "Max", v.Max);

	t.put_child(s, tChild);
}

template <> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const Color_t& v)
{
	boost::property_tree::ptree tChild;
	SaveJson(tChild, "r", v.r);
	SaveJson(tChild, "g", v.g);
	SaveJson(tChild, "b", v.b);
	SaveJson(tChild, "a", v.a);

	t.put_child(s, tChild);
}

template <> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const std::vector<std::pair<std::string, Color_t>>& v)
{
	boost::property_tree::ptree tChild;
	for (auto& [m, c] : v)
	{
		boost::property_tree::ptree tLayer;
		SaveJson(tLayer, "Material", m);
		SaveJson(tLayer, "Color", c);

		tChild.push_back({ "", tLayer });
	}

	t.put_child(s, tChild);
}

template <> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const Gradient_t& v)
{
	boost::property_tree::ptree tChild;
	SaveJson(tChild, "StartColor", v.StartColor);
	SaveJson(tChild, "EndColor", v.EndColor);

	t.put_child(s, tChild);
}

template <> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const DragBox_t& v)
{
	boost::property_tree::ptree tChild;
	SaveJson(tChild, "x", v.x);
	SaveJson(tChild, "y", v.y);

	t.put_child(s, tChild);
}

template <> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const WindowBox_t& v)
{
	boost::property_tree::ptree tChild;
	SaveJson(tChild, "x", v.x);
	SaveJson(tChild, "y", v.y);
	SaveJson(tChild, "w", v.w);
	SaveJson(tChild, "h", v.h);

	t.put_child(s, tChild);
}

template <> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const Chams_t& v)
{
	boost::property_tree::ptree tChild;
	SaveJson(tChild, "Visible", v.Visible);
	SaveJson(tChild, "Occluded", v.Occluded);

	t.put_child(s, tChild);
}

template <> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const Glow_t& v)
{
	boost::property_tree::ptree tChild;
	SaveJson(tChild, "Stencil", v.Stencil);
	SaveJson(tChild, "Blur", v.Blur);

	t.put_child(s, tChild);
}



template <class T> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, T& v)
{
	if (auto o = t.get_optional<T>(s))
		v = *o;
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, IntRange_t& v)
{
	if (auto tChild = t.get_child_optional(s))
	{
		LoadJson(*tChild, "Min", v.Min);
		LoadJson(*tChild, "Max", v.Max);
	}
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, FloatRange_t& v)
{
	if (auto tChild = t.get_child_optional(s))
	{
		LoadJson(*tChild, "Min", v.Min);
		LoadJson(*tChild, "Max", v.Max);
	}
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, Color_t& v)
{
	if (auto tChild = t.get_child_optional(s))
	{
		LoadJson(*tChild, "r", v.r);
		LoadJson(*tChild, "g", v.g);
		LoadJson(*tChild, "b", v.b);
		LoadJson(*tChild, "a", v.a);
	}
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, std::vector<std::pair<std::string, Color_t>>& v)
{
	if (auto tChild = t.get_child_optional(s))
	{
		v.clear();
		for (auto& tLayer : *tChild | std::views::values)
		{
			if (auto o = tLayer.get_optional<std::string>("Material"))
			{
				std::string& m = *o;
				Color_t c; LoadJson(tLayer, "Color", c);
				v.emplace_back(m, c);
			}
		}
	}

	// remove invalid/duplicate materials
	for (auto it = v.begin(); it != v.end();)
	{
		auto uHash = FNV1A::Hash32(it->first.c_str());
		bool bValid = uHash != FNV1A::Hash32Const("None") && (uHash == FNV1A::Hash32Const("Original") || F::Materials.m_mMaterials.contains(uHash));
		if (bValid)
		{
			int i = 0; for (auto& s : v | std::views::keys)
			{
				auto uHash2 = FNV1A::Hash32(s.c_str());
				if (uHash == uHash2)
					i++;
			}
			bValid = i <= 1;
		}

		if (bValid)
			++it;
		else
			it = v.erase(it);
	}
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, Gradient_t& v)
{
	if (auto tChild = t.get_child_optional(s))
	{
		LoadJson(*tChild, "StartColor", v.StartColor);
		LoadJson(*tChild, "EndColor", v.EndColor);
	}
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, DragBox_t& v)
{
	if (auto tChild = t.get_child_optional(s))
	{
		LoadJson(*tChild, "x", v.x);
		LoadJson(*tChild, "y", v.y);
	}
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, WindowBox_t& v)
{
	if (auto tChild = t.get_child_optional(s))
	{
		LoadJson(*tChild, "x", v.x);
		LoadJson(*tChild, "y", v.y);
		LoadJson(*tChild, "w", v.w);
		LoadJson(*tChild, "h", v.h);
	}
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, Chams_t& v)
{
	if (auto tChild = t.get_child_optional(s))
	{
		LoadJson(*tChild, "Visible", v.Visible);
		LoadJson(*tChild, "Occluded", v.Occluded);
	}
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, Glow_t& v)
{
	if (auto tChild = t.get_child_optional(s))
	{
		LoadJson(*tChild, "Stencil", v.Stencil);
		LoadJson(*tChild, "Blur", v.Blur);
	}
}



template <class T> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, ConfigVar<T>* c, int i)
{
	LoadJson(t, s, c->Map[i]);
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, ConfigVar<int>* c, int i)
{
	auto& v = c->Map[i];
	LoadJson(t, s, v);

	if (!c->m_vValues.empty())
	{
		if (c->m_iFlags & DROPDOWN_NOSANITIZATION)
			return;

		if (!(c->m_iFlags & DROPDOWN_MULTI))
			v = std::clamp(v, 0, int(c->m_vValues.size() - 1));
		else
		{
			for (int i = 0; i < sizeof(int) * 8; i++)
			{
				bool bFound = v & (1 << i) && i < c->m_vValues.size();
				if (!bFound)
					v &= ~(1 << i);
			}
		}
	}
	else if (c->m_sExtra)
	{
		if (!(c->m_iFlags & SLIDER_PRECISION))
			v = float(v) - fnmodf(float(v) - c->m_unStep.i / 2.f, c->m_unStep.i) + c->m_unStep.i / 2.f;
		if (c->m_iFlags & SLIDER_CLAMP)
			v = std::clamp(v, c->m_unMin.i, c->m_unMax.i);
		else if (c->m_iFlags & SLIDER_MIN)
			v = std::max(v, c->m_unMin.i);
		else if (c->m_iFlags & SLIDER_MAX)
			v = std::min(v, c->m_unMax.i);
	}
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, ConfigVar<float>* c, int i)
{
	auto& v = c->Map[i];
	LoadJson(t, s, v);

	if (!(c->m_iFlags & SLIDER_PRECISION))
		v = v - fnmodf(v - c->m_unStep.f / 2, c->m_unStep.f) + c->m_unStep.f / 2;
	if (c->m_iFlags & SLIDER_CLAMP)
		v = std::clamp(v, c->m_unMin.f, c->m_unMax.f);
	else if (c->m_iFlags & SLIDER_MIN)
		v = std::max(v, c->m_unMin.f);
	else if (c->m_iFlags & SLIDER_MAX)
		v = std::min(v, c->m_unMax.f);
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, ConfigVar<IntRange_t>* c, int i)
{
	auto& v = c->Map[i];
	LoadJson(t, s, v);

	if (!(c->m_iFlags & SLIDER_PRECISION))
	{
		v.Min = float(v.Min) - fnmodf(float(v.Min) - c->m_unStep.i / 2.f, c->m_unStep.i) + c->m_unStep.i / 2.f;
		v.Max = float(v.Max) - fnmodf(float(v.Max) - c->m_unStep.i / 2.f, c->m_unStep.i) + c->m_unStep.i / 2.f;
	}
	if (c->m_iFlags & SLIDER_CLAMP)
	{
		v.Min = std::clamp(v.Min, c->m_unMin.i, c->m_unMax.i - c->m_unStep.i);
		v.Max = std::clamp(v.Max, c->m_unMin.i + c->m_unStep.i, c->m_unMax.i);
	}
	else if (c->m_iFlags & SLIDER_MIN)
	{
		v.Min = std::max(v.Min, c->m_unMin.i);
		v.Max = std::max(v.Max, c->m_unMin.i + c->m_unStep.i);
	}
	else if (c->m_iFlags & SLIDER_MAX)
	{
		v.Min = std::min(v.Min, c->m_unMax.i - c->m_unStep.i);
		v.Max = std::min(v.Max, c->m_unMax.i);
	}
	v.Max = std::max(v.Max, v.Min + c->m_unStep.i);
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, ConfigVar<FloatRange_t>* c, int i)
{
	auto& v = c->Map[i];
	LoadJson(t, s, v);

	if (!(c->m_iFlags & SLIDER_PRECISION))
	{
		v.Min = v.Min - fnmodf(v.Min - c->m_unStep.f / 2, c->m_unStep.f) + c->m_unStep.f / 2;
		v.Max = v.Max - fnmodf(v.Max - c->m_unStep.f / 2, c->m_unStep.f) + c->m_unStep.f / 2;
	}
	if (c->m_iFlags & SLIDER_CLAMP)
	{
		v.Min = std::clamp(v.Min, c->m_unMin.f, c->m_unMax.f - c->m_unStep.f);
		v.Max = std::clamp(v.Max, c->m_unMin.f + c->m_unStep.f, c->m_unMax.f);
	}
	else if (c->m_iFlags & SLIDER_MIN)
	{
		v.Min = std::max(v.Min, c->m_unMin.f);
		v.Max = std::max(v.Max, c->m_unMin.f + c->m_unStep.f);
	}
	else if (c->m_iFlags & SLIDER_MAX)
	{
		v.Min = std::min(v.Min, c->m_unMax.f - c->m_unStep.f);
		v.Max = std::min(v.Max, c->m_unMax.f);
	}
	v.Max = std::max(v.Max, v.Min + c->m_unStep.f);
}



CConfigs::CConfigs()
{
	m_sConfigPath = std::filesystem::current_path().string() + "\\Nikogram\\";
	m_sVisualsPath = m_sConfigPath + "Visuals\\";
	m_sCorePath = m_sConfigPath + "Core\\";
	m_sMaterialsPath = m_sConfigPath + "Materials\\";

	if (!std::filesystem::exists(m_sConfigPath))
		std::filesystem::create_directory(m_sConfigPath);

	if (!std::filesystem::exists(m_sVisualsPath))
		std::filesystem::create_directory(m_sVisualsPath);

	if (!std::filesystem::exists(m_sCorePath))
		std::filesystem::create_directory(m_sCorePath);

	if (!std::filesystem::exists(m_sMaterialsPath))
		std::filesystem::create_directory(m_sMaterialsPath);
	EnsureDefaultConfig();
}

bool CConfigs::EnsureDefaultConfig()
{
	try
	{
		const auto path = m_sConfigPath + "default" + m_sConfigExtension;
		return std::filesystem::exists(path) || BundledDefault::Write(path);
	}
	catch (...) { return false; }
}

#define IsType(t) pBase->m_iType == typeid(t).hash_code()

template <class T>
static inline void SaveMain(BaseVar*& pBase, boost::property_tree::ptree& tTree)
{
	auto pVar = pBase->As<T>();

	boost::property_tree::ptree tMap;
	for (auto& [iBind, tValue] : pVar->Map)
		F::Configs.SaveJson(tMap, std::to_string(iBind), tValue);
	tTree.put_child(pVar->Name(), tMap);
}
#define Save(t, j) if (IsType(t)) SaveMain<t>(pBase, j);

template <class T>
static inline void LoadMain(BaseVar*& pBase, boost::property_tree::ptree& tTree)
{
	auto pVar = pBase->As<T>();

	pVar->Map = { { DEFAULT_BIND, pVar->Default } };
	if (auto tMap = tTree.get_child_optional(pVar->Name()))
	{
		for (auto& sKey : *tMap | std::views::keys)
		{
			int iBind = std::stoi(sKey);
			if (iBind == DEFAULT_BIND || F::Binds.m_vBinds.size() > iBind && !(pVar->m_iFlags & NOBIND))
			{
				F::Configs.LoadJson(*tMap, sKey, pVar, iBind);
				if (iBind != DEFAULT_BIND)
					std::next(F::Binds.m_vBinds.begin(), iBind)->m_vVars.push_back(pVar);
			}
		}
	}
	// Missing individual settings are normal when loading an older config.
	// The map was reset to the declared default above, so new features are safe
	// without carrying values/binds over from the previously loaded config.
	// Whole-section and parse errors remain reported by LoadConfig.
}
#define Load(t, j) if (IsType(t)) LoadMain<t>(pBase, j);

bool CConfigs::SaveConfig(const std::string& sConfigName, bool bNotify)
{
	try
	{
		boost::property_tree::ptree tWrite;

		{
			boost::property_tree::ptree tSub;
			for (int iID = 0; iID < F::Binds.m_vBinds.size(); iID++)
			{
				auto& tBind = F::Binds.m_vBinds[iID];

				boost::property_tree::ptree tChild;
				SaveJson(tChild, "Name", tBind.m_sName);
				SaveJson(tChild, "Type", tBind.m_iType);
				SaveJson(tChild, "Info", tBind.m_iInfo);
				SaveJson(tChild, "Key", tBind.m_iKey);
				SaveJson(tChild, "Enabled", tBind.m_bEnabled);
				SaveJson(tChild, "Visibility", tBind.m_iVisibility);
				SaveJson(tChild, "Not", tBind.m_bNot);
				SaveJson(tChild, "Active", tBind.m_bActive);
				SaveJson(tChild, "Parent", tBind.m_iParent);

				tSub.put_child(std::to_string(iID), tChild);
			}
			tWrite.put_child("Binds", tSub);
		}

		{
			boost::property_tree::ptree tSub;
			bool bNoSave = GetAsyncKeyState(VK_SHIFT) & 0x8000;
			for (auto& pBase : G::Vars)
			{
				if (!bNoSave && pBase->m_iFlags & NOSAVE)
					continue;

				Save(bool, tSub)
				else Save(int, tSub)
				else Save(float, tSub)
				else Save(IntRange_t, tSub)
				else Save(FloatRange_t, tSub)
				else Save(std::string, tSub)
				else Save(VA_LIST(std::vector<std::pair<std::string, Color_t>>), tSub)
				else Save(Color_t, tSub)
				else Save(Gradient_t, tSub)
				else Save(DragBox_t, tSub)
				else Save(WindowBox_t, tSub)
			}
			tWrite.put_child("Vars", tSub);
		}

		{
			boost::property_tree::ptree tSub;
			for (int iID = 0; iID < F::Groups.m_vGroups.size(); iID++)
			{
				auto& tGroup = F::Groups.m_vGroups[iID];

				boost::property_tree::ptree tChild;
				SaveJson(tChild, "Name", tGroup.m_sName);
				SaveJson(tChild, "Color", tGroup.m_tColor);
				SaveJson(tChild, "TagsOverrideColor", tGroup.m_bTagsOverrideColor);
				SaveJson(tChild, "Targets", tGroup.m_iTargets);
				SaveJson(tChild, "Conditions", tGroup.m_iConditions);
				SaveJson(tChild, "Players", tGroup.m_iPlayers);
				SaveJson(tChild, "Buildings", tGroup.m_iBuildings);
				SaveJson(tChild, "Projectiles", tGroup.m_iProjectiles);
				SaveJson(tChild, "ESP", tGroup.m_iESP);
				SaveJson(tChild, "CustomNameColor", tGroup.m_bCustomNameColor);
				SaveJson(tChild, "NameColor", tGroup.m_tNameColor);
				SaveJson(tChild, "Chams", tGroup.m_tChams);
				SaveJson(tChild, "Glow", tGroup.m_tGlow);
				SaveJson(tChild, "OffscreenArrows", tGroup.m_bOffscreenArrows);
				SaveJson(tChild, "OffscreenArrowsOffset", tGroup.m_iOffscreenArrowsOffset);
				SaveJson(tChild, "OffscreenArrowsMaxDistance", tGroup.m_flOffscreenArrowsMaxDistance);
				SaveJson(tChild, "PickupTimer", tGroup.m_bPickupTimer);
				SaveJson(tChild, "Backtrack", tGroup.m_iBacktrack);
				SaveJson(tChild, "BacktrackChams", tGroup.m_tBacktrackChams);
				SaveJson(tChild, "BacktrackGlow", tGroup.m_tBacktrackGlow);
				SaveJson(tChild, "Trajectory", tGroup.m_iTrajectory);
				SaveJson(tChild, "Sightlines", tGroup.m_iSightlines);

				tSub.put_child(std::to_string(iID), tChild);
				if (iID + 1 >= int(sizeof(int) * 8)) // groups are stored as bits in an int, so cap at 32
					break;
			}
			tWrite.put_child("Groups", tSub);
		}

		(Workspace::Load)(); // Config serializer defines function-like Load/Save macros.
		(ConfigColours::Save)(tWrite);
		write_json(m_sConfigPath + sConfigName + m_sConfigExtension, tWrite);

		m_sCurrentConfig = sConfigName; m_sCurrentVisuals = "";
		if (bNotify)
			SDK::Output("Nikogram", std::format("Config {} saved", sConfigName).c_str(), INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_INFO);
	}
	catch (...)
	{
		SDK::Output("Nikogram", "Save config failed", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);
		return false;
	}

	return true;
}

bool CConfigs::LoadConfig(const std::string& sConfigName, bool bNotify)
{
	try
	{
		if (!std::filesystem::exists(m_sConfigPath + sConfigName + m_sConfigExtension))
		{
			if (sConfigName != "default" || !EnsureDefaultConfig())
				return false;
		}

		boost::property_tree::ptree tRead;
		read_json(m_sConfigPath + sConfigName + m_sConfigExtension, tRead);

		F::Binds.m_vBinds.clear();
		F::Groups.m_vGroups.clear();

		if (auto tSub = tRead.get_child_optional("Binds"))
		{
			for (const auto& tChild : *tSub | std::views::values)
			{
				Bind_t tBind = {};
				LoadJson(tChild, "Name", tBind.m_sName);
				LoadJson(tChild, "Type", tBind.m_iType);
				LoadJson(tChild, "Info", tBind.m_iInfo);
				LoadJson(tChild, "Key", tBind.m_iKey);
				LoadJson(tChild, "Enabled", tBind.m_bEnabled);
				LoadJson(tChild, "Visibility", tBind.m_iVisibility);
				LoadJson(tChild, "Not", tBind.m_bNot);
				LoadJson(tChild, "Active", tBind.m_bActive);
				LoadJson(tChild, "Parent", tBind.m_iParent);
				if (F::Binds.m_vBinds.size() == tBind.m_iParent)
					tBind.m_iParent = DEFAULT_BIND - 1; // prevent infinite loop

				F::Binds.m_vBinds.push_back(tBind);
			}
		}
		else
			SDK::Output("Nikogram", "Config binds not found", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);

		// orphan binds whose parent is out of range or part of a cycle (e.g. a hand-edited config with A -> B -> A),
		// using the same DEFAULT_BIND - 1 value the self-parent check above uses, so they are never evaluated
		for (int iID = 0; iID < F::Binds.m_vBinds.size(); iID++)
		{
			auto& tBind = F::Binds.m_vBinds[iID];
			if (tBind.m_iParent == DEFAULT_BIND || tBind.m_iParent == DEFAULT_BIND - 1)
				continue;

			int iParent = tBind.m_iParent;
			for (size_t n = 0; iParent != DEFAULT_BIND; n++)
			{
				if (iParent == iID || n > F::Binds.m_vBinds.size() || iParent < 0 || iParent >= F::Binds.m_vBinds.size())
				{
					tBind.m_iParent = DEFAULT_BIND - 1;
					break;
				}
				iParent = F::Binds.m_vBinds[iParent].m_iParent;
			}
		}

		if (auto tSub = tRead.get_child_optional("Vars");
			tSub || (tSub = tRead.get_child_optional("ConVars")))
		{
			bool bNoSave = GetAsyncKeyState(VK_SHIFT) & 0x8000;
			for (auto& pBase : G::Vars)
			{
				if (!bNoSave && pBase->m_iFlags & NOSAVE)
					continue;

				Load(bool, *tSub)
				else Load(int, *tSub)
				else Load(float, *tSub)
				else Load(IntRange_t, *tSub)
				else Load(FloatRange_t, *tSub)
				else Load(std::string, *tSub)
				else Load(VA_LIST(std::vector<std::pair<std::string, Color_t>>), *tSub)
				else Load(Color_t, *tSub)
				else Load(Gradient_t, *tSub)
				else Load(DragBox_t, *tSub)
				else Load(WindowBox_t, *tSub)
			}
		}
		else
			SDK::Output("Nikogram", "Config vars not found", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);

		if (auto tSub = tRead.get_child_optional("Groups"))
		{
			for (auto& tChild : *tSub | std::views::values)
			{
				Group_t tGroup = {};
				LoadJson(tChild, "Name", tGroup.m_sName);
				LoadJson(tChild, "Color", tGroup.m_tColor);
				LoadJson(tChild, "TagsOverrideColor", tGroup.m_bTagsOverrideColor);
				LoadJson(tChild, "Targets", tGroup.m_iTargets);
				LoadJson(tChild, "Conditions", tGroup.m_iConditions);
				LoadJson(tChild, "Players", tGroup.m_iPlayers);
				LoadJson(tChild, "Buildings", tGroup.m_iBuildings);
				LoadJson(tChild, "Projectiles", tGroup.m_iProjectiles);
				LoadJson(tChild, "ESP", tGroup.m_iESP);
				LoadJson(tChild, "CustomNameColor", tGroup.m_bCustomNameColor);
				LoadJson(tChild, "NameColor", tGroup.m_tNameColor);
				LoadJson(tChild, "Chams", tGroup.m_tChams);
				LoadJson(tChild, "Glow", tGroup.m_tGlow);
				LoadJson(tChild, "OffscreenArrows", tGroup.m_bOffscreenArrows);
				LoadJson(tChild, "OffscreenArrowsOffset", tGroup.m_iOffscreenArrowsOffset);
				LoadJson(tChild, "OffscreenArrowsMaxDistance", tGroup.m_flOffscreenArrowsMaxDistance);
				LoadJson(tChild, "PickupTimer", tGroup.m_bPickupTimer);
				LoadJson(tChild, "Backtrack", tGroup.m_iBacktrack);
				LoadJson(tChild, "BacktrackChams", tGroup.m_tBacktrackChams);
				LoadJson(tChild, "BacktrackGlow", tGroup.m_tBacktrackGlow);
				LoadJson(tChild, "Trajectory", tGroup.m_iTrajectory);
				LoadJson(tChild, "Sightlines", tGroup.m_iSightlines);

				if (F::Groups.m_vGroups.size() < 32) // ActiveGroups is a 32-bit mask
					F::Groups.m_vGroups.push_back(tGroup);
			}
		}
		else
			SDK::Output("Nikogram", "Config groups not found", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);

		F::Binds.SetVars(nullptr, nullptr, false);
		H::Fonts.Reload();

		(Workspace::Load)();
		(ConfigColours::Load)(tRead);
		m_sCurrentConfig = sConfigName; m_sCurrentVisuals = "";
		if (bNotify)
			SDK::Output("Nikogram", std::format("Config {} loaded", sConfigName).c_str(), INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_INFO);
	}
	catch (...)
	{
		SDK::Output("Nikogram", "Load config failed", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);
		return false;
	}

	return true;
}

template <class T>
static inline void SaveMiscMain(BaseVar*& pBase, boost::property_tree::ptree& tTree)
{
	F::Configs.SaveJson(tTree, pBase->Name(), pBase->As<T>()->Map[DEFAULT_BIND]);
}
#define SaveMisc(t, j) if (IsType(t)) SaveMiscMain<t>(pBase, j);

template <class T>
static inline void LoadMiscMain(BaseVar*& pBase, boost::property_tree::ptree& tTree)
{
	F::Configs.LoadJson(tTree, pBase->Name(), pBase->As<T>(), DEFAULT_BIND);
}
#define LoadMisc(t, j) if (IsType(t)) LoadMiscMain<t>(pBase, j);

bool CConfigs::SaveVisual(const std::string& sConfigName, bool bNotify)
{
	try
	{
		boost::property_tree::ptree tWrite;

		{
			boost::property_tree::ptree tSub;
			bool bNoSave = GetAsyncKeyState(VK_SHIFT) & 0x8000;
			for (auto& pBase : G::Vars)
			{
				if (!(pBase->m_iFlags & VISUAL) || !bNoSave && pBase->m_iFlags & NOSAVE)
					continue;

				SaveMisc(bool, tSub)
				else SaveMisc(int, tSub)
				else SaveMisc(float, tSub)
				else SaveMisc(IntRange_t, tSub)
				else SaveMisc(FloatRange_t, tSub)
				else SaveMisc(std::string, tSub)
				else SaveMisc(VA_LIST(std::vector<std::pair<std::string, Color_t>>), tSub)
				else SaveMisc(Color_t, tSub)
				else SaveMisc(Gradient_t, tSub)
				else SaveMisc(DragBox_t, tSub)
				else SaveMisc(WindowBox_t, tSub)
			}
			tWrite.put_child("Vars", tSub);
		}

		{
			boost::property_tree::ptree tSub;
			for (int iID = 0; iID < F::Groups.m_vGroups.size(); iID++)
			{
				auto& tGroup = F::Groups.m_vGroups[iID];

				boost::property_tree::ptree tChild;
				SaveJson(tChild, "Name", tGroup.m_sName);
				SaveJson(tChild, "Color", tGroup.m_tColor);
				SaveJson(tChild, "TagsOverrideColor", tGroup.m_bTagsOverrideColor);
				SaveJson(tChild, "Targets", tGroup.m_iTargets);
				SaveJson(tChild, "Conditions", tGroup.m_iConditions);
				SaveJson(tChild, "Players", tGroup.m_iPlayers);
				SaveJson(tChild, "Buildings", tGroup.m_iBuildings);
				SaveJson(tChild, "Projectiles", tGroup.m_iProjectiles);
				SaveJson(tChild, "ESP", tGroup.m_iESP);
				SaveJson(tChild, "CustomNameColor", tGroup.m_bCustomNameColor);
				SaveJson(tChild, "NameColor", tGroup.m_tNameColor);
				SaveJson(tChild, "Chams", tGroup.m_tChams);
				SaveJson(tChild, "Glow", tGroup.m_tGlow);
				SaveJson(tChild, "OffscreenArrows", tGroup.m_bOffscreenArrows);
				SaveJson(tChild, "OffscreenArrowsOffset", tGroup.m_iOffscreenArrowsOffset);
				SaveJson(tChild, "OffscreenArrowsMaxDistance", tGroup.m_flOffscreenArrowsMaxDistance);
				SaveJson(tChild, "PickupTimer", tGroup.m_bPickupTimer);
				SaveJson(tChild, "Backtrack", tGroup.m_iBacktrack);
				SaveJson(tChild, "BacktrackChams", tGroup.m_tBacktrackChams);
				SaveJson(tChild, "BacktrackGlow", tGroup.m_tBacktrackGlow);
				SaveJson(tChild, "Trajectory", tGroup.m_iTrajectory);
				SaveJson(tChild, "Sightlines", tGroup.m_iSightlines);

				tSub.put_child(std::to_string(iID), tChild);
				if (iID + 1 >= int(sizeof(int) * 8)) // groups are stored as bits in an int, so cap at 32
					break;
			}
			tWrite.put_child("Groups", tSub);
		}

		write_json(m_sVisualsPath + sConfigName + m_sConfigExtension, tWrite);

		if (bNotify)
			SDK::Output("Nikogram", std::format("Visual config {} saved", sConfigName).c_str(), INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_INFO);
	}
	catch (...)
	{
		SDK::Output("Nikogram", "Save visuals failed", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);
		return false;
	}
	return true;
}

bool CConfigs::LoadVisual(const std::string& sConfigName, bool bNotify)
{
	try
	{
		if (!std::filesystem::exists(m_sVisualsPath + sConfigName + m_sConfigExtension))
			return false;

		boost::property_tree::ptree tRead;
		read_json(m_sVisualsPath + sConfigName + m_sConfigExtension, tRead);

		F::Groups.m_vGroups.clear();

		if (auto tSub = tRead.get_child_optional("Vars");
			tSub || (tSub = tRead))
		{
			bool bNoSave = GetAsyncKeyState(VK_SHIFT) & 0x8000;
			for (auto& pBase : G::Vars)
			{
				if (!(pBase->m_iFlags & VISUAL) || !bNoSave && pBase->m_iFlags & NOSAVE)
					continue;

				LoadMisc(bool, *tSub)
				else LoadMisc(int, *tSub)
				else LoadMisc(float, *tSub)
				else LoadMisc(IntRange_t, *tSub)
				else LoadMisc(FloatRange_t, *tSub)
				else LoadMisc(std::string, *tSub)
				else LoadMisc(VA_LIST(std::vector<std::pair<std::string, Color_t>>), *tSub)
				else LoadMisc(Color_t, *tSub)
				else LoadMisc(Gradient_t, *tSub)
				else LoadMisc(DragBox_t, *tSub)
				else LoadMisc(WindowBox_t, *tSub)
			}
		}
		else
			SDK::Output("Nikogram", "Config vars not found", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);

		if (auto tSub = tRead.get_child_optional("Groups"))
		{
			for (auto& tChild : *tSub | std::views::values)
			{
				Group_t tGroup = {};
				LoadJson(tChild, "Name", tGroup.m_sName);
				LoadJson(tChild, "Color", tGroup.m_tColor);
				LoadJson(tChild, "TagsOverrideColor", tGroup.m_bTagsOverrideColor);
				LoadJson(tChild, "Targets", tGroup.m_iTargets);
				LoadJson(tChild, "Conditions", tGroup.m_iConditions);
				LoadJson(tChild, "Players", tGroup.m_iPlayers);
				LoadJson(tChild, "Buildings", tGroup.m_iBuildings);
				LoadJson(tChild, "Projectiles", tGroup.m_iProjectiles);
				LoadJson(tChild, "ESP", tGroup.m_iESP);
				LoadJson(tChild, "CustomNameColor", tGroup.m_bCustomNameColor);
				LoadJson(tChild, "NameColor", tGroup.m_tNameColor);
				LoadJson(tChild, "Chams", tGroup.m_tChams);
				LoadJson(tChild, "Glow", tGroup.m_tGlow);
				LoadJson(tChild, "OffscreenArrows", tGroup.m_bOffscreenArrows);
				LoadJson(tChild, "OffscreenArrowsOffset", tGroup.m_iOffscreenArrowsOffset);
				LoadJson(tChild, "OffscreenArrowsMaxDistance", tGroup.m_flOffscreenArrowsMaxDistance);
				LoadJson(tChild, "PickupTimer", tGroup.m_bPickupTimer);
				LoadJson(tChild, "Backtrack", tGroup.m_iBacktrack);
				LoadJson(tChild, "BacktrackChams", tGroup.m_tBacktrackChams);
				LoadJson(tChild, "BacktrackGlow", tGroup.m_tBacktrackGlow);
				LoadJson(tChild, "Trajectory", tGroup.m_iTrajectory);
				LoadJson(tChild, "Sightlines", tGroup.m_iSightlines);

				if (F::Groups.m_vGroups.size() < 32) // ActiveGroups is a 32-bit mask
					F::Groups.m_vGroups.push_back(tGroup);
			}
		}
		else
			SDK::Output("Nikogram", "Config groups not found", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);

		F::Binds.SetVars(nullptr, nullptr, false);

		m_sCurrentVisuals = sConfigName;
		if (bNotify)
			SDK::Output("Nikogram", std::format("Visual config {} loaded", sConfigName).c_str(), INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_INFO);
	}
	catch (...)
	{
		SDK::Output("Nikogram", "Load visuals failed", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);
		return false;
	}
	return true;
}

template <class T>
static inline void ResetMain(BaseVar*& pBase)
{
	auto pVar = pBase->As<T>();

	pVar->Map = { { DEFAULT_BIND, pVar->Default } };
}
#define Reset(t) if (IsType(t)) ResetMain<t>(pBase);

void CConfigs::DeleteConfig(const std::string& sConfigName, bool bNotify)
{
	try
	{
		if (FNV1A::Hash32(sConfigName.c_str()) == FNV1A::Hash32Const("default"))
		{
			ResetConfig(sConfigName);
			return;
		}

		std::filesystem::remove(m_sConfigPath + sConfigName + m_sConfigExtension);
		if (FNV1A::Hash32(m_sCurrentConfig.c_str()) == FNV1A::Hash32(sConfigName.c_str()))
			LoadConfig("default", false);

		if (bNotify)
			SDK::Output("Nikogram", std::format("Config {} deleted", sConfigName).c_str(), INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_INFO);
	}
	catch (...)
	{
		SDK::Output("Nikogram", "Remove config failed", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);
	}
}

void CConfigs::ResetConfig(const std::string& sConfigName, bool bNotify)
{
	try
	{
		if (sConfigName == "default")
		{
			if (!BundledDefault::Write(m_sConfigPath + sConfigName + m_sConfigExtension, true) || !LoadConfig(sConfigName, false))
				throw std::runtime_error("Could not restore bundled default");
			if (bNotify)
				SDK::Output("Nikogram", "Config default reset", INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_INFO);
			return;
		}
		F::Binds.m_vBinds.clear();
		F::Groups.m_vGroups.clear();

		bool bNoSave = GetAsyncKeyState(VK_SHIFT) & 0x8000;
		for (auto& pBase : G::Vars)
		{
			if (!bNoSave && pBase->m_iFlags & NOSAVE)
				continue;

			Reset(bool)
			else Reset(int)
			else Reset(float)
			else Reset(IntRange_t)
			else Reset(FloatRange_t)
			else Reset(std::string)
			else Reset(VA_LIST(std::vector<std::pair<std::string, Color_t>>))
			else Reset(Color_t)
			else Reset(Gradient_t)
			else Reset(DragBox_t)
			else Reset(WindowBox_t)
		}

		SaveConfig(sConfigName, false);
		F::Binds.SetVars(nullptr, nullptr, false);
		H::Fonts.Reload();

		if (bNotify)
			SDK::Output("Nikogram", std::format("Config {} reset", sConfigName).c_str(), INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_INFO);
	}
	catch (...)
	{
		SDK::Output("Nikogram", "Reset config failed", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);
	}
}

void CConfigs::DeleteVisual(const std::string& sConfigName, bool bNotify)
{
	try
	{
		std::filesystem::remove(m_sVisualsPath + sConfigName + m_sConfigExtension);

		if (bNotify)
			SDK::Output("Nikogram", std::format("Visual config {} deleted", sConfigName).c_str(), INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_INFO);
	}
	catch (...)
	{
		SDK::Output("Nikogram", "Remove visuals failed", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);
	}
}

void CConfigs::ResetVisual(const std::string& sConfigName, bool bNotify)
{
	try
	{
		F::Groups.m_vGroups.clear();

		bool bNoSave = GetAsyncKeyState(VK_SHIFT) & 0x8000;
		for (auto& pBase : G::Vars)
		{
			if (!(pBase->m_iFlags & VISUAL) || !bNoSave && pBase->m_iFlags & NOSAVE)
				continue;

			Reset(bool)
			else Reset(int)
			else Reset(float)
			else Reset(IntRange_t)
			else Reset(FloatRange_t)
			else Reset(std::string)
			else Reset(VA_LIST(std::vector<std::pair<std::string, Color_t>>))
			else Reset(Color_t)
			else Reset(Gradient_t)
			else Reset(DragBox_t)
			else Reset(WindowBox_t)
		}

		SaveVisual(sConfigName, false);
		F::Binds.SetVars(nullptr, nullptr, false);

		if (bNotify)
			SDK::Output("Nikogram", std::format("Visual config {} reset", sConfigName).c_str(), INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_INFO);
	}
	catch (...)
	{
		SDK::Output("Nikogram", "Reset visuals failed", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);
	}
}
