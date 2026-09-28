#include "../SDK/SDK.h"
#include "../Features/Simulation/MovementSimulation/MovementSimulation.h"

// Observe the actual engine host so movesim can restore it, including nested calls.
MAKE_HOOK(IMoveHelper_SetHost, U::Memory.GetVirtual(I::MoveHelper, 12), void,
    void* rcx, CBasePlayer* host)
{
    MoveSimulationHost::Current = host;
    CALL_ORIGINAL(rcx, host);
}
