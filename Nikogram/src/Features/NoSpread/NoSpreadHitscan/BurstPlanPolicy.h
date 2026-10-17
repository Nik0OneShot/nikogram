#pragma once
// Spread placement for the shots of one packet. Pure logic, no SDK: the geometry
// (solving a view, counting hits) is a template parameter, so the same code runs
// in the game and in tests/test_burst_plan.cpp.
//
// What the server does with a packet of ours (Source SDK 2013 TF):
//   - ProcessUsercmds stamps every command of the packet from Plat_FloatTime, read
//     per command inside one loop microseconds long, so every shot in it uses the
//     same seed (sv_usercmd_custom_random_seed 1); only a packet straddling a seed
//     edge at the finest steps splits.
//   - CTFPlayer::NoteWeaponFired locks the view angles in the server tick of the
//     first shot, and PlayerRunCommand restores them for every later command run
//     in that tick (sv_maxusrcmdprocessticks_holdaim 1, FCVAR_CHEAT, so every
//     server). A doubletap burst runs in one tick: shots 2..N fire along the FIRST
//     shot's view; their own view angles are ignored for shooting. (Separate packets
//     read in one tick are held too, e.g. warp sending each shot alone; not planned.)
//   - FX_FireBullets: pellet i of a shot leaves along base + x*spread*right +
//     y*spread*up, (x, y) from RandomSeed(seed + i); pellet 0 is exact when the
//     previous shot was long enough ago (first-shot accuracy).
// So a locked burst has ONE free variable, the first shot's view, shared by
// patterns that differ only where first-shot accuracy differs. The plan tries
// every pellet of every shot as the one that lands on its shot's aim point and
// keeps the view that puts the most bullets of the whole packet on the target.
#include <algorithm>
#include <cmath>
#include <vector>

namespace BurstPlanPolicy
{
    struct Pellet { float x = 0.f, y = 0.f; };

    struct Shot
    {
        std::vector<Pellet> pellets;   // this shot's pattern for its seed
        int defaultPellet = 0;         // the aimbot's intent: nearest the mean, or the exact pellet 0
        bool exactFirst = false;       // pellet 0 is exact (first-shot accuracy)
    };

    struct Plan
    {
        bool valid = false;
        int anchorAim = 0;      // the shot whose aim point the chosen pellet lands on
        int anchorShot = 0, anchorPellet = 0; // the pellet (shot's pattern, index) placed there
        int hits = -1;          // confirmed hits of the chosen view (-1: no target to count)
        int defaultHits = -1;   // confirmed hits of the old placement (first shot's default pellet)
        int exact = 0;          // bullets leaving exactly along their shot's aim point
        int candidates = 0;
    };

    // Squared distance of a pellet from its pattern's mean: the ranking tie-break.
    inline float MeanDistance(const std::vector<Pellet>& pellets, int i)
    {
        float mx = 0.f, my = 0.f;
        for (const auto& p : pellets) { mx += p.x; my += p.y; }
        mx /= float(pellets.size()); my /= float(pellets.size());
        const float dx = pellets[i].x - mx, dy = pellets[i].y - my;
        return dx * dx + dy * dy;
    }

    // Distance from the aimbot's intent: the exact pellet 0 when there is one, else the mean.
    inline float IntentDistance(const Shot& shot, int i)
    {
        if (shot.exactFirst) return shot.pellets[i].x * shot.pellets[i].x + shot.pellets[i].y * shot.pellets[i].y;
        return MeanDistance(shot.pellets, i);
    }

    inline int DefaultPellet(const std::vector<Pellet>& pellets, bool exactFirst)
    {
        if (exactFirst || pellets.empty()) return 0;
        int best = 0; float bestDistance = 1e30f;
        for (int i = 0; i < int(pellets.size()); i++)
        {
            const float d = MeanDistance(pellets, i);
            if (d < bestDistance) { bestDistance = d; best = i; }
        }
        return best;
    }

    // Geometry G:
    //   using Angles = ...;
    //   Angles Solve(int shot, const Pellet& p) const;          view that sends a pellet at offset p along `shot`'s aim point
    //   int Hits(const Angles& view, int shot, bool trace) const; target hits of `shot` fired from `view` (0 without a target)
    //   int Exact(const Angles& view, int shot) const;            pellets of `shot` leaving along its aim point
    //   bool HasTarget() const;
    // `shots` are the packet's shots in command order; shot 0 fires first and, when
    // `locked`, its view is the one every shot fires along.
    template <class G>
    Plan Choose(const std::vector<Shot>& shots, const G& g, typename G::Angles& out)
    {
        Plan plan{};
        if (shots.empty() || shots[0].pellets.empty()) return plan;

        // A candidate puts one pellet offset (taken from any shot's pattern) on one shot's
        // aim point. A locked burst's patterns differ (first-shot accuracy), and the aimbot
        // re-aims every command, so every offset is tried at every shot's aim point.
        struct Candidate { int aim, shot, pellet; typename G::Angles view; int hits, exact; float intent; };
        std::vector<Candidate> candidates;
        for (int a = 0; a < int(shots.size()); a++)
            for (int s = 0; s < int(shots.size()); s++)
                for (int i = 0; i < int(shots[s].pellets.size()); i++)
                {
                    const auto& q = shots[s].pellets[i];
                    bool duplicate = false; // same offset at the same aim point: same view
                    for (const auto& c : candidates)
                    {
                        if (c.aim != a) continue;
                        const auto& p = shots[c.shot].pellets[c.pellet];
                        if (std::fabs(p.x - q.x) < 1e-6f && std::fabs(p.y - q.y) < 1e-6f) { duplicate = true; break; }
                    }
                    if (duplicate) continue;
                    Candidate c{ a, s, i, g.Solve(a, q), 0, 0, 0.f };
                    for (int k = 0; k < int(shots.size()); k++)
                    {
                        c.hits += g.Hits(c.view, k, false);
                        // Exactness only ranks views shared by several shots; a single shot keeps
                        // the old order (hits, then the aimbot's intent).
                        if (shots.size() > 1) c.exact += g.Exact(c.view, k);
                    }
                    // Ties keep the first shot's aim point (it fires even if a later one does
                    // not), then offsets of that shot's own pattern, then the aimbot's intent.
                    c.intent = float(a) * 1000.f + (s == a ? 0.f : 100.f)
                        + (i == shots[s].defaultPellet ? 0.f : 1.f + IntentDistance(shots[s], i));
                    candidates.push_back(c);
                }
        plan.candidates = int(candidates.size());
        if (candidates.empty()) return plan;

        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
        {
            if (a.hits != b.hits) return a.hits > b.hits;
            if (a.exact != b.exact) return a.exact > b.exact;
            return a.intent < b.intent;
        });

        // Without a target, the ordering already is the answer: most bullets exact.
        int chosen = 0;
        if (g.HasTarget())
        {
            // Confirm the best few against the world; the first confirmed count that no
            // later candidate's unconfirmed count can beat wins.
            int best = -1, bestHits = -1;
            for (int c = 0; c < int(candidates.size()) && c < 4; c++)
            {
                if (candidates[c].hits <= bestHits) break;
                int confirmed = 0;
                for (int k = 0; k < int(shots.size()); k++) confirmed += g.Hits(candidates[c].view, k, true);
                if (confirmed > bestHits) { bestHits = confirmed; best = c; }
            }
            if (best >= 0 && bestHits > 0) { chosen = best; plan.hits = bestHits; }
            else if (shots.size() == 1)
            {
                // One shot, nothing confirmed: the aimbot's intent, as 0.7.0.36.
                std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
                    { return a.exact != b.exact ? a.exact > b.exact : a.intent < b.intent; });
                plan.hits = 0;
            }
            else
                plan.hits = 0; // a burst keeps the hitbox order: unconfirmed beats nothing
            // The old placement for comparison (unconfirmed count, as the log always
            // printed it): the first shot's own default pellet.
            for (const auto& c : candidates)
            {
                if (c.aim != 0) continue;
                const auto& p = shots[c.shot].pellets[c.pellet];
                const auto& d = shots[0].pellets[shots[0].defaultPellet];
                if (std::fabs(p.x - d.x) < 1e-6f && std::fabs(p.y - d.y) < 1e-6f) { plan.defaultHits = c.hits; break; }
            }
        }

        const auto& pick = candidates[chosen];
        out = pick.view;
        plan.valid = true;
        plan.anchorAim = pick.aim;
        plan.anchorShot = pick.shot;
        plan.anchorPellet = pick.pellet;
        plan.exact = pick.exact;
        return plan;
    }
}
