#include "../SDK/SDK.h"

#include "../Features/EnginePrediction/EnginePrediction.h"
#include "../Features/Ticks/Ticks.h"
#include "../Features/Ticks/TickbaseCorrectionPolicy.h"
#include "../Features/Triggerbot/Triggerbot.h"

MAKE_SIGNATURE(CPrediction_RunSimulation, "client.dll", "48 83 EC 38 4C 8B 44", 0x0);

static_assert(TickbaseCorrectionPolicy::Capacity == MULTIPLAYER_BACKUP);

MAKE_HOOK(CPrediction_RunSimulation, S::CPrediction_RunSimulation(), void,
	void* rcx, int current_command, float curtime, CUserCmd* cmd, CTFPlayer* localPlayer)
{
	DEBUG_RETURN(CPrediction_RunSimulation, rcx, current_command, curtime, cmd, localPlayer);

    auto& corrections = TickbaseCorrectionPolicy::queue;
    const auto channel = I::EngineClient->GetNetChannelInfo();
    const bool valid = localPlayer && cmd && I::Input && I::Input->m_pCommands && channel
        && localPlayer->IsAlive()
        && I::ClientEntityList->GetClientEntityFromHandle(localPlayer->GetRefEHandle()) == localPlayer;
    TickbaseCorrectionPolicy::Context context{};
    if (valid)
        context = {reinterpret_cast<std::uintptr_t>(localPlayer), reinterpret_cast<std::uintptr_t>(channel),
            reinterpret_cast<std::uintptr_t>(I::Input->m_pCommands), localPlayer->GetRefEHandle().ToInt(),
            localPlayer->m_iSpawnCounter(), I::ClientState->m_nServerCount};
    const std::int64_t newest = std::int64_t(I::ClientState->lastoutgoingcommand) + I::ClientState->chokedcommands;
    const bool tracking = corrections.Observe(context, I::ClientState->last_command_ack,
        I::ClientState->lastoutgoingcommand,
        newest >= 0 && newest <= std::numeric_limits<int>::max() ? int(newest) : -1, valid);

	if (F::Ticks.m_bShifting && F::Ticks.m_iShiftedTicks + 1 == F::Ticks.m_iShiftStart)
	{
        // UpdateInfo has not run for the new command yet. OriginalCmd identifies
        // the prior input sequence; its committed slot may contain a Crit rewrite.
        const int sequence = G::OriginalCmd.command_number;
        const std::int64_t shift = std::int64_t(F::Ticks.m_iShiftStart) - F::Ticks.m_iShiftedGoal;
        if (tracking && G::CurrentUserCmd && I::Input->CommandSlot(sequence) == G::CurrentUserCmd
            && shift > 0 && shift <= std::numeric_limits<int>::max())
            corrections.Register(reinterpret_cast<std::uintptr_t>(G::CurrentUserCmd), sequence,
                G::CurrentUserCmd->command_number, int(shift), F::Ticks.m_iMaxUsrCmdProcessTicks);
		F::Ticks.m_bShifting = false;
	}

    const int shift = tracking ? corrections.Correction(reinterpret_cast<std::uintptr_t>(cmd),
        current_command, cmd->command_number) : 0;
    // Re-prediction restores native state before replay; the same valid command
    // still needs its correction on each pass. Never underflow a reset tickbase.
    if (shift > 0 && localPlayer->m_nTickBase() >= shift)
        localPlayer->m_nTickBase() -= shift;

	F::EnginePrediction.AdjustPlayers(localPlayer);
	const auto triggerObservation=F::Triggerbot.BeginNative(localPlayer,current_command,cmd);
	CALL_ORIGINAL(rcx, current_command, curtime, cmd, localPlayer);
	F::Triggerbot.EndNative(triggerObservation);
	F::EnginePrediction.RestorePlayers();
}
