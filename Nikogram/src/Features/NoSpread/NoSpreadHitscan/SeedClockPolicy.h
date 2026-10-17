#pragma once
// Server seed clock for hitscan no-spread. Pure logic, no SDK, so it can be
// simulated and tested outside the game (tests/test_seed_clock.cpp).
//
// What the server does (Source SDK 2013 TF, CBasePlayer::ProcessUsercmds):
//   float stamp = float(Plat_FloatTime() * 1000.0);     // taken when OUR PACKET IS READ
//   cmd->server_random_seed = bits(stamp);               // low byte is the bullet seed
// A dedicated server reads packets at the start of each 15 ms frame, so the stamp
// of a packet read in server tick k is
//   ms(k) = intercept + 15 * k  (+ sub-ms frame jitter)
// and the seed is round(ms / step) mod 256, where step is the float's mantissa
// step at the current uptime (1 ms from 2.33 h, 2 ms from 4.66 h, 0.5 ms below).
//
// Three observations pin that line down without ever using the client's clock:
//   Ack   : the snapshot with server tick k acknowledges our command S, so the
//           packet whose last command is S was read in tick k (tick - S = offset).
//   Stamp : "playerperf" prints Plat_FloatTime() taken at the end of
//           ProcessUsercmds for each of our packets (the second block of lines),
//           microseconds after the seed stamp. The printed float seconds pin the
//           stamp to an interval; ms(S) - 15 * tick(S) is the intercept.
//   Echo  : TE_FireBullets from another player carries the real seed the server
//           used for a command read in tick k: the stamp was inside that seed's
//           bin. It refines the intercept to seed resolution and gives an honest
//           live accuracy count.
// Which tick reads a packet is counted in client engine ticks (ObserveSendAck,
// PredictSend), not command numbers: tick shifting makes no commands while it
// recharges and many in one tick when it fires, so tick - cmd moves by the stored
// ticks twice per doubletap while tick - sendTick stays put (0.7.0.50).
// Both stamps and echoes are intervals, and 15 ms is not a multiple of the step,
// so medians of centres land on bin edges. Each estimate is the midpoint of the
// value range consistent with the most observations (set membership).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace SeedClockPolicy
{
    constexpr double DefaultTickMs = 15.0; // host_state.interval_per_tick (66.67 tick) in ms

    inline int SeedFromMs(double ms)
    {
        const float f = float(ms);
        std::uint32_t bits = 0; std::memcpy(&bits, &f, sizeof(bits));
        return int(bits & 255u);
    }

    // Mantissa step (ms) of float(ms): the seed changes once per step.
    inline double StepMs(double ms)
    {
        if (!(ms > 0.0) || !std::isfinite(ms)) return 0.0;
        int exponent = 0; std::frexp(ms, &exponent); // ms = m * 2^exponent, m in [0.5, 1)
        return std::ldexp(1.0, exponent - 24);
    }

    // Mantissa step (ms) of float(seconds), the type playerperf prints.
    inline double SecondsStepMs(double ms)
    {
        if (!(ms > 0.0) || !std::isfinite(ms)) return 0.0;
        int exponent = 0; std::frexp(ms / 1000.0, &exponent);
        return std::ldexp(1.0, exponent - 24) * 1000.0;
    }

    struct Interval { double lo = 0.0, hi = 0.0; };

    // The stamp interval behind a printed "%.3f" float of seconds. The server
    // rounds the stamp to a float, then prints it to 1 ms. Once the float's step
    // is above 1 ms the printed value identifies the float exactly.
    inline Interval StampInterval(double printedMs)
    {
        const double step = SecondsStepMs(printedMs);
        const double recovered = double(float(printedMs / 1000.0)) * 1000.0;
        if (step >= 1.0) return { recovered - step * 0.5, recovered + step * 0.5 };
        const double half = 0.5 + step * 0.5;
        return { printedMs - half, printedMs + half };
    }

    // The stamp interval behind an observed seed, nearest to `nearMs`.
    inline std::optional<Interval> SeedInterval(double nearMs, int seed)
    {
        const double step = StepMs(nearMs);
        if (!(step > 0.0)) return std::nullopt;
        const double index = std::round(nearMs / step);
        const int nearSeed = int(std::fmod(index, 256.0) + 256.0) & 255;
        int delta = ((seed & 255) - nearSeed + 256) & 255;
        if (delta > 127) delta -= 256;
        const double centre = (index + delta) * step;
        return Interval{ centre - step * 0.5, centre + step * 0.5 };
    }

    // Signed distance (ms) from predictedMs to the centre of the nearest bin that
    // yields `seed`, and the same in seed steps.
    struct Residual { double ms = 0.0; int steps = 0; bool valid = false; };
    inline Residual SeedResidual(double predictedMs, int seed)
    {
        const double step = StepMs(predictedMs);
        if (!(step > 0.0)) return {};
        const double index = std::round(predictedMs / step);
        const int predictedSeed = int(std::fmod(index, 256.0) + 256.0) & 255;
        int delta = ((seed & 255) - predictedSeed + 256) & 255;
        if (delta > 127) delta -= 256;
        return { (index + delta) * step - predictedMs, delta, true };
    }

    // Where the intervals agree. The densest point is where the most intervals
    // overlap; the estimate is the mean centre of the intervals in that cluster
    // (an interval's centre is an unbiased reading of its stamp, and averaging
    // over ticks cancels the per-frame rounding pattern that a bare overlap
    // midpoint or a median would keep). `count` is the cluster size.
    struct Coverage { double mid = 0.0; double estimate = 0.0; int count = 0; bool valid = false; };
    inline Coverage ClusterEstimate(const std::vector<Interval>& intervals)
    {
        if (intervals.empty()) return {};
        std::vector<std::pair<double, int>> events; events.reserve(intervals.size() * 2);
        double width = 0.0;
        for (const auto& i : intervals)
        {
            events.emplace_back(i.lo, +1); events.emplace_back(i.hi, -1);
            width = std::max(width, i.hi - i.lo);
        }
        std::sort(events.begin(), events.end(), [](const auto& a, const auto& b)
            { return a.first < b.first || (a.first == b.first && a.second < b.second); }); // close before open at a tie
        int depth = 0, best = 0; double bestLo = 0.0, bestHi = 0.0;
        for (size_t i = 0; i < events.size(); ++i)
        {
            depth += events[i].second;
            if (depth > best && i + 1 < events.size())
            {
                best = depth; bestLo = events[i].first; bestHi = events[i + 1].first;
            }
        }
        if (best <= 0) return {};
        const double mid = (bestLo + bestHi) * 0.5;
        double sum = 0.0; int count = 0;
        for (const auto& i : intervals)
        {
            const double centre = (i.lo + i.hi) * 0.5;
            if (std::fabs(centre - mid) <= width) { sum += centre; ++count; }
        }
        if (!count) return {};
        return { mid, sum / count, count, true };
    }

    struct Options
    {
        double tickMs = DefaultTickMs;  // server tick interval in ms (interval_per_tick * 1000)
        int ackWindow = 160;            // snapshots kept for cmd -> tick lookup (~2.4 s)
        int offsetVote = 8;             // recent offsets voted for the prediction
        int stampWindow = 180;          // three 60-line replies
        int echoWindow = 20;            // echoes scored on the HUD and used for the refinement
        double echoGateMs = 8.0;        // echoes further than this from the stamp line are outliers...
        int echoOverride = 6;           // ...unless this many recent ones agree with each other
        double minimumStepMs = 0.0;     // Ready() refuses smaller seed steps (the game gates with its own setting)
    };

    class FrameClock
    {
    public:
        explicit FrameClock(Options options = {}) : m_Options(options) {}

        void Reset() { *this = FrameClock(m_Options); }

        // Snapshot `tick` acknowledged our command `cmd` (newest acknowledged).
        void ObserveAck(int cmd, int tick)
        {
            int from = cmd - 1;                                   // first ack: assume one packet
            if (!m_Acks.empty())
            {
                if (cmd <= m_Acks.back().cmd) return;            // repeat or reorder: nothing new
                if (tick < m_Acks.back().tick) return;           // snapshot order broken
                from = m_Acks.back().cmd;
            }
            m_Acks.push_back({ from, cmd, tick });
            while (int(m_Acks.size()) > m_Options.ackWindow) m_Acks.pop_front();
            m_Offsets.push_back(tick - cmd);
            while (int(m_Offsets.size()) > m_Options.offsetVote) m_Offsets.pop_front();
        }

        // The same acknowledgement in engine-tick space: `sendTick` is the client engine
        // tick (one per CL_Move call, whether or not it made commands) in which the
        // acknowledged packet left. Command numbers stop on real time under tick shifting:
        // a recharge makes no commands for N ticks and the burst then makes N extra in one
        // tick, so tick - cmd jumps by N twice per doubletap. tick - sendTick does not.
        void ObserveSendAck(int sendTick, int tick)
        {
            if (!m_SendAcks.empty() && sendTick <= m_SendAcks.back()) return;
            m_SendAcks.push_back(sendTick);
            while (int(m_SendAcks.size()) > m_Options.offsetVote) m_SendAcks.pop_front();
            m_SendOffsets.push_back(tick - sendTick);
            while (int(m_SendOffsets.size()) > m_Options.offsetVote) m_SendOffsets.pop_front();
        }
        bool SendOffsetKnown() const { return !m_SendOffsets.empty(); }
        int SendOffset() const { return Mode(m_SendOffsets); }

        // Any server Plat_FloatTime seen (ms): fixes the mantissa step and the binade.
        void ObserveUptime(double serverMs)
        {
            if (std::isfinite(serverMs) && serverMs > 0.0) m_UptimeMs = std::max(m_UptimeMs, serverMs);
        }

        // playerperf stamp (printed ms) for the packet whose last command is `cmd`.
        void ObserveStamp(int cmd, double printedMs)
        {
            if (!std::isfinite(printedMs) || printedMs <= 0.0) return;
            ObserveUptime(printedMs);
            if (StepMs(printedMs) <= 0.0) return;
            const auto tick = TickFor(cmd);
            if (!tick) return;
            for (const auto& s : m_Stamps) if (s.cmd == cmd) return;        // overlapping replies
            const Interval stamp = StampInterval(printedMs);
            const double shift = m_Options.tickMs * double(*tick);
            m_Stamps.push_back({ cmd, { stamp.lo - shift, stamp.hi - shift } });
            while (int(m_Stamps.size()) > m_Options.stampWindow) m_Stamps.pop_front();
            m_Dirty = true;
            // Echoes heard before the first reply can be placed now.
            for (auto& e : m_Echoes) if (!e.haveInterval) PlaceEcho(e);
        }

        // Where this set of stamps (cmd, printed ms) would put the intercept, without
        // adopting them: lets a reply be checked against the line before it is used.
        std::optional<double> InterceptOf(const std::vector<std::pair<int, double>>& stamps) const
        {
            std::vector<Interval> intervals;
            for (const auto& [cmd, printedMs] : stamps)
            {
                if (!std::isfinite(printedMs) || printedMs <= 0.0) continue;
                const auto tick = TickFor(cmd);
                if (!tick) continue;
                const Interval stamp = StampInterval(printedMs);
                const double shift = m_Options.tickMs * double(*tick);
                intervals.push_back({ stamp.lo - shift, stamp.hi - shift });
            }
            const auto fit = ClusterEstimate(intervals);
            if (!fit.valid) return std::nullopt;
            return fit.estimate;
        }

        // Another player's shot: the server used `seed` for a command read in `tick`.
        // Returns whether the live prediction for that tick matched.
        bool ObserveEcho(int tick, int seed)
        {
            seed &= 255;
            Echo echo{ tick, seed };
            const auto live = PredictTick(tick);
            echo.scored = live.valid;
            echo.matched = live.valid && live.seed == seed;
            PlaceEcho(echo);
            m_Echoes.push_back(echo);
            while (int(m_Echoes.size()) > m_Options.echoWindow) m_Echoes.pop_front();
            m_Dirty = true;
            if (echo.haveInterval) ConsiderOverride();
            return echo.matched;
        }

        struct Prediction
        {
            bool valid = false; int seed = 0; double ms = 0.0; int tick = 0; double step = 0.0;
            int votes = 0, voters = 0; // echo bins that could vote for this tick, and how many agreed
        };

        // The command will be carried by the packet whose last command is `packetCmd`.
        Prediction Predict(int packetCmd) const
        {
            if (!OffsetKnown()) return {};
            return PredictTick(packetCmd + Offset());
        }

        // The packet leaving in engine tick `sendTick` (exactly known at send time).
        Prediction PredictSend(int sendTick) const
        {
            if (!SendOffsetKnown()) return {};
            return PredictTick(sendTick + SendOffset());
        }

        Prediction PredictTick(int tick) const
        {
            if (!HaveBase()) return {};
            const double ms = Intercept() + Refinement() + m_Options.tickMs * double(tick);
            const double step = StepMs(ms);
            if (!(step > 0.0)) return {};
            Prediction prediction{ true, SeedFromMs(ms), ms, tick, step };
            // When the server's frame line sits on a seed edge, frame jitter decides the
            // seed and no point estimate can: predict the bin most echoes landed in. An
            // echo's bin carries over to this tick exactly when 15 * (tick - its tick) is a
            // whole number of steps (always below 1 ms; every `step` ticks above).
            const double intercept = Intercept();
            std::vector<std::pair<long long, int>> votes;
            for (const auto& e : m_Echoes)
            {
                if (!e.haveInterval) continue;
                const double centre = (e.interval.lo + e.interval.hi) * 0.5;
                if (std::fabs(centre - intercept) > m_Options.echoGateMs) continue;
                const double shiftSteps = m_Options.tickMs * double(tick - e.tick) / step;
                if (std::fabs(shiftSteps - std::round(shiftSteps)) > 1e-6) continue;
                const long long index = std::llround((centre + m_Options.tickMs * double(e.tick)) / step) + std::llround(shiftSteps);
                bool found = false;
                for (auto& v : votes) if (v.first == index) { ++v.second; found = true; break; }
                if (!found) votes.emplace_back(index, 1);
                ++prediction.voters;
            }
            if (prediction.voters >= 3)
            {
                const auto top = std::max_element(votes.begin(), votes.end(),
                    [](const auto& a, const auto& b) { return a.second < b.second; });
                if (top->second * 2 > prediction.voters)
                {
                    prediction.seed = int(top->first & 255);
                    prediction.votes = top->second;
                }
            }
            return prediction;
        }

        bool Ready() const
        {
            return OffsetKnown() && HaveBase() && StepMs(m_UptimeMs) >= m_Options.minimumStepMs;
        }

        bool OffsetKnown() const { return !m_Offsets.empty(); }
        bool OffsetStable() const
        {
            if (m_Offsets.size() < 2) return false;
            for (int o : m_Offsets) if (o != m_Offsets.front()) return false;
            return true;
        }
        // Mode of the recent offsets, ties resolved towards the newest value.
        int Offset() const { return Mode(m_Offsets); }

        bool HaveBase() const { return !m_Stamps.empty() && m_UptimeMs > 0.0; }
        double UptimeMs() const { return m_UptimeMs; }
        double StepNowMs() const { return StepMs(m_UptimeMs); }
        int StampCount() const { return int(m_Stamps.size()); }
        int StampAgreement() const { Recompute(); return m_StampCoverage; }
        int EchoScored() const { int n = 0; for (const auto& e : m_Echoes) n += e.scored; return n; }
        int EchoMatched() const { int n = 0; for (const auto& e : m_Echoes) n += e.matched; return n; }
        int EchoCount() const { return int(m_Echoes.size()); }
        int EchoAgreement() const { Recompute(); return m_EchoCoverage; }
        double RefinementMs() const { return Refinement(); }
        double InterceptMs() const { return Intercept(); }
        std::optional<int> TickForCommand(int cmd) const { return TickFor(cmd); }

    private:
        static int Mode(const std::deque<int>& values)
        {
            int best = 0, bestCount = -1;
            for (auto it = values.rbegin(); it != values.rend(); ++it)
            {
                int count = 0; for (int o : values) count += (o == *it);
                if (count > bestCount) { best = *it; bestCount = count; }
            }
            return best;
        }

        struct Ack { int from; int cmd; int tick; }; // commands in (from, cmd] read in tick
        struct Stamp { int cmd; Interval intercept; };
        struct Echo
        {
            int tick; int seed;
            bool scored = false, matched = false, haveInterval = false;
            Interval interval{}; // intercept range implied by the seed
        };

        void PlaceEcho(Echo& echo) const
        {
            if (!HaveBase()) return;
            const double base = Intercept() + m_Options.tickMs * double(echo.tick);
            if (const auto bin = SeedInterval(base, echo.seed))
            {
                echo.interval = { bin->lo - m_Options.tickMs * double(echo.tick), bin->hi - m_Options.tickMs * double(echo.tick) };
                echo.haveInterval = true;
            }
        }

        std::optional<int> TickFor(int cmd) const
        {
            // Everything in (previous ack, ack] was read in that snapshot's tick.
            for (const auto& ack : m_Acks)
                if (cmd > ack.from && cmd <= ack.cmd) return ack.tick;
            if (!m_Acks.empty() && cmd > m_Acks.back().cmd && OffsetKnown()) return cmd + Offset();
            return std::nullopt; // older than the window, or never acknowledged
        }

        void Recompute() const
        {
            if (!m_Dirty) return;
            m_Dirty = false;
            std::vector<Interval> stamps; stamps.reserve(m_Stamps.size());
            for (const auto& s : m_Stamps) stamps.push_back(s.intercept);
            const auto stampFit = ClusterEstimate(stamps);
            m_Intercept = stampFit.valid ? stampFit.estimate : 0.0;
            m_StampCoverage = stampFit.count;
            // Echoes within the gate of the stamp line refine it.
            std::vector<Interval> echoes;
            for (const auto& e : m_Echoes)
            {
                if (!e.haveInterval) continue;
                const double mid = (e.interval.lo + e.interval.hi) * 0.5;
                if (std::fabs(mid - m_Intercept) <= m_Options.echoGateMs) echoes.push_back(e.interval);
            }
            const auto echoFit = ClusterEstimate(echoes);
            m_Refinement = echoFit.valid ? echoFit.estimate - m_Intercept : 0.0;
            m_EchoCoverage = echoFit.count;
        }
        double Intercept() const { Recompute(); return m_Intercept; }
        double Refinement() const { Recompute(); return m_Refinement; }

        // A server hitch that drops time moves the whole line at once. Stamps follow
        // within a reply or two; enough recent echoes that agree with each other but
        // not with the stamp line re-phase it now.
        void ConsiderOverride()
        {
            const int n = m_Options.echoOverride;
            if (n <= 0 || int(m_Echoes.size()) < n) return;
            std::vector<Interval> recent;
            const double intercept = Intercept();
            for (auto it = m_Echoes.rbegin(); it != m_Echoes.rend() && int(recent.size()) < n; ++it)
            {
                if (!it->haveInterval) return;
                const double mid = (it->interval.lo + it->interval.hi) * 0.5;
                if (std::fabs(mid - intercept) <= m_Options.echoGateMs) return;
                recent.push_back(it->interval);
            }
            const auto fit = ClusterEstimate(recent);
            if (!fit.valid || fit.count < n) return;
            const double delta = fit.estimate - intercept;
            for (auto& s : m_Stamps) { s.intercept.lo += delta; s.intercept.hi += delta; }
            m_Dirty = true;
        }

        Options m_Options{};
        std::deque<Ack> m_Acks{};
        std::deque<int> m_Offsets{};
        std::deque<int> m_SendAcks{}, m_SendOffsets{};
        std::deque<Stamp> m_Stamps{};
        std::deque<Echo> m_Echoes{};
        double m_UptimeMs = 0.0;
        mutable double m_Intercept = 0.0, m_Refinement = 0.0;
        mutable int m_StampCoverage = 0, m_EchoCoverage = 0;
        mutable bool m_Dirty = true;
    };

    // Aligns one playerperf reply (second block, newest first: "time numcmds dropped")
    // with our own send records. A reply never lists the request's own packet, so the
    // newest line is the packet sent before it; lost packets print no line, but the
    // next newer packet's `dropped` says how many were lost just before it.
    // sentMs: client Plat_FloatTime when the packet is finalized (just before its commands
    // are written); sendTick: client engine tick it left in (see ObserveSendAck).
    struct SentPacket { int cmd = 0; int newCommands = 0; double sentMs = 0.0; int sendTick = -1; };
    struct ReplyLine { double serverMs = 0.0; int numCmds = 0; int dropped = 0; };

    // `sent` newest last. Returns (cmd, serverMs) pairs for the lines that could be
    // placed; empty when the command fingerprint contradicts every plausible shift.
    inline std::vector<std::pair<int, double>> AlignReply(const std::vector<SentPacket>& sent,
        int requestCmd, const std::vector<ReplyLine>& lines)
    {
        std::vector<std::pair<int, double>> best; int bestMismatches = std::numeric_limits<int>::max();
        // Index of the record just before the request.
        int start = -1;
        for (int i = int(sent.size()) - 1; i >= 0; --i) if (sent[i].cmd < requestCmd) { start = i; break; }
        if (start < 0 || lines.empty()) return {};
        for (int shift = 0; shift <= 3; ++shift)
        {
            std::vector<std::pair<int, double>> placed; int mismatches = 0, index = start - shift;
            for (const auto& line : lines)
            {
                if (index < 0) break;
                if (sent[index].newCommands != line.numCmds) ++mismatches;
                placed.emplace_back(sent[index].cmd, line.serverMs);
                index -= 1 + std::clamp(line.dropped, 0, 1024);
            }
            if (placed.empty()) continue;
            // Prefer the natural alignment; a later shift must be strictly better.
            if (mismatches < bestMismatches) { bestMismatches = mismatches; best = std::move(placed); }
        }
        if (bestMismatches > int(lines.size()) / 4) return {};
        return best;
    }
}
