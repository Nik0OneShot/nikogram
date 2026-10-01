#include "../SDK/SDK.h"
#include "../Features/SkinChanger/SkinChanger.h"
#include "../Features/SkinChanger/RenderPolicy.h"
#include <atomic>

MAKE_SIGNATURE(CSoundEmitterSystem_EmitSound, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 41 56 48 81 EC ? ? ? ? 49 8B D9", 0x0);
//MAKE_SIGNATURE(S_StartDynamicSound, "engine.dll", "4C 8B DC 57 48 81 EC", 0x0);
MAKE_SIGNATURE(S_StartSound, "engine.dll", "40 53 48 83 EC ? 48 83 79 ? ? 48 8B D9 75 ? 33 C0", 0x0);
MAKE_SIGNATURE(CBaseEntity_EmitSound, "client.dll", "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC ? ? ? ? 48 8B 3D", 0x0);

class IRecipientFilter
{
public:
	virtual			~IRecipientFilter() {}

	virtual bool	IsReliable(void) const = 0;
	virtual bool	IsInitMessage(void) const = 0;

	virtual int		GetRecipientCount(void) const = 0;
	virtual int		GetRecipientIndex(int slot) const = 0;
};

struct CSoundParameters
{
	CSoundParameters()
	{
		channel = 0; // 0
		volume = 1.0f;  // 1.0f
		pitch = 100; // 100

		pitchlow = 100;
		pitchhigh = 100;

		soundlevel = SNDLVL_NORM; // 75dB
		soundname[0] = 0;
		play_to_owner_only = false;
		count = 0;

		delay_msec = 0;
	}

	int				channel;
	float			volume;
	int				pitch;
	int				pitchlow, pitchhigh;
	soundlevel_t	soundlevel;
	// For weapon sounds...
	bool			play_to_owner_only;
	int				count;
	char 			soundname[128];
	int				delay_msec;
};

struct EmitSound_t
{
	EmitSound_t() :
		m_nChannel(0),
		m_pSoundName(0),
		m_flVolume(1.0f),
		m_SoundLevel(0),
		m_nFlags(0),
		m_nPitch(100),
		m_nSpecialDSP(0),
		m_pOrigin(0),
		m_flSoundTime(0.0f),
		m_pflSoundDuration(0),
		m_bEmitCloseCaption(true),
		m_bWarnOnMissingCloseCaption(false),
		m_bWarnOnDirectWaveReference(false),
		m_nSpeakerEntity(-1),
		m_UtlVecSoundOrigin(),
		m_hSoundScriptHandle(-1)
	{
	}

	EmitSound_t(const CSoundParameters& src);

	int m_nChannel;
	char const* m_pSoundName;
	float m_flVolume;
	int m_SoundLevel;
	int m_nFlags;
	int m_nPitch;
	int m_nSpecialDSP;
	const Vector* m_pOrigin;
	float m_flSoundTime; ///< NOT DURATION, but rather, some absolute time in the future until which this sound should be delayed
	float* m_pflSoundDuration;
	bool m_bEmitCloseCaption;
	bool m_bWarnOnMissingCloseCaption;
	bool m_bWarnOnDirectWaveReference;
	int m_nSpeakerEntity;
	mutable CUtlVector<Vector> m_UtlVecSoundOrigin;  ///< Actual sound origin(s) (can be multiple if sound routed through speaker entity(ies) )
	mutable short m_hSoundScriptHandle;
};

const static std::vector<const char*> s_vFootsteps = { "footstep", "flesh_impact_hard", "body_medium_impact_soft", "ceiling_tile_step", "glass_sheet_step", "rubber_tire_impact_soft", "plastic_box_impact_soft", "plastic_barrel_impact_soft", "cardboard_box_impact_soft", "glass_impact_soft" };
const static std::vector<const char*> s_vNoisemaker = { "items\\halloween", "items\\football_manager", "items\\japan_fundraiser", "items\\samurai\\tf_samurai_noisemaker", "items\\summer", "misc\\happy_birthday_tf", "misc\\jingle_bells" };
const static std::vector<const char*> s_vFryingPan = { "pan_" };
const static std::vector<const char*> s_vWater = { "ambient_mp3\\water\\water_splash", "slosh", "wade" };

static std::atomic<void*> s_cosmeticSoundEmitter=nullptr;
static thread_local bool s_replayingCosmeticSound=false;
static thread_local int s_cosmeticSoundGuid=0;
class CosmeticSoundFilter final:public IRecipientFilter
{
public:
    bool IsReliable()const override{return false;}
    bool IsInitMessage()const override{return false;}
    int GetRecipientCount()const override{return 1;}
    int GetRecipientIndex(int)const override{return I::EngineClient->GetLocalPlayer();}
};

static inline bool ShouldBlockSound(const char* pSound)
{
	if (!Vars::Misc::Sound::Block.Value || !pSound)
		return false;

	std::string sSound = pSound;
	std::transform(sSound.begin(), sSound.end(), sSound.begin(), ::tolower);
	auto fCheckSound = [&](const std::vector<const char*>& vSounds, int iFlag = -1)
	{
		if (Vars::Misc::Sound::Block.Value & iFlag)
		{
			for (auto& sNoise : vSounds)
			{
				if (sSound.find(sNoise) != std::string::npos)
					return true;
			}
		}
		return false;
	};

	if (fCheckSound(s_vFootsteps, Vars::Misc::Sound::BlockEnum::Footsteps))
		return true;

	if (fCheckSound(s_vNoisemaker, Vars::Misc::Sound::BlockEnum::Noisemaker))
		return true;

	if (fCheckSound(s_vFryingPan, Vars::Misc::Sound::BlockEnum::FryingPan))
		return true;

	if (fCheckSound(s_vWater, Vars::Misc::Sound::BlockEnum::Water))
		return true;

	return false;
}

MAKE_HOOK(CSoundEmitterSystem_EmitSound, S::CSoundEmitterSystem_EmitSound(), void,
	void* rcx, IRecipientFilter& filter, int entindex, const EmitSound_t& ep)
{
	DEBUG_RETURN(CSoundEmitterSystem_EmitSound, rcx, filter, entindex, ep);
	s_cosmeticSoundEmitter.store(rcx,std::memory_order_relaxed);

    // Native ragdoll creation can play the sound before RagdollCreated runs.
    // Confirm the engine sound GUID instead of assuming that creation played it.
    if(!s_replayingCosmeticSound&&ep.m_pSoundName&&
        (!strcmp(ep.m_pSoundName,"Saxxy.TurnGold")||!strcmp(ep.m_pSoundName,"Icicle.TurnToIce")))
    {
        if(auto begin=SkinChanger::BeginDeathSound(entindex))
        {
            if(*begin)SkinChanger::PlayCosmeticDeathSound(entindex,!strcmp(ep.m_pSoundName,"Saxxy.TurnGold")?SkinModel::DeathGold:SkinModel::DeathIce);
            return; // Tracked cosmetic deaths share one gate and fixed origin.
        }
        SkinRender::ViewmodelDrawScope replay(s_replayingCosmeticSound,true);s_cosmeticSoundGuid=0;
        if(!ShouldBlockSound(ep.m_pSoundName))CALL_ORIGINAL(rcx,filter,entindex,ep);
        SkinChanger::ObserveDeathSound(entindex,s_cosmeticSoundGuid>0,s_cosmeticSoundGuid,"native_create");return;
    }

	if (ShouldBlockSound(ep.m_pSoundName))
		return;

    const auto cosmetic=SkinChanger::RemoteSoundFor(entindex,ep.m_pSoundName);
    if(!cosmetic.empty()&&!s_replayingCosmeticSound)
    {
        if(ShouldBlockSound(cosmetic.c_str()))return;
        // Don't mutate the caller's const parameters or shallow-copy its
        // CUtlVector. Preserve every scalar and deep-copy bounded origins.
        if(ep.m_UtlVecSoundOrigin.Count()<0||ep.m_UtlVecSoundOrigin.Count()>128)return CALL_ORIGINAL(rcx,filter,entindex,ep);
        EmitSound_t replacement;replacement.m_pSoundName=cosmetic.c_str();
        replacement.m_nChannel=ep.m_nChannel;replacement.m_flVolume=ep.m_flVolume;
        replacement.m_SoundLevel=ep.m_SoundLevel;replacement.m_nFlags=ep.m_nFlags;replacement.m_nPitch=ep.m_nPitch;
        replacement.m_nSpecialDSP=ep.m_nSpecialDSP;replacement.m_pOrigin=ep.m_pOrigin;
        replacement.m_flSoundTime=ep.m_flSoundTime;replacement.m_pflSoundDuration=ep.m_pflSoundDuration;
        replacement.m_bEmitCloseCaption=ep.m_bEmitCloseCaption;replacement.m_bWarnOnMissingCloseCaption=ep.m_bWarnOnMissingCloseCaption;
        replacement.m_bWarnOnDirectWaveReference=ep.m_bWarnOnDirectWaveReference;replacement.m_nSpeakerEntity=ep.m_nSpeakerEntity;
        replacement.m_UtlVecSoundOrigin=ep.m_UtlVecSoundOrigin;
        CALL_ORIGINAL(rcx,filter,entindex,replacement);
        // Native callers may inspect the generated origin list afterward.
        if(replacement.m_UtlVecSoundOrigin.Count()<=128)ep.m_UtlVecSoundOrigin=replacement.m_UtlVecSoundOrigin;
        return;
    }

	return CALL_ORIGINAL(rcx, filter, entindex, ep);
}

/*
MAKE_HOOK(S_StartDynamicSound, S::S_StartDynamicSound(), int,
	StartSoundParams_t& params)
{
	DEBUG_RETURN(S_StartDynamicSound, params);

	H::Entities.ManualNetwork(params);
	if (params.pSfx && ShouldBlockSound(params.pSfx->getname()))
		return 0;

	return CALL_ORIGINAL(params);
}
*/

MAKE_HOOK(S_StartSound, S::S_StartSound(), int,
	StartSoundParams_t& params)
{
	DEBUG_RETURN(S_StartSound, params);

	if (!params.staticsound)
		H::Entities.ManualNetwork(params);
	if (params.pSfx && ShouldBlockSound(params.pSfx->getname()))
		return 0;

    // SND_CHANGE_VOL=1, SND_CHANGE_PITCH=2, SND_STOP=4 in Source's sound flags.
    // Weapon impact scripts (Bat/Knife/Pan/etc.) use CHAN_STATIC too.
    // Static-channel is not proof of an ambient sound: verified peer ownership
    // and an exact authored source-wave match provide that boundary instead.
    if(!s_replayingCosmeticSound&&params.fromserver&&params.pSfx&&!(params.flags&7))
    {
        const auto cosmetic=SkinChanger::RemoteSoundFor(params.soundsource,params.pSfx->getname(),true);
        auto emitter=s_cosmeticSoundEmitter.load(std::memory_order_relaxed);
        if(!cosmetic.empty()&&emitter&&Hooks::CSoundEmitterSystem_EmitSound::Hook.m_pOriginal)
        {
            if(ShouldBlockSound(cosmetic.c_str()))return 0;
            EmitSound_t ep;ep.m_pSoundName=cosmetic.c_str();ep.m_nChannel=params.entchannel;
            ep.m_flVolume=params.fvol;ep.m_SoundLevel=params.soundlevel;ep.m_nFlags=params.flags;
            ep.m_nPitch=params.pitch;ep.m_nSpecialDSP=params.specialdsp;ep.m_pOrigin=&params.origin;
            ep.m_flSoundTime=I::GlobalVars->curtime+params.delay;ep.m_nSpeakerEntity=params.speakerentity;
            CosmeticSoundFilter filter;
            SkinRender::ViewmodelDrawScope replay(s_replayingCosmeticSound,true);s_cosmeticSoundGuid=0;
            Hooks::CSoundEmitterSystem_EmitSound::Hook.As<Hooks::CSoundEmitterSystem_EmitSound::FN>()(emitter,filter,params.soundsource,ep);
            if(s_cosmeticSoundGuid>0){SkinChanger::ObserveCosmeticEffect("sound_replay_started",cosmetic);return s_cosmeticSoundGuid;}
            SkinChanger::ObserveCosmeticEffect("sound_replay_failed_native_preserved",cosmetic);
        }
        else if(!cosmetic.empty())SkinChanger::ObserveCosmeticEffect("sound_emitter_unavailable_native_preserved",cosmetic);
    }
    const int result=CALL_ORIGINAL(params);
    if(s_replayingCosmeticSound&&result>0)s_cosmeticSoundGuid=result;
    return result;
}

MAKE_HOOK(CBaseEntity_EmitSound, S::CBaseEntity_EmitSound(), void,
	void* rcx, const char* soundname, float soundtime, float* duration)
{
	DEBUG_RETURN(CBaseEntity_EmitSound, rcx, soundname, soundtime, duration);

	if (soundname)
	{
		switch (FNV1A::Hash32(soundname))
		{
		case FNV1A::Hash32Const("BumperCar.Jump"):
		case FNV1A::Hash32Const("BumperCar.JumpLand"):
		case FNV1A::Hash32Const("BumperCar.Bump"):
		case FNV1A::Hash32Const("BumperCar.BumpHard"):
			if (I::Prediction->InPrediction() && !I::Prediction->m_bFirstTimePredicted)
				return;
		}
	}

	CALL_ORIGINAL(rcx, soundname, soundtime, duration);
}

bool SkinChanger::PlayKillstreakMilestoneSound()
{
    if(G::Unload||SDK::CleanScreenshot()||!I::EngineClient->IsInGame()||I::EngineClient->IsPlayingDemo())return false;
    auto emitter=s_cosmeticSoundEmitter.load(std::memory_order_relaxed);
    if(!emitter||!Hooks::CSoundEmitterSystem_EmitSound::Hook.m_pOriginal)return false;
    CosmeticSoundFilter filter;EmitSound_t sound;sound.m_pSoundName="Game.KillStreak";
    sound.m_bEmitCloseCaption=false;
    // The native client-only recipient filter and local-player sound source:
    // no event is sent to the server or another client's sound system.
    Hooks::CSoundEmitterSystem_EmitSound::Hook.As<Hooks::CSoundEmitterSystem_EmitSound::FN>()(emitter,filter,-1,sound);
    return true;
}

bool SkinChanger::PlaySapperVoice(int entity,const char* name)
{
    if(!name||(strcmp(name,"PSap.Deploy")&&strcmp(name,"Psap.Idle")&&strcmp(name,"PSap.Holster")&&strcmp(name,"Psap.Attached")&&strcmp(name,"PSap.Hacking"))||G::Unload||SDK::CleanScreenshot()
        ||!I::EngineClient->IsInGame()||I::EngineClient->IsPlayingDemo()||ShouldBlockSound(name))return false;
    auto emitter=s_cosmeticSoundEmitter.load(std::memory_order_relaxed);
    if(!emitter||!Hooks::CSoundEmitterSystem_EmitSound::Hook.m_pOriginal)return false;
    CosmeticSoundFilter filter;EmitSound_t sound;sound.m_pSoundName=name;sound.m_bEmitCloseCaption=false;
    Hooks::CSoundEmitterSystem_EmitSound::Hook.As<Hooks::CSoundEmitterSystem_EmitSound::FN>()(emitter,filter,entity,sound);return true;
}

bool SkinChanger::PlayCosmeticDeathSound(int entity,int effects)
{
    if(G::Unload||SDK::CleanScreenshot()||!I::EngineClient->IsInGame()||I::EngineClient->IsPlayingDemo())return false;
    auto emitter=s_cosmeticSoundEmitter.load(std::memory_order_relaxed);
    if(!emitter||!Hooks::CSoundEmitterSystem_EmitSound::Hook.m_pOriginal)
    {ObserveDeathSound(entity,false,0,"emitter_unavailable");return false;}
    const char* name=effects&SkinModel::DeathGold?"Saxxy.TurnGold":effects&SkinModel::DeathIce?"Icicle.TurnToIce":nullptr;
    if(!name)return false;
    if(ShouldBlockSound(name)){ObserveDeathSound(entity,false,0,"sound_blocked");return false;}
    auto client=I::ClientEntityList->GetClientEntity(entity);auto body=client?client->As<CBaseEntity>():nullptr;
    if(!body||body->GetClassID()!=ETFClassID::CTFRagdoll)return false;
    const Vector origin=SkinChanger::DeathSoundOrigin(entity).value_or(body->GetAbsOrigin());
    CosmeticSoundFilter filter;EmitSound_t sound;sound.m_pSoundName=name;sound.m_bEmitCloseCaption=false;sound.m_pOrigin=&origin;
    SkinRender::ViewmodelDrawScope replay(s_replayingCosmeticSound,true);s_cosmeticSoundGuid=0;
    Hooks::CSoundEmitterSystem_EmitSound::Hook.As<Hooks::CSoundEmitterSystem_EmitSound::FN>()(emitter,filter,entity,sound);
    const bool started=s_cosmeticSoundGuid>0;ObserveDeathSound(entity,started,s_cosmeticSoundGuid,"one_shot_positional");return started;
}
