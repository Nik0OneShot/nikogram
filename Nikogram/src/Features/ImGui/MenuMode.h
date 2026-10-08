#pragma once
namespace MenuMode
{
    enum Style { Nullcore = 0, Moonlit = 1 };
    inline int Saved = Nullcore, Active = Nullcore, Pending = -1;
    inline bool Animations = true, SmoothSliders = true;
    inline bool DrawingMoonlit = false;
    inline float AnimationSpeed = 1.f;
    inline bool Valid(int value) { return value == Nullcore || value == Moonlit; }
    inline void Initialize(int saved, int startup)
    {
        Saved = Valid(saved) ? saved : Nullcore;
        Active = Valid(startup) ? startup : Saved;
        Pending = -1;
    }
    inline void Choose(int value) { if (Valid(value)) Pending = value == Active ? -1 : value; }
    inline bool ApplyClosed()
    {
        if (!Valid(Pending)) return false;
        Saved = Active = Pending; Pending = -1; return true;
    }
}
