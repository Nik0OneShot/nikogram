#pragma once
namespace MenuMode
{
    enum Style { Nullcore = 0, Nikogram = 1, Moonlit = Nikogram, DapperDesktop = 2, BankOfDapper = 3, DapperScrapbook = 4 };
    inline bool Valid(int value) { return value >= Nullcore && value <= DapperScrapbook; }
    inline bool Custom(int value) { return Valid(value) && value != Nullcore; }
    inline bool Dapper(int value) { return value >= DapperDesktop && value <= DapperScrapbook; }
    inline int InitialPreference(int saved, int revision, bool existing)
    {
        if (revision >= 2 && Valid(saved)) return saved;
        return existing ? Nullcore : Nikogram;
    }
    inline int Saved = Nullcore, Active = Nullcore, Pending = -1;
    // -1 preserves the previous behaviour: indicators follow the menu.
    inline int Indicator = -1;
    inline int Hud() { return Valid(Indicator) ? Indicator : Active; }
    inline bool Animations = true, SmoothSliders = true;
    inline bool DrawingMoonlit = false;
    inline int DrawingStyle = Nikogram;
    inline float AnimationSpeed = 1.f;
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
