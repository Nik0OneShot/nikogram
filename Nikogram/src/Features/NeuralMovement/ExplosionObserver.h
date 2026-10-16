#pragma once
#include <array>

namespace NeuralMovement::ExplosionObserver
{
    struct Event
    {
        std::array<float, 3> origin{};
        int weaponID = 0;
        int itemDefinition = -1;
    };
    // Called during packet parsing. Enqueue the payload; correlate other entity
    // state only after NET_UPDATE_END, when that snapshot is complete.
    using Callback = void (*)(const Event&) noexcept;

    // Call once on the game frame, after global MinHook initialization. Optional:
    // failure disables this observer without affecting client startup.
    bool Initialize(Callback callback) noexcept;
    bool Available() noexcept;
    const char* Status() noexcept;
    // Stops delivery only. Global Hooks::Unload disables, drains and removes the
    // hook. Never individually remove/reinstall a hook while callbacks may run.
    void Shutdown() noexcept;
}
