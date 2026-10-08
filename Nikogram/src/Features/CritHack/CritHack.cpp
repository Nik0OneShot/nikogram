#include "CritHack.h"
#include "../ImGui/MoonlitHud.h"
#include "CritIndicatorStyle.h"

#include "../Ticks/Ticks.h"
#include "../ImGui/Workspace.h"
#include "../ImGui/Menu/Menu.h"
#include "../ImGui/Menu/BindLayout.h"
#include "../ImGui/Notifications/NotificationStyle.h"
#include "../AntiCheatCompatibility/AntiCheatCompatibility.h"

#define WEAPON_RANDOM_RANGE				10000
#define TF_DAMAGE_CRIT_MULTIPLIER		3.0f
#define TF_DAMAGE_CRIT_CHANCE			0.02f
#define TF_DAMAGE_CRIT_CHANCE_RAPID		0.02f
#define TF_DAMAGE_CRIT_CHANCE_MELEE		0.15f
#define TF_DAMAGE_CRIT_DURATION_RAPID	2.0f

#define STATS_SEND_FREQUENCY 1.f

#define SEED_ATTEMPTS 4096
#define BUCKET_ATTEMPTS 1000

//#define SERVER_CRIT_DATA

int CCritHack::GetCritCommand(CTFWeaponBase* pWeapon, int iCommandNumber, bool bCrit, bool bSafe)
{
	for (int i = iCommandNumber; i < iCommandNumber + SEED_ATTEMPTS; i++)
	{
		if (IsCritCommand(i, pWeapon, bCrit, bSafe))
			return i;
	}
	return 0;
}

bool CCritHack::IsCritCommand(int iCommandNumber, CTFWeaponBase* pWeapon, bool bCrit, bool bSafe)
{
	int iSeed = CommandToSeed(iCommandNumber);
	return IsCritSeed(iSeed, pWeapon, bCrit, bSafe);
}

bool CCritHack::IsCritSeed(int iSeed, CTFWeaponBase* pWeapon, bool bCrit, bool bSafe)
{
	if (iSeed == pWeapon->m_iCurrentSeed())
		return false;

	SDK::RandomSeed(iSeed);
	int iRandom = SDK::RandomInt(0, WEAPON_RANDOM_RANGE - 1);

	if (bSafe)
	{
		int iLower, iUpper;
		if (m_bMelee)
			iLower = 1500, iUpper = 6000;
		else
			iLower = 100, iUpper = 800;
		iLower *= m_flMultCritChance, iUpper *= m_flMultCritChance;

		if (bCrit ? iLower >= 0 : iUpper < WEAPON_RANDOM_RANGE)
			return bCrit ? iRandom < iLower : !(iRandom < iUpper);
	}

	int iRange = m_flCritChance * WEAPON_RANDOM_RANGE;
	return bCrit ? iRandom < iRange : !(iRandom < iRange);
}

int CCritHack::CommandToSeed(int iCommandNumber)
{
	int iSeed = MD5_PseudoRandom(iCommandNumber) & std::numeric_limits<int>::max();
	int iMask = m_bMelee
		? m_iEntIndex << 16 | I::EngineClient->GetLocalPlayer() << 8
		: m_iEntIndex << 8 | I::EngineClient->GetLocalPlayer();
	return iSeed ^ iMask;
}



void CCritHack::UpdateWeaponInfo(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	m_iEntIndex = pWeapon->entindex();
	m_bMelee = pWeapon->GetSlot() == SLOT_MELEE;
	if (m_bMelee)
		m_flCritChance = TF_DAMAGE_CRIT_CHANCE_MELEE * pLocal->GetCritMult();
	else if (pWeapon->IsRapidFire())
	{
		m_flCritChance = TF_DAMAGE_CRIT_CHANCE_RAPID * pLocal->GetCritMult();
		float flNonCritDuration = (TF_DAMAGE_CRIT_DURATION_RAPID / m_flCritChance) - TF_DAMAGE_CRIT_DURATION_RAPID;
		m_flCritChance = 1.f / flNonCritDuration;
	}
	else
		m_flCritChance = TF_DAMAGE_CRIT_CHANCE * pLocal->GetCritMult();
	m_flMultCritChance = SDK::AttribHookValue(1.f, "mult_crit_chance", pWeapon);
	m_flCritChance *= m_flMultCritChance;



	static CTFWeaponBase* pStaticWeapon = nullptr;
	const CTFWeaponBase* pOldWeapon = pStaticWeapon;
	pStaticWeapon = pWeapon;

	static float flStaticBucket = 0.f;
	const float flLastBucket = flStaticBucket;
	const float flBucket = flStaticBucket = pWeapon->m_flCritTokenBucket();

	static int iStaticCritChecks = 0.f;
	const int iLastCritChecks = iStaticCritChecks;
	const int iCritChecks = iStaticCritChecks = pWeapon->m_nCritChecks();

	static int iStaticCritSeedRequests = 0.f;
	const int iLastCritSeedRequests = iStaticCritSeedRequests;
	const int iCritSeedRequests = iStaticCritSeedRequests = pWeapon->m_nCritSeedRequests();

	if (pWeapon == pOldWeapon && flBucket == flLastBucket && iCritChecks == iLastCritChecks && iCritSeedRequests == iLastCritSeedRequests)
		return;

	static auto tf_weapon_criticals_bucket_cap = H::ConVars.FindVar("tf_weapon_criticals_bucket_cap");
	const float flBucketCap = tf_weapon_criticals_bucket_cap->GetFloat();
	bool bRapidFire = pWeapon->IsRapidFire();
	float flFireRate = pWeapon->GetFireRate();

	float flDamage = pWeapon->GetDamage();
	int nProjectilesPerShot = pWeapon->GetBulletsPerShot(false);
	if (!m_bMelee && nProjectilesPerShot > 0)
		nProjectilesPerShot = SDK::AttribHookValue(nProjectilesPerShot, "mult_bullets_per_shot", pWeapon);
	else
		nProjectilesPerShot = 1;
	float flBaseDamage = flDamage *= nProjectilesPerShot;
	if (bRapidFire && flFireRate > 0.f)
	{
		flDamage *= TF_DAMAGE_CRIT_DURATION_RAPID / flFireRate;
		if (flDamage * TF_DAMAGE_CRIT_MULTIPLIER > flBucketCap)
			flDamage = flBucketCap / TF_DAMAGE_CRIT_MULTIPLIER;
	}

	float flMult = m_bMelee ? 0.5f : Math::RemapVal(float(iCritSeedRequests + 1) / (iCritChecks + 1), 0.1f, 1.f, 1.f, 3.f);
	float flCost = flDamage * TF_DAMAGE_CRIT_MULTIPLIER;

	const float flPotentialCost = TF_DAMAGE_CRIT_MULTIPLIER * flDamage / (m_bMelee ? 2 : 1) - flBaseDamage;
	int iPotentialCrits = flPotentialCost > 0.f ? int((std::max(flBucketCap, flBucket) - flBaseDamage) / flPotentialCost) : 0;
	int iAvailableCrits = 0;
	{
		int iTestShots = iCritChecks, iTestCrits = iCritSeedRequests;
		float flTestBucket = flBucket;
		for (int i = 0; i < BUCKET_ATTEMPTS; i++)
		{
			iTestShots++; iTestCrits++;

			float flTestMult = m_bMelee ? 0.5f : Math::RemapVal(float(iTestCrits) / iTestShots, 0.1f, 1.f, 1.f, 3.f);
			if (flTestBucket < flBucketCap)
				flTestBucket = std::min(flTestBucket + flBaseDamage, flBucketCap);
			flTestBucket -= flCost * flTestMult;
			if (flTestBucket < 0.f)
				break;

			iAvailableCrits++;
		}
	}

	int iNextCrit = 0;
	if (iAvailableCrits != iPotentialCrits)
	{
		int iTestShots = iCritChecks, iTestCrits = iCritSeedRequests;
		float flTestBucket = flBucket;
		float flTickBase = I::GlobalVars->curtime;
		float flLastRapidFireCritCheckTime = pWeapon->m_flLastRapidFireCritCheckTime();
		for (int i = 0; i < BUCKET_ATTEMPTS; i++)
		{
			int iCrits = 0;
			{
				int iTestShots2 = iTestShots, iTestCrits2 = iTestCrits;
				float flTestBucket2 = flTestBucket;
				for (int j = 0; j < BUCKET_ATTEMPTS; j++)
				{
					iTestShots2++; iTestCrits2++;

					float flTestMult = m_bMelee ? 0.5f : Math::RemapVal(float(iTestCrits2) / iTestShots2, 0.1f, 1.f, 1.f, 3.f);
					if (flTestBucket2 < flBucketCap)
						flTestBucket2 = std::min(flTestBucket2 + flBaseDamage, flBucketCap);
					flTestBucket2 -= flCost * flTestMult;
					if (flTestBucket2 < 0.f)
						break;

					iCrits++;
				}
			}
			if (iAvailableCrits < iCrits)
				break;

			if (!bRapidFire)
				iTestShots++;
			else 
			{
				flTickBase += std::ceilf(flFireRate / TICK_INTERVAL) * TICK_INTERVAL;
				if (flTickBase >= flLastRapidFireCritCheckTime + 1.f || !i && flTestBucket == flBucketCap)
				{
					iTestShots++;
					flLastRapidFireCritCheckTime = flTickBase;
				}
			}

			if (flTestBucket < flBucketCap)
				flTestBucket = std::min(flTestBucket + flBaseDamage, flBucketCap);

			iNextCrit++;
		}
	}

	m_flDamage = flBaseDamage;
	m_flCost = flCost * flMult;
	m_iPotentialCrits = iPotentialCrits;
	m_iAvailableCrits = iAvailableCrits;
	m_iNextCrit = iNextCrit;
}

void CCritHack::UpdateInfo(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	UpdateWeaponInfo(pLocal, pWeapon);

	m_bCritBanned = false;
	m_flDamageTilFlip = 0;
	if (!m_bMelee)
	{
		const float flNormalizedDamage = m_iCritDamage / TF_DAMAGE_CRIT_MULTIPLIER;
		float flCritChance = m_flCritChance + 0.1f;
		if (m_iRangedDamage && m_iCritDamage)
		{
			const float flObservedCritChance = flNormalizedDamage / (flNormalizedDamage + m_iRangedDamage - m_iCritDamage);
			m_bCritBanned = flObservedCritChance > flCritChance;
		}

		if (m_bCritBanned)
			m_flDamageTilFlip = flNormalizedDamage / flCritChance + flNormalizedDamage * 2 - m_iRangedDamage;
		else
			m_flDamageTilFlip = TF_DAMAGE_CRIT_MULTIPLIER * (flNormalizedDamage - flCritChance * (flNormalizedDamage + m_iRangedDamage - m_iCritDamage)) / (flCritChance - 1);
	}

	if (auto pResource = H::Entities.GetResource())
	{
		m_iResourceDamage = pResource->m_iDamage(I::EngineClient->GetLocalPlayer());
		/* // more of a proof of concept for resyncing crit damage
		if (m_flLastDamageTime < I::GlobalVars->curtime + STATS_SEND_FREQUENCY * 2)
		{	// attempt to resync damages
			m_iRangedDamage = m_iResourceDamage - m_iMeleeDamage;

			float flObservedCritChance = pWeapon->m_flObservedCritChance();
			m_iCritDamage = (TF_DAMAGE_CRIT_MULTIPLIER * flObservedCritChance * m_iResourceDamage) / (1 + 2 * flObservedCritChance);
			SDK::Output("Info", std::format("{}, {}", m_iRangedDamage, m_iCritDamage).c_str());
		}
		*/
		m_iDesyncDamage = m_iRangedDamage + m_iMeleeDamage - m_iResourceDamage;
	}
}

bool CCritHack::WeaponCanCrit(CTFWeaponBase* pWeapon, bool bWeaponOnly)
{
	if (!bWeaponOnly && !pWeapon->AreRandomCritsEnabled() || SDK::AttribHookValue(1.f, "mult_crit_chance", pWeapon) <= 0.f)
		return false;

	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_PDA:
	case TF_WEAPON_PDA_ENGINEER_BUILD:
	case TF_WEAPON_PDA_ENGINEER_DESTROY:
	case TF_WEAPON_PDA_SPY:
	case TF_WEAPON_PDA_SPY_BUILD:
	case TF_WEAPON_BUILDER:
	case TF_WEAPON_INVIS:
	case TF_WEAPON_JAR_MILK:
	case TF_WEAPON_LUNCHBOX:
	case TF_WEAPON_BUFF_ITEM:
	case TF_WEAPON_FLAME_BALL:
	case TF_WEAPON_ROCKETPACK:
	case TF_WEAPON_JAR_GAS:
	case TF_WEAPON_LASER_POINTER:
	case TF_WEAPON_MEDIGUN:
	case TF_WEAPON_SNIPERRIFLE:
	case TF_WEAPON_SNIPERRIFLE_DECAP:
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
	case TF_WEAPON_COMPOUND_BOW:
	case TF_WEAPON_JAR:
	case TF_WEAPON_KNIFE:
	case TF_WEAPON_PASSTIME_GUN:
		return false;
	}

	return true;
}

void CCritHack::Reset()
{
	m_iCritDamage = 0;
	m_iRangedDamage = 0;
	m_iMeleeDamage = 0;
	m_iResourceDamage = 0;
	m_iDesyncDamage = 0;

	m_bCritBanned = false;
	m_flDamageTilFlip = 0;

	m_mHealthHistory.clear();

	//m_flLastDamageTime = 0.f;
}



int CCritHack::GetCritRequest(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	bool bCanCrit = m_iAvailableCrits > 0 && !m_bCritBanned;

	bool bForce = bCanCrit && Vars::CritHack::ForceCrits.Value;
	if (bCanCrit && m_bMelee && Vars::CritHack::AlwaysMeleeCrit.Value
		&& (Vars::Aimbot::General::AutoShoot.Value ? pCmd->buttons & IN_ATTACK && !(G::OriginalCmd.buttons & IN_ATTACK) : Vars::Aimbot::General::AimType.Value)
		&& G::AimTarget.m_iEntIndex)
	{
		auto pEntity = I::ClientEntityList->GetClientEntity(G::AimTarget.m_iEntIndex)->As<CBaseEntity>();
		if (pEntity && pEntity->IsPlayer() && (SDK::FriendlyFire() || pLocal->m_iTeamNum() != pEntity->m_iTeamNum()))
			bForce = true;
	}
	
	bool bSkip = Vars::CritHack::AvoidRandomCrits.Value;
	bool bDesync = CommandToSeed(pCmd->command_number) == pWeapon->m_iCurrentSeed();

	return bForce ? CritRequestEnum::Crit : bSkip || bDesync ? CritRequestEnum::Skip : CritRequestEnum::Any;
}

void CCritHack::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!pWeapon || !pLocal->IsAlive() || pLocal->IsAGhost() || !I::EngineClient->IsInGame())
		return;

	UpdateInfo(pLocal, pWeapon);
	if (pLocal->IsCritBoosted() || pWeapon->m_flCritTime() > I::GlobalVars->curtime || !WeaponCanCrit(pWeapon))
		return;

	if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN && pCmd->buttons & IN_ATTACK)
		pCmd->buttons &= ~IN_ATTACK2;
	
	bool bAttacking = G::Attacking /*== 1*/ || F::Ticks.m_bDoubletap || F::Ticks.m_bSpeedhack;
	if (m_bMelee)
	{
		bAttacking = G::CanPrimaryAttack && pCmd->buttons & IN_ATTACK;
		if (!bAttacking && pWeapon->GetWeaponID() == TF_WEAPON_FISTS)
			bAttacking = G::CanPrimaryAttack && pCmd->buttons & IN_ATTACK2;
	}
	else if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN && !(G::LastUserCmd->buttons & IN_ATTACK))
		bAttacking = false;
	else if (!bAttacking)
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_ROCKETLAUNCHER:
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
			if (pWeapon->IsInReload() && G::CanPrimaryAttack && SDK::AttribHookValue(0, "can_overload", pWeapon))
			{
				int iClip1 = pWeapon->m_iClip1();
				if (pWeapon->m_bRemoveable() && iClip1 > 0)
					bAttacking = true;
				else if (iClip1 >= pWeapon->GetMaxClip1() || iClip1 > 0 && pLocal->GetAmmoCount(pWeapon->m_iPrimaryAmmoType()) == 0)
					bAttacking = true;
			}
		}
	}
	if (!bAttacking || pWeapon->IsRapidFire() && I::GlobalVars->curtime < pWeapon->m_flLastRapidFireCritCheckTime() + 1.f)
		return;

	int iRequest = GetCritRequest(pCmd, pLocal, pWeapon);
	if (iRequest == CritRequestEnum::Any)
		return;

	if (!F::AntiCheatCompatibility.Active())
	{
		if (int iCommand = GetCritCommand(pWeapon, pCmd->command_number, iRequest == CritRequestEnum::Crit))
		{
			pCmd->command_number = iCommand;
			pCmd->random_seed = MD5_PseudoRandom(iCommand) & std::numeric_limits<int>::max();
		}
	}
	else if (Vars::Misc::Game::AntiCheatCritHack.Value)
	{
		if (!IsCritCommand(pCmd->command_number, pWeapon, iRequest == CritRequestEnum::Crit, false))
		{
			pCmd->buttons &= ~IN_ATTACK;
			pCmd->viewangles = G::OriginalCmd.viewangles;
			G::PSilentAngles = false;
		}
	}
}

int CCritHack::PredictCmdNum(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	auto fGetCmdNum = [&](int iCommandNumber)
	{
		if (!pWeapon || !pLocal->IsAlive() || !I::EngineClient->IsInGame() || F::AntiCheatCompatibility.Active()
			|| pLocal->IsCritBoosted() || pWeapon->m_flCritTime() > I::GlobalVars->curtime || !WeaponCanCrit(pWeapon))
			return iCommandNumber;

		UpdateInfo(pLocal, pWeapon);
		if (pWeapon->IsRapidFire() && I::GlobalVars->curtime < pWeapon->m_flLastRapidFireCritCheckTime() + 1.f)
			return iCommandNumber;

		int iRequest = GetCritRequest(pCmd, pLocal, pWeapon);
		if (iRequest == CritRequestEnum::Any)
			return iCommandNumber;

		if (int iCommand = GetCritCommand(pWeapon, iCommandNumber, iRequest == CritRequestEnum::Crit))
			return iCommand;
		return iCommandNumber;
	};

	static int iCommandNumber = 0; // cache, don't constantly test

	static int iStaticCommand = 0;
	static CTFWeaponBase* pStaticWeapon = nullptr;
	if (pCmd->command_number != iStaticCommand || pWeapon != pStaticWeapon)
	{
		iCommandNumber = fGetCmdNum(pCmd->command_number);
		iStaticCommand = pCmd->command_number;
		pStaticWeapon = pWeapon;
	}

	return iCommandNumber;
}

bool CCritHack::ShouldForceEffects(CTFPlayer* pLocal)
{
	if (!Vars::CritHack::CritEffects.Value || !pLocal->IsAlive() || pLocal->IsAGhost() || pLocal->IsCritBoosted())
		return false;

	auto pWeapon = H::Entities.GetWeapon();
	if (!pWeapon || !WeaponCanCrit(pWeapon))
		return false;

	float flTickBase = TICKS_TO_TIME(pLocal->m_nTickBase());
	return Vars::CritHack::ForceCrits.Value && !m_bCritBanned && m_iAvailableCrits > 0 || pWeapon->m_flCritTime() > flTickBase;
}

void CCritHack::Event(IGameEvent* pEvent, uint32_t uHash, CTFPlayer* pLocal)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("player_hurt"):
	{
		if (!pLocal)
			return;

		int iVictim = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid"));
		int iAttacker = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("attacker"));
		bool bCrit = pEvent->GetBool("crit") || pEvent->GetBool("minicrit");
		int iDamage = pEvent->GetInt("damageamount");
		int iHealth = pEvent->GetInt("health");
		int iWeaponID = pEvent->GetInt("weaponid");

		if (m_mHealthHistory.contains(iVictim))
		{
			auto& tHistory = m_mHealthHistory[iVictim];
			auto pVictim = I::ClientEntityList->GetClientEntity(iVictim)->As<CTFPlayer>();

			if (!iHealth)
			{
				iDamage = std::clamp(iDamage, 0, tHistory.m_iNewHealth);
				tHistory.m_iSpawnCounter = -1;
			}
			else if (pVictim && (pVictim->m_bFeignDeathReady() || pVictim->InCond(TF_COND_FEIGN_DEATH))) // damage number is spoofed upon sending, correct it
			{
				int iOldHealth = (tHistory.m_mHistory.contains(iHealth % 32768) ? tHistory.m_mHistory[iHealth % 32768].m_iOldHealth : tHistory.m_iNewHealth) % 32768;
				if (iHealth > iOldHealth)
				{
					for (auto& tOldHealth : tHistory.m_mHistory | std::views::values)
					{
						int iOldHealth2 = tOldHealth.m_iOldHealth % 32768;
						if (iOldHealth2 > iHealth)
							iOldHealth = iHealth > iOldHealth ? iOldHealth2 : std::min(iOldHealth, iOldHealth2);
					}
				}
				iDamage = std::clamp(iOldHealth - iHealth, 0, iDamage);
			}
		}
		if (iHealth)
			StoreHealthHistory(iVictim, iHealth);

		if (iVictim == iAttacker || iAttacker != I::EngineClient->GetLocalPlayer())
			return;

		if (auto pGameRules = I::TFGameRules())
		{
			auto pMatchDesc = pGameRules->GetMatchGroupDescription();
			if (pMatchDesc && pGameRules->m_iRoundState() != GR_STATE_RND_RUNNING)
			{
				switch (pMatchDesc->m_eMatchType)
				{
				case MATCH_TYPE_COMPETITIVE:
				case MATCH_TYPE_CASUAL:
					return;
				}
			}

			const int iInsanePlayerDamage = pGameRules->m_bPlayingMannVsMachine() ? 5000 : 1500;
			if (iDamage > iInsanePlayerDamage)
				return;
		}

		CTFWeaponBase* pWeapon = nullptr;
		for (int i = 0; i < MAX_WEAPONS; i++)
		{
			auto pWeapon2 = pLocal->GetWeaponFromSlot(i);
			if (!pWeapon2 || pWeapon2->GetWeaponID() != iWeaponID)
				continue;

			pWeapon = pWeapon2;
			break;
		}

		if (!pWeapon || pWeapon->GetSlot() != SLOT_MELEE)
		{
			m_iRangedDamage += iDamage;
			if (bCrit && !pLocal->IsCritBoosted())
				m_iCritDamage += iDamage;
		}
		else
			m_iMeleeDamage += iDamage;

		return;
	}
	case FNV1A::Hash32Const("player_spawn"):
	{
		int iIndex = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid"));

		if (m_mHealthHistory.contains(iIndex))
		{
			auto& tHistory = m_mHealthHistory[iIndex];

			tHistory.m_iSpawnCounter = -1;
		}

		return;
	}
	case FNV1A::Hash32Const("scorestats_accumulated_update"):
	case FNV1A::Hash32Const("mvm_reset_stats"):
	{
		m_iRangedDamage = m_iCritDamage = m_iMeleeDamage = 0;
		return;
	}
	case FNV1A::Hash32Const("client_beginconnect"):
	case FNV1A::Hash32Const("client_disconnect"):
	case FNV1A::Hash32Const("game_newmap"):
	{
		Reset();
	}
	}
}

void CCritHack::Store()
{
	for (int n = 1; n <= I::EngineClient->GetMaxClients(); n++)
	{
		auto pPlayer = I::ClientEntityList->GetClientEntity(n)->As<CTFPlayer>();
		if (pPlayer && pPlayer->IsAlive() && !pPlayer->IsAGhost())
			StoreHealthHistory(n, pPlayer->m_iHealth(), pPlayer);
	}
}

void CCritHack::StoreHealthHistory(int iIndex, int iHealth, CTFPlayer* pPlayer)
{
	bool bContains = m_mHealthHistory.contains(iIndex);
	auto& tHistory = m_mHealthHistory[iIndex];

	if (bContains && pPlayer)
	{	// deal with instant respawn damage desync better
		if (pPlayer->IsDormant())
			tHistory.m_iSpawnCounter = -1;
		else if (tHistory.m_iSpawnCounter == -1)
			tHistory.m_iSpawnCounter = pPlayer->m_iSpawnCounter();
		else if (tHistory.m_iSpawnCounter != pPlayer->m_iSpawnCounter())
			return; // wait for spawn
	}

	if (!bContains)
		tHistory = { iHealth, iHealth };
	else if (iHealth != tHistory.m_iNewHealth)
	{
		tHistory.m_iOldHealth = std::max(tHistory.m_iNewHealth, iHealth);
		tHistory.m_iNewHealth = iHealth;
	}

	tHistory.m_mHistory[iHealth % 32768] = { tHistory.m_iOldHealth, float(SDK::PlatFloatTime()) };
	while (tHistory.m_mHistory.size() > 3)
	{
		int iIndex2; float flMin = std::numeric_limits<float>::max();
		for (auto& [i, tStorage] : tHistory.m_mHistory)
		{
			if (tStorage.m_flTime < flMin)
				flMin = tStorage.m_flTime, iIndex2 = i;
		}
		tHistory.m_mHistory.erase(iIndex2);
	}
}

#ifdef SERVER_CRIT_DATA
MAKE_SIGNATURE(CTFGameStats_FindPlayerStats, "server.dll", "4C 8B C1 48 85 D2 75", 0x0);
MAKE_SIGNATURE(UTIL_PlayerByIndex, "server.dll", "48 83 EC ? 8B D1 85 C9 7E ? 48 8B 05", 0x0);

static void* s_pCTFGameStats = nullptr;
MAKE_HOOK(CTFGameStats_FindPlayerStats, S::CTFGameStats_FindPlayerStats(), void*,
	void* rcx, CBasePlayer* pPlayer)
{
	DEBUG_RETURN(CTFGameStats_FindPlayerStats, rcx, pPlayer);

	s_pCTFGameStats = rcx;
	return CALL_ORIGINAL(rcx, pPlayer);
}
#endif

void CCritHack::Draw(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::CritHack) || !I::EngineClient->IsInGame())
		return;

	auto pWeapon = H::Entities.GetWeapon();
	if (!pWeapon || !pLocal->IsAlive() || pLocal->IsAGhost() || !WeaponCanCrit(pWeapon, true))
		return;



	int x = Vars::Menu::CritsDisplay.Value.x;
	int y = Vars::Menu::CritsDisplay.Value.y + 8;
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = fFont.m_nTall + H::Draw.Scale(1);
	y -= nTall;

	EAlign align = ALIGN_TOP;
	if (x <= 100 + H::Draw.Scale(50, Scale_Round))
	{
		x -= H::Draw.Scale(42, Scale_Round);
		align = ALIGN_TOPLEFT;
	}
	else if (x >= H::Draw.m_nScreenW - 100 - H::Draw.Scale(50, Scale_Round))
	{
		x += H::Draw.Scale(42, Scale_Round);
		align = ALIGN_TOPRIGHT;
	}

	const bool enabled = pWeapon->AreRandomCritsEnabled();
	const float tickBase = TICKS_TO_TIME(pLocal->m_nTickBase());
	const bool streaming = CritIndicatorStyle::Streaming(enabled, pLocal->IsCritBoosted(), pWeapon->m_flCritTime(), tickBase);
	std::string status = CritIndicatorStyle::ReserveStatus(m_iAvailableCrits), detailLabel = "NEXT CHARGE";
	std::string detail = std::format("{}{} SHOTS", m_iNextCrit, m_iNextCrit == BUCKET_ATTEMPTS ? "+" : "");
	if (!enabled)
		status = "DISABLED", detailLabel = "RANDOM CRITS", detail = "SERVER DISABLED";
	else if (pLocal->IsCritBoosted())
		status = "BOOSTED";
	else if (streaming)
	{
		status = "STREAMING"; detailLabel = "REMAINING";
		detail = std::format("{:.1f}s", pWeapon->m_flCritTime() - tickBase);
	}
	else if (m_bCritBanned)
	{
		status = "CRITBANNED"; detailLabel = "DAMAGE REQUIRED";
		detail = std::format("{:.0f}", ceilf(m_flDamageTilFlip));
	}
	else if (m_iPotentialCrits <= 0)
		status = "UNAVAILABLE", detail = "--";
	else if (m_iAvailableCrits > 0)
	{
		status = "READY";
		if (pWeapon->IsRapidFire() && tickBase < pWeapon->m_flLastRapidFireCritCheckTime() + 1.f)
		{
			status = "COOLDOWN"; detailLabel = "REMAINING";
			detail = std::format("{:.1f}s", pWeapon->m_flLastRapidFireCritCheckTime() + 1.f - tickBase);
		}
	}
	if (detailLabel == "NEXT CHARGE" && m_iNextCrit <= 0)
		detail = "--";

	// Keep uncommon diagnostic information separate from the four primary rows.
	std::vector<std::pair<std::string, std::string>> extras;
	if (enabled && F::AntiCheatCompatibility.Active())
		extras.emplace_back("COMPATIBILITY", "ACTIVE");
	if (enabled && m_flDamageTilFlip && !m_bCritBanned)
		extras.emplace_back("DAMAGE", std::format("{:.0f}", floorf(m_flDamageTilFlip)));
	if (enabled && m_iDesyncDamage)
		extras.emplace_back("DESYNC", std::format("{:+}", m_iDesyncDamage));
	if (MoonlitHud::Enabled())
	{
		auto rows = extras;
		rows.insert(rows.begin(), {detailLabel, detail});
		const auto pos = Vars::Menu::CritsDisplay.Value;
		const auto count = enabled ? std::format("{:02}{} / {:02}", std::max(0, m_iAvailableCrits),
			m_iAvailableCrits == BUCKET_ATTEMPTS ? "+" : "", std::max(0, m_iPotentialCrits)) : "-- / --";
		m_vIndicatorSize = MoonlitHud::Meter(pos.x, pos.y, "crit reserve", status, count, "stored",
			CritIndicatorStyle::BarCharge(CritIndicatorStyle::Charge(m_iAvailableCrits, m_iPotentialCrits, enabled), streaming), MoonlitHud::Gold, rows);
		return;
	}

	const auto& labelFont = H::Fonts.GetFont(FONT_CRIT_LABEL);
	const auto& countFont = H::Fonts.GetFont(FONT_CRIT_COUNT);
	const int pad = std::max(3, int(H::Draw.Scale(6, Scale_Round)));
	const int gap = std::max(2, int(H::Draw.Scale(3, Scale_Round)));
	const int barH = std::max(5, int(H::Draw.Scale(10, Scale_Round)));
	const int lineH = labelFont.m_nTall;
	const std::string count = enabled
		? std::format("{:02}{} / {:02}", std::max(0, m_iAvailableCrits), m_iAvailableCrits == BUCKET_ATTEMPTS ? "+" : "", std::max(0, m_iPotentialCrits))
		: "-- / --";
	int width = int(H::Draw.Scale(196, Scale_Round));
	auto fitRow = [&](const std::string& first, const Font_t& font, const std::string& second)
	{
		width = std::max(width, int(H::Draw.GetTextSize(first.c_str(), font).x +
			H::Draw.GetTextSize(second.c_str(), labelFont).x) + pad * 2 + gap * 3);
	};
	fitRow("CRIT", labelFont, status);
	fitRow(count, countFont, "STORED");
	fitRow(detailLabel, labelFont, detail);
	for (const auto& [label, value] : extras) fitRow(label, labelFont, value);
	const int height = pad * 2 + lineH * 2 + countFont.m_nTall + barH + gap * 3
		+ int(extras.size()) * (lineH + gap);
	m_vIndicatorSize = { float(width), float(height) };
	const int left = int(BindLayout::ClampAxis(float(Vars::Menu::CritsDisplay.Value.x - width / 2), float(width), 0.f, float(H::Draw.m_nScreenW)));
	const float taskbar = F::Menu.m_bIsOpen ? H::Draw.Scale(26) : 0.f;
	const int top = int(BindLayout::ClampAxis(float(Vars::Menu::CritsDisplay.Value.y), float(height),
		Workspace::TopTaskbar ? taskbar : 0.f, H::Draw.m_nScreenH - (Workspace::TopTaskbar ? 0.f : taskbar)));
	const Color_t accent(int(Workspace::Accent[0] * 255), int(Workspace::Accent[1] * 255), int(Workspace::Accent[2] * 255), 255);
	const float fraction = CritIndicatorStyle::Charge(m_iAvailableCrits, m_iPotentialCrits, enabled);
	const bool full = CritIndicatorStyle::Full(m_iAvailableCrits, m_iPotentialCrits, enabled);
	const Color_t textColour(int(Workspace::TextChannel(0) * 255), int(Workspace::TextChannel(1) * 255), int(Workspace::TextChannel(2) * 255), 255);
	Workspace::ScopedTextColour preserveTextColour;
	const Color_t rightColour = textColour.Lerp({ 0, 0, 0, 255 }, 1.f - CritIndicatorStyle::LabelBrightness(fraction));
	auto drawText = [&](const Font_t& font, int textX, int textY, EAlign alignment, const char* text, bool chargeTint, bool streamGlow = false)
	{
		Color_t colour = chargeTint ? rightColour : textColour;
		if (streamGlow)
		{
			colour = textColour.Lerp({ 255, 255, 255, 255 }, .20f);
			// Stronger than the reserve halo, but keep the crisp glyphs readable.
			for (int ring = 2; ring >= 1; --ring)
			{
				Color_t halo = colour; halo.a = ring == 2 ? 20 : 42;
				const int offset = ring * std::max(1, int(std::round(H::Draw.Scale())));
				for (int dx = -1; dx <= 1; ++dx)
					for (int dy = -1; dy <= 1; ++dy)
						if (dx || dy)
							H::Draw.String(font, textX + dx * offset, textY + dy * offset, halo, alignment, text);
			}
		}
		// Match the ticks HUD: four low-alpha samples and a 5% highlight at full.
		// Left-hand labels and the reserve count never receive charge effects.
		else if (chargeTint && full)
		{
			colour = textColour.Lerp({ 255, 255, 255, 255 }, .05f);
			Color_t halo = colour; halo.a = 10;
			const int offset = std::max(1, int(std::round(std::max(.1f, H::Draw.Scale()))));
			H::Draw.String(font, textX - offset, textY, halo, alignment, text);
			H::Draw.String(font, textX + offset, textY, halo, alignment, text);
			H::Draw.String(font, textX, textY - offset, halo, alignment, text);
			H::Draw.String(font, textX, textY + offset, halo, alignment, text);
		}
		H::Draw.String(font, textX, textY, colour, alignment, text);
	};
	H::Draw.FillRect(left, top, width, height, { 0, 0, 0, 255 });
	H::Draw.LineRect(left, top, width, height, Color_t(int(Workspace::BorderChannel(0) * 255), int(Workspace::BorderChannel(1) * 255), int(Workspace::BorderChannel(2) * 255), 255));
	auto row = [&](int rowY, const std::string& label, const std::string& value, bool streamGlow = false)
	{
		drawText(labelFont, left + pad, rowY, ALIGN_TOPLEFT, label.c_str(), false);
		drawText(labelFont, left + width - pad, rowY, ALIGN_TOPRIGHT, value.c_str(), true, streamGlow);
	};
	int rowY = top + pad;
	row(rowY, "CRIT", status, streaming);
	rowY += lineH + gap;
	drawText(countFont, left + pad, rowY, ALIGN_TOPLEFT, count.c_str(), false);
	drawText(labelFont, left + width - pad, rowY + countFont.m_nTall - lineH, ALIGN_TOPRIGHT, "STORED", true);
	rowY += countFont.m_nTall + gap;
	const int barX = left + pad, barW = width - pad * 2;
	const auto layout = NotificationStyle::Layout(float(barW), std::max(0.1f, H::Draw.Scale()));
	const int filled = NotificationStyle::Filled(CritIndicatorStyle::BarCharge(fraction, streaming), layout.count);
	if (streaming)
	{
		// Bound the halo by the available gap so it cannot cover adjacent text.
		for (int ring = 2; ring >= 1; --ring)
		{
			const int spread = std::min(gap - 1, ring * std::max(1, int(std::round(H::Draw.Scale()))));
			Color_t halo = accent; halo.a = ring == 2 ? 24 : 48;
			H::Draw.LineRect(barX - spread, rowY - spread, barW + spread * 2, barH + spread * 2, halo);
		}
	}
	for (int i = 0; i < layout.count; ++i)
	{
		const int cellX = barX + int(layout.Left(i));
		const int cellW = std::max(1, int(layout.Right(i)) - int(layout.Left(i)));
		if (i < filled)
			H::Draw.GradientRect(cellX, rowY, cellW, barH,
				accent.Lerp({ 255, 255, 255, 255 }, streaming ? .30f : .15f), accent.Lerp({ 0, 0, 0, 255 }, streaming ? .15f : .45f), false);
		else
			H::Draw.FillRect(cellX, rowY, cellW, barH, { 35, 35, 35, 255 });
	}
	rowY += barH + gap;
	row(rowY, detailLabel, detail);
	for (const auto& [label, value] : extras)
	{
		rowY += lineH + gap;
		row(rowY, label, value);
	}
	y = top + height;
	if (!enabled) return;



	if (Vars::Debug::Info.Value)
	{
		H::Draw.StringOutlined(fFont, x, y += nTall * 2, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, std::format("RangedDamage: {}, CritDamage: {}", m_iRangedDamage, m_iCritDamage).c_str());

#ifdef SERVER_CRIT_DATA
		H::Draw.StringOutlined(fFont, x, y += nTall, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, std::format("AllDamage: {} ({})", m_iRangedDamage + m_iMeleeDamage, m_iMeleeDamage).c_str());

		if (s_pCTFGameStats)
		{
			if (auto pPlayer2 = S::UTIL_PlayerByIndex.Call<void*>(I::EngineClient->GetLocalPlayer()))
			{
				if (auto pPlayerStats = S::CTFGameStats_FindPlayerStats.Call<PlayerStats_t*>(s_pCTFGameStats, pPlayer2))
				{
					int& iRangedDamage = pPlayerStats->statsCurrentRound.m_iStat[TFSTAT_DAMAGE_RANGED];
					int& iCritDamage = pPlayerStats->statsCurrentRound.m_iStat[TFSTAT_DAMAGE_RANGED_CRIT_RANDOM];
					int& iDamage = pPlayerStats->statsCurrentRound.m_iStat[TFSTAT_DAMAGE];

					//iRangedDamage = m_iRangedDamage;
					//iCritDamage = m_iCritDamage = 0;
					//iDamage = m_iRangedDamage + m_iMeleeDamage;

					H::Draw.StringOutlined(fFont, x, y += nTall, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, std::format("RangedDamage: {}, CritDamage: {}", iRangedDamage, iCritDamage).c_str());
					H::Draw.StringOutlined(fFont, x, y += nTall, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, std::format("AllDamage: {} ({})", iDamage, iDamage - iRangedDamage).c_str());
				}
			}
		}

		H::Draw.StringOutlined(fFont, x, y += nTall, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, std::format("ResourceDamage: {} ({})", m_iResourceDamage, m_iMeleeDamage).c_str());
#endif

		H::Draw.StringOutlined(fFont, x, y += nTall * 2, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, std::format("Bucket: {}, Shots: {}, Crits: {}", pWeapon->m_flCritTokenBucket(), pWeapon->m_nCritChecks(), pWeapon->m_nCritSeedRequests()).c_str());
		H::Draw.StringOutlined(fFont, x, y += nTall, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, std::format("Damage: {}, Cost: {}", m_flDamage, m_flCost).c_str());
		H::Draw.StringOutlined(fFont, x, y += nTall, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, std::format("CritChance: {:.2f} ({:.2f})", m_flCritChance, m_flCritChance + 0.1f).c_str());
	}
}
