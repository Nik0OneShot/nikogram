#include "Events.h"
#include "../../Features/Aimbot/SelfDamageDiagnostics.h"

#include "../../Core/Core.h"
#include "../../Features/Statistics/Statistics.h"
#include "../../Features/LearningAccess.h"
#include "../../Features/Aimbot/AutoHeal/AutoHeal.h"
#include "../../Features/Backtrack/Backtrack.h"
#include "../../Features/CheatDetection/CheatDetection.h"
#include "../../Features/CritHack/CritHack.h"
#include "../../Features/Misc/Misc.h"
#include "../../Features/PacketManip/AntiAim/AntiAim.h"
#include "../../Features/Output/Output.h"
#include "../../Features/Resolver/Resolver.h"
#include "../../Features/Visuals/Visuals.h"

bool CEventListener::Initialize()
{
	std::vector<const char*> vEvents = { 
		"client_beginconnect", "client_connected", "client_disconnect", "game_newmap", "teamplay_round_start", "scorestats_accumulated_update", "mvm_reset_stats", "player_connect_client", "player_spawn", "player_changeclass", "player_hurt", "vote_cast", "item_pickup", "revive_player_notify"
	};

	for (auto szEvent : vEvents)
	{
		I::GameEventManager->AddListener(this, szEvent, false);

		if (!I::GameEventManager->FindListener(this, szEvent))
		{
			U::Core.AppendFailText(std::format("Failed to add listener: {}", szEvent).c_str());
			m_bFailed = true;
		}
	}

	// Optional feature event: an unavailable schema must not prevent the rest
	// of Nikogram from loading. Statistics reports that limitation in its UI.
	const bool deathEvents = I::GameEventManager->AddListener(this, "player_death", false);
	Statistics::DeathEventsAvailable(deathEvents && I::GameEventManager->FindListener(this, "player_death"));
	return !m_bFailed;
}

void CEventListener::Unload()
{
	I::GameEventManager->RemoveListener(this);
}

void CEventListener::FireGameEvent(IGameEvent* pEvent)
{
	if (!pEvent)
		return;
	Statistics::Event(pEvent);
	PrivateLearning::Event(pEvent);

	auto pLocal = H::Entities.GetLocal();
	auto uHash = FNV1A::Hash32(pEvent->GetName());

	F::Output.Event(pEvent, uHash, pLocal);
	if (I::EngineClient->IsPlayingDemo())
		return;

	F::CritHack.Event(pEvent, uHash, pLocal);
	F::AutoHeal.Event(pEvent, uHash);
	F::Misc.Event(pEvent, uHash);
	F::Visuals.Event(pEvent, uHash);
	switch (uHash)
	{
	case FNV1A::Hash32Const("player_hurt"):
	{
		if(SelfDamageDiagnostics::Enabled() && I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid"))==I::EngineClient->GetLocalPlayer())
			SelfDamageDiagnostics::Write("local_hurt",std::format("damage={} health={} self={} weaponid={} crit={}",pEvent->GetInt("damageamount"),pEvent->GetInt("health"),pEvent->GetInt("attacker")==pEvent->GetInt("userid"),pEvent->GetInt("weaponid"),pEvent->GetBool("crit")));
		F::Resolver.PlayerHurt(pEvent);
		F::CheatDetection.ReportDamage(pEvent);
		return;
	}
	case FNV1A::Hash32Const("player_spawn"):
	{
		if (I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid")) != I::EngineClient->GetLocalPlayer())
			return;

		F::Backtrack.SetLerp();
		return;
	}
	case FNV1A::Hash32Const("revive_player_notify"):
	{
		if (!Vars::Misc::MannVsMachine::InstantRevive.Value || pEvent->GetInt("entindex") != I::EngineClient->GetLocalPlayer())
			return;

		KeyValues* kv = new KeyValues("MVM_Revive_Response");
		kv->SetBool("accepted", true);
		I::EngineClient->ServerCmdKeyValues(kv);
	}
	}
}
