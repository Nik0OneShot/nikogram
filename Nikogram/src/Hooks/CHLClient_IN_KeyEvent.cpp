#include "../SDK/SDK.h"
#include "../Features/Visuals/Visuals.h"

MAKE_HOOK(CHLClient_IN_KeyEvent, U::Memory.GetVirtual(I::Client, 20), int,
    void* rcx, int eventcode, ButtonCode_t keynum, const char* binding)
{
    DEBUG_RETURN(CHLClient_IN_KeyEvent, rcx, eventcode, keynum, binding);
    if (keynum == MOUSE_WHEEL_UP || keynum == MOUSE_WHEEL_DOWN)
    {
        static OptifineZoom::WheelButton ownership[2];
        const bool down = eventcode > 0;
        const int index = keynum == MOUSE_WHEEL_UP ? 0 : 1;
        const bool captured = down && !G::Unload && F::Visuals.OptifineWheel(index == 0 ? 1 : -1);
        // Releases follow their press, so a forwarded +command is never stuck.
        if (ownership[index].Event(down, captured)) return 0;
    }
    return CALL_ORIGINAL(rcx, eventcode, keynum, binding);
}
