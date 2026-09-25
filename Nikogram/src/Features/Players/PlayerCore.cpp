#include "PlayerCore.h"

#include "PlayerUtils.h"
#include "../Configs/Configs.h"

void CPlayerlistCore::Run()
{
	static Timer tTimer = {};
	if (!tTimer.Run(1.f))
		return;

	LoadPlayerlist();
	SavePlayerlist();
}

void CPlayerlistCore::SavePlayerlist()
{
	if (!F::PlayerUtils.m_bSave || F::PlayerUtils.m_bLoad) // terrible if we end up saving while loading
		return;

	F::PlayerUtils.m_bSave = false;

	try
	{
		boost::property_tree::ptree tWrite;

		{
			boost::property_tree::ptree tSub;
			for (auto it = F::PlayerUtils.m_vTags.begin(); it != F::PlayerUtils.m_vTags.end(); it++)
			{
				int iID = std::distance(F::PlayerUtils.m_vTags.begin(), it);
				auto& tTag = *it;

				boost::property_tree::ptree tChild;
				F::Configs.SaveJson(tChild, "Name", tTag.m_sName);
				F::Configs.SaveJson(tChild, "Color", tTag.m_tColor);
				F::Configs.SaveJson(tChild, "Priority", tTag.m_iPriority);
				F::Configs.SaveJson(tChild, "Label", tTag.m_bLabel);

				tSub.put_child(std::to_string(F::PlayerUtils.IndexToTag(iID)), tChild);
			}
			tWrite.put_child("Config", tSub);
		}

		{
			boost::property_tree::ptree tSub;
			for (auto& [uAccountID, vTags] : F::PlayerUtils.m_mPlayerTags)
			{
				if (vTags.empty())
					continue;

				boost::property_tree::ptree tChild;
				for (auto& iID : vTags)
				{
					boost::property_tree::ptree t;
					t.put("", F::PlayerUtils.IndexToTag(iID));
					tChild.push_back({ "", t });
				}

				tSub.put_child(std::to_string(uAccountID), tChild);
			}
			tWrite.put_child("Tags", tSub);
		}

		{
			boost::property_tree::ptree tSub;
			for (auto& [uAccountID, sAlias] : F::PlayerUtils.m_mPlayerAliases)
			{
				if (!sAlias.empty())
					tSub.put(std::to_string(uAccountID), sAlias);
			}
			tWrite.put_child("Aliases", tSub);
		}

		write_json(F::Configs.m_sCorePath + "Players.json", tWrite);

		SDK::Output("Nikogram", "Saved playerlist", INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_INFO);
	}
	catch (...)
	{
		SDK::Output("Nikogram", "Save playerlist failed", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);
	}
}

void CPlayerlistCore::LoadPlayerlist()
{
	if (!F::PlayerUtils.m_bLoad)
		return;

	try
	{
		if (!std::filesystem::exists(F::Configs.m_sCorePath + "Players.json"))
		{
			F::PlayerUtils.m_bLoad = false;
			return;
		}

		boost::property_tree::ptree tRead;
		read_json(F::Configs.m_sCorePath + "Players.json", tRead);

		// parse everything into temporaries first, so a bad entry can't leave the live list half-loaded
		// (a half-loaded list would otherwise get written over Players.json on the next save)
		std::unordered_map<uint32_t, std::vector<int>> mPlayerTags = {};
		std::unordered_map<uint32_t, std::string> mPlayerAliases = {};
		std::vector<PriorityLabel_t> vTags = {
			{ "Default", { 200, 200, 200, 255 }, 0, false, false, true },
			{ "Ignored", { 200, 200, 200, 255 }, -1, false, true, true },
			{ "Cheater", { 255, 100, 100, 255 }, 1, false, true, true },
			{ "Friend", { 100, 255, 100, 255 }, 0, true, false, true },
			{ "Party", { 100, 100, 255, 255 }, 0, true, false, true },
			{ "F2P", { 255, 255, 255, 255 }, 0, true, false, true }
		};

		if (auto tSub = tRead.get_child_optional("Config"))
		{
			for (auto& [sName, tChild] : *tSub)
			{
				PriorityLabel_t tTag = {};
				F::Configs.LoadJson(tChild, "Name", tTag.m_sName);
				F::Configs.LoadJson(tChild, "Color", tTag.m_tColor);
				F::Configs.LoadJson(tChild, "Priority", tTag.m_iPriority);
				F::Configs.LoadJson(tChild, "Label", tTag.m_bLabel);

				int iID = F::PlayerUtils.TagToIndex(std::stoi(sName));
				if (iID > -1 && iID < vTags.size())
				{
					vTags[iID].m_sName = tTag.m_sName;
					vTags[iID].m_tColor = tTag.m_tColor;
					vTags[iID].m_iPriority = tTag.m_iPriority;
					vTags[iID].m_bLabel = tTag.m_bLabel;
				}
				else
					vTags.push_back(tTag);
			}
		}
		else
			SDK::Output("Nikogram", "Playerlist config not found", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);

		if (auto tSub = tRead.get_child_optional("Tags"))
		{
			for (auto& [sName, tChild] : *tSub)
			{
				uint32_t uAccountID = std::stoul(sName);
				if (!uAccountID)
					continue;

				for (auto& tTag : tChild | std::views::values)
				{
					int iID = F::PlayerUtils.TagToIndex(std::stoi(tTag.data()));
					if (iID < 0 || iID >= vTags.size() || !vTags[iID].m_bAssignable)
						continue;

					auto& vPlayer = mPlayerTags[uAccountID];
					if (std::ranges::find(vPlayer, iID) == vPlayer.end())
						vPlayer.push_back(iID);
				}
			}
		}
		else
			SDK::Output("Nikogram", "Playerlist tags not found", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);

		if (auto tSub = tRead.get_child_optional("Aliases"))
		{
			for (auto& [sName, jAlias] : *tSub)
			{
				uint32_t uAccountID = std::stoul(sName);
				const std::string& sAlias = jAlias.data();

				if (uAccountID && !sAlias.empty())
					mPlayerAliases[uAccountID] = sAlias;
			}
		}
		else
			SDK::Output("Nikogram", "Playerlist aliases not found", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);

		// everything parsed, now apply it
		F::PlayerUtils.m_vTags = std::move(vTags);
		F::PlayerUtils.m_mPlayerTags = std::move(mPlayerTags);
		F::PlayerUtils.m_mPlayerAliases = std::move(mPlayerAliases);

		SDK::Output("Nikogram", "Loaded playerlist", INFO_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_INFO);
	}
	catch (...)
	{
		// keep whatever was in memory and don't overwrite the file with it until the user changes something
		F::PlayerUtils.m_bSave = false;
		SDK::Output("Nikogram", "Load playerlist failed, Players.json was left untouched", ERROR_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG, ICON_MD_CANCEL);
	}

	F::PlayerUtils.m_bLoad = false;
}
