#pragma once
#include "../../Definitions/Interfaces/IEngineTrace.h"
#include "../../Definitions/Definitions.h"

class CBaseEntity;

enum
{
	SKIP_CHECK,
	FORCE_PASS,
	FORCE_HIT
};

enum
{
	WEAPON_INCLUDE,
	WEAPON_EXCLUDE
};
enum
{
	PLAYER_DEFAULT,
	PLAYER_NONE,
	PLAYER_ALL
};
enum
{
	OBJECT_DEFAULT,
	OBJECT_NONE,
	OBJECT_ALL
};

class CTraceFilterHitscan : public ITraceFilter
{
public:
	bool ShouldHitEntity(IHandleEntity* pHandleEntity, int nContentsMask) override;
	TraceType_t GetTraceType() const override;
	CBaseEntity* pSkip = nullptr;

	int iTeam = -1;
	static constexpr int aWeapons[] = { TF_WEAPON_SNIPERRIFLE, TF_WEAPON_SNIPERRIFLE_CLASSIC, TF_WEAPON_SNIPERRIFLE_DECAP };
	bool bWeaponChecked = false;
	int iType = FORCE_HIT;
	int iWeapon = WEAPON_EXCLUDE;
	bool bWeapon = false;
};

class CTraceFilterCollideable : public ITraceFilter
{
public:
	bool ShouldHitEntity(IHandleEntity* pHandleEntity, int nContentsMask) override;
	TraceType_t GetTraceType() const override;
	CBaseEntity* pSkip = nullptr;

	int iTeam = -1;
	static constexpr int aWeapons[] = { TF_WEAPON_CROSSBOW, TF_WEAPON_LUNCHBOX };
	bool bWeaponChecked = false;
	int iType = FORCE_HIT;
	int iWeapon = WEAPON_INCLUDE;
	bool bWeapon = false;
	int iPlayer = PLAYER_DEFAULT;
	int iObject = OBJECT_ALL;
	bool bMisc = false;
};

class CTraceFilterWorldAndPropsOnly : public ITraceFilter
{
public:
	bool ShouldHitEntity(IHandleEntity* pHandleEntity, int nContentsMask) override;
	TraceType_t GetTraceType() const override;
	CBaseEntity* pSkip = nullptr;

	int iTeam = -1;
	int iSkipStaticProp = -1; // static prop entry index to ignore, -1 for none
};