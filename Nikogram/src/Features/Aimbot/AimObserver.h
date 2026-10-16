#pragma once
#include "ObserverPolicy.h"
#include "../NeuralMovement/NeuralMovement.h"
class CTFPlayer;class CTFWeaponBase;class CUserCmd;
namespace AimObserver {
using Reason=AimObservation::Reason;
void Begin(CTFPlayer*,CTFWeaponBase*,CUserCmd*);
void Mark(Reason);
void Selected(CTFPlayer*);
void Seal(CTFPlayer*,CTFWeaponBase*,CUserCmd*);
void FinishComparison(CTFWeaponBase*);
void Network();
void Reset();
void DrawHud();
void DrawWorld();
struct Scope {bool previous=false;explicit Scope(bool live);~Scope();};
class Capture {
    bool active=false,ordinary=true,attach=false;
    uint64_t epoch=0;
    AimObservation::Cohort row;

public:
    Capture(CTFPlayer*,CTFWeaponBase*,bool eligible,bool ordinary=true);
    ~Capture();
    void Point(double time,float x,float y,float z,bool valid);
};
}
