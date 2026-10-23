// TL-MASK (docs/TileLink_Review_Action_Plan.md problem 5): the monitor checks every mask lane of the bus, w up to
// 4096 (Table 4), not the first 64. Widths 8, 64, 128, 4096; lanes 63, 64, 100 and the last; a missing active lane
// (M-MASK-2), an extra inactive lane (M-MASK-1), a fault on a later beat, PutPartialData's legal partial and zero
// masks and its out-of-region lane (M-MASK-3); each fault reported once per rule and beat, its lane named. The
// same check on B (a Probe at w = 128), and in fatal mode (a child process: exit 2, the rule and lane named).
#include "Subprocess.h"
#include "../fixtures/ScriptedMaster.h"
#include "../fixtures/ScriptedSlave.h"
#include "tilelink/Kernel.h"
#include "tilelink/Monitor.h"

using namespace octopus::tl;
using namespace octopus::tl::fixture;

namespace
{
    unsigned lg(unsigned w) { unsigned s = 0; while ((1u << s) < w) s++; return s; }

    struct Bus
    {
        Kernel k;
        ScriptedMaster *M; ScriptedSlave *S; Link *l;
        std::unique_ptr<Monitor> mon;
        unsigned w;
        uint64_t t = 100;
        explicit Bus(unsigned width, Level level = Level::UH, MonitorOptions::Mode mode = MonitorOptions::Report)
            : k(KernelOptions{100, 1, DataMode::None}), w(width)
        {
            M = static_cast<ScriptedMaster *>(&k.add(std::unique_ptr<Agent>(new ScriptedMaster("M"))));
            S = static_cast<ScriptedSlave *>(&k.add(std::unique_ptr<Agent>(new ScriptedSlave("S"))));
            LinkParams p; p.w = w; p.a = 32; p.z = 4; p.o = 4; p.level = level; if (level == Level::C) p.i = 4;
            l = &k.connect("L0", p, *M, *S);
            MonitorOptions mo; mo.mode = mode; mo.print = false; mo.max_size = 15;
            mon.reset(new Monitor(k, *l, mo));
        }
        ~Bus() { mon.reset(); }
        // offered once at the next slot and rejected (the mask is checked at the offer); `edit` changes the mask
        void offer(OpA op, unsigned size, uint64_t addr, std::function<void(Msg &)> edit, unsigned beat = 0)
        {
            static uint64_t src = 0;
            MsgPtr m = makeMsg(k, Channel::A, unsigned(op), size, w, addr, (src++) & 15, DataMode::None);
            if (edit) edit(*m);
            if (beat == 0) { M->at(t, Channel::A, m, 0); S->notReady(Channel::A, t); }
            else
            {
                // beats before `beat` accepted, `beat` offered and rejected
                for (unsigned b = 0; b < beat; b++) M->at(t + b, Channel::A, m, b);
                M->at(t + beat, Channel::A, m, beat); S->notReady(Channel::A, t + beat);
            }
            t += beat + 2;
        }
        void run() { k.run(t + 10); }
        std::vector<Violation> of(const std::string &rule) const
        {
            std::vector<Violation> v; for (auto &x : mon->log()) if (x.rule == rule) v.push_back(x); return v;
        }
    };
    void lane(Msg &m, unsigned beat, unsigned l, bool on) { m.mask[size_t(beat) * m.w + l] = on ? 1 : 0; }
}

TEST(missing_active_lane_found_at_every_width)
{
    for (unsigned w : {8u, 64u, 128u, 4096u})
        for (unsigned bad : {63u, 64u, 100u, w - 1})
        {
            if (bad >= w) continue;
            Bus b(w);
            b.offer(OpA::Get, lg(w), 0, [&](Msg &m) { lane(m, 0, bad, false); });   // a Get covering the bus, one lane LOW
            b.run();
            auto v = b.of("M-MASK-2");
            CHECK_EQ(v.size(), size_t(1));
            if (!v.empty()) CHECK(v[0].text.find("lane(s) " + std::to_string(bad)) != std::string::npos);
            CHECK_EQ(b.mon->violations(), uint64_t(1));                             // nothing else
        }
}

TEST(extra_inactive_lane_found_at_every_width)
{
    for (unsigned w : {8u, 64u, 128u, 4096u})
    {
        Bus b(w);
        // a half-bus Put at 0: lanes 0..w/2-1 active; the last lane HIGH is inactive
        b.offer(OpA::PutFullData, lg(w) - 1, 0, [&](Msg &m) { lane(m, 0, w - 1, true); });
        b.run();
        auto v = b.of("M-MASK-1");
        CHECK_EQ(v.size(), size_t(1));
        if (!v.empty()) CHECK(v[0].text.find("lane(s) " + std::to_string(w - 1)) != std::string::npos);
        CHECK_EQ(b.mon->violations(), uint64_t(1));
    }
}

TEST(fault_on_a_later_beat)
{
    for (unsigned w : {64u, 128u})
    {
        Bus b(w);
        b.offer(OpA::PutFullData, lg(w) + 1, 0, [&](Msg &m) { lane(m, 1, 100 % w, false); }, 1);   // beat 1, lane 100 (or 36)
        b.run();
        auto v = b.of("M-MASK-2");
        CHECK_EQ(v.size(), size_t(1));
        if (!v.empty()) CHECK(v[0].msg.find("beat=1") != std::string::npos);
    }
}

TEST(legal_masks_raise_nothing)
{
    for (unsigned w : {8u, 128u, 4096u})
    {
        Bus b(w);
        b.offer(OpA::Get, lg(w), 0, nullptr);                                                    // full mask
        b.offer(OpA::PutPartialData, lg(w), 0, [&](Msg &m) { for (unsigned l = 0; l < w; l += 2) lane(m, 0, l, false); });   // partial
        b.offer(OpA::PutPartialData, lg(w), 0, [&](Msg &m) { for (unsigned l = 0; l < w; l++) lane(m, 0, l, false); });      // zero
        b.run();
        CHECK_EQ(b.mon->violations(), uint64_t(0));
    }
}

TEST(put_partial_outside_its_region)
{
    for (unsigned w : {8u, 128u, 4096u})
    {
        Bus b(w);
        b.offer(OpA::PutPartialData, lg(w) - 1, 0, [&](Msg &m) { lane(m, 0, w - 1, true); });     // a lane past its half-bus region
        b.run();
        CHECK_EQ(b.of("M-MASK-3").size(), size_t(1));
        CHECK_EQ(b.of("M-MASK-1").size(), size_t(0));                                             // the PutPartial rule, not M-MASK-1
        CHECK_EQ(b.mon->violations(), uint64_t(1));
    }
}

TEST(probe_on_b_checked_at_every_lane)
{
    // a ProbeBlock of a 64-byte block on a 128-byte TL-C bus: lanes 0..63 active. Lane 100 HIGH is M-MASK-1, lane 63
    // LOW is M-MASK-2
    for (unsigned bad : {63u, 100u})
    {
        Bus b(128, Level::C);
        MsgPtr m = makeMsg(b.k, Channel::B, unsigned(OpB::ProbeBlock), 6, 128, 0, 0, DataMode::None, unsigned(Cap::toN));
        lane(*m, 0, bad, bad >= 64);
        b.S->at(100, Channel::B, m, 0); b.M->notReady(Channel::B, 100);
        b.run();
        auto v = b.of(bad >= 64 ? "M-MASK-1" : "M-MASK-2");
        CHECK_EQ(v.size(), size_t(1));
        if (!v.empty()) CHECK(v[0].text.find("lane(s) " + std::to_string(bad)) != std::string::npos);
        CHECK_EQ(b.of(bad >= 64 ? "M-MASK-2" : "M-MASK-1").size(), size_t(0));
    }
}

CHILD_CASE(fatal_lane_100)
{
    Bus b(4096, Level::UH, MonitorOptions::Fatal);
    b.offer(OpA::Get, 12, 0, [&](Msg &m) { lane(m, 0, 100, false); });
    b.run();
}

TEST(fatal_mode_stops_on_a_missing_lane)
{
    tltest::ChildResult r = tltest::runCase("fatal_lane_100");
    CHECK_EQ(r.status, 2);
    CHECK(r.err.find("M-MASK-2") != std::string::npos);
    CHECK(r.err.find("lane(s) 100") != std::string::npos);
}

TL_TEST_MAIN_WITH_CHILDREN("TL-MASK")
