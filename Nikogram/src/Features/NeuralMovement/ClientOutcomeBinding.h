#pragma once
#include <cstdint>
class CTFPlayer;class CTFWeaponBase;class IGameEvent;
namespace movement::trajectory {struct Outcome;}
namespace NeuralMovement::ClientOutcomes {
void Frame() noexcept;
void Network() noexcept;
void NativeFire(CTFPlayer*,CTFWeaponBase*,int command,bool selected,uint32_t target,const movement::trajectory::Outcome&) noexcept;
void Event(IGameEvent*) noexcept;
void Reset() noexcept;
void Shutdown() noexcept;
bool ObserverAvailable() noexcept;
}
