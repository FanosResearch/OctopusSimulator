/*
 * File  :      test_system.cpp
 * TL-TOPO: the topology agents together, a TL-FUZZ-U-style run (plan §6) with the tests' own
 * random master: 4 masters (TL-UH, `full` data: Get, PutFullData, PutPartialData,
 * ArithmeticData, LogicalData, bursts) -> Throttle each -> Xbar 4 x 2 -> { WidthWidget 8->4
 * -> Fragmenter (max_size 3) -> RAM answering newest first } and { Buffer -> RAM }, one
 * ValueCheck shared by the four master links. 10 seeds: every request answered, ValueCheck
 * clean, the system idle. Plus TL-DET for this system: the same seed twice gives the same
 * transcript. The T2 Monitor and FuzzMaster replace the test's master in TL-FUZZ-U proper.
 */
#include "TopoTest.h"
#include "tilelink/ValueCheck.h"
#include "tilelink/agents/Buffer.h"
#include "tilelink/agents/Fragmenter.h"
#include "tilelink/agents/Throttle.h"
#include "tilelink/agents/WidthWidget.h"
#include "tilelink/agents/Xbar.h"
#include <sstream>

using namespace topo;

namespace
{
    // Random legal TL-UH traffic from a 2^o source pool; D always ready.
    class RandMaster : public Agent
    {
    public:
        RandMaster(const std::string &n, unsigned total) : Agent(n), total(total) {}
        Level capability(const Link &, Role) const override { return Level::UH; }
        unsigned total, issued = 0, answered = 0;
        std::set<uint64_t> busy;
        MsgPtr cur;
        unsigned beat = 0;
        uint64_t reuse = 0;
        Link &link() const { return *m_links.at(0).link; }
        uint64_t rnd(uint64_t n) { return rng()() % n; }
        MsgPtr generate(uint64_t src)
        {
            unsigned w = link().params().w;
            unsigned pick = unsigned(rnd(100));
            unsigned op = pick < 35 ? unsigned(OpA::Get) : pick < 60 ? unsigned(OpA::PutFullData) : pick < 75 ? unsigned(OpA::PutPartialData)
                        : pick < 90 ? unsigned(OpA::ArithmeticData) : unsigned(OpA::LogicalData);
            bool atomic = op == unsigned(OpA::ArithmeticData) || op == unsigned(OpA::LogicalData);
            unsigned size = unsigned(rnd(atomic ? 4 : 7));
            // a small address set per slave for contention: a 256-byte window in each
            uint64_t base = rnd(2) ? 0x1000 : 0x0;
            uint64_t addr = base + (rnd(256 >> size) << size);
            MsgPtr m = make(Channel::A, op, size, addr, src, w, DataMode::Full, [this](uint64_t) { return rnd(256); });
            if (op == unsigned(OpA::ArithmeticData)) m->param = uint8_t(rnd(5));
            if (op == unsigned(OpA::LogicalData)) m->param = uint8_t(rnd(4));
            if (op == unsigned(OpA::PutPartialData))
                for (uint64_t x = addr; x < addr + m->bytes(); x++) { LanePos p = lanePos(*m, x); m->mask[size_t(p.beat) * w + p.lane] = uint8_t(rnd(2)); }
            m->id = m_kernel->newMsgId();
            m->from_agent = m_id;
            return m;
        }
        void offer(uint64_t) override
        {
            if (!cur && issued < total)
            {
                uint64_t n = link().params().numSources();
                std::vector<uint64_t> free;
                for (uint64_t s = 0; s < n; s++) if (!busy.count(s)) free.push_back(s);
                if (free.empty() || rnd(4) == 0) return;          // sometimes idle: valid LOW is always legal
                cur = generate(free[size_t(rnd(free.size()))]);
                beat = 0;
            }
            if (cur) link().chan(Channel::A).offer(cur, beat);
        }
        void accept(uint64_t) override { link().chan(Channel::D).setReady(true); }
        void commit(uint64_t) override
        {
            const Beat &a = link().chan(Channel::A).accepted();
            if (a.msg)
            {
                if (a.index == 0) { if (busy.count(a.msg->source)) reuse++; busy.insert(a.msg->source); }
                if (a.last()) { cur = nullptr; issued++; } else beat = a.index + 1;
            }
            const Beat &d = link().chan(Channel::D).accepted();
            if (d.msg && d.last()) { busy.erase(d.msg->source); answered++; }
        }
        bool idle() const override { return answered == total; }
    };

    struct Result { bool done; unsigned answered; uint64_t violations, reads, atomics, writes, reuse; std::string transcript; };

    Result runSystem(uint64_t seed, unsigned per_master, bool transcript = false, bool mutant = false)
    {
        Kernel k(opts(seed));
        std::vector<RandMaster *> ms;
        std::vector<Throttle *> ths;
        for (int i = 0; i < 4; i++) ms.push_back(addAgent(k, new RandMaster("m" + std::to_string(i), per_master)));
        for (int i = 0; i < 4; i++) ths.push_back(addAgent(k, new Throttle("t" + std::to_string(i), cfg({"p_ready(d)=0.3", "p_valid(d)=0.3"}))));
        Xbar *x = addAgent(k, new Xbar("X", cfg({"routes(vs)=XA:0x0:0x1000,XB:0x1000:0x1000", "arbitration(s)=rr"})));
        WidthWidget *ww = addAgent(k, new WidthWidget("W", Config()));
        Fragmenter *fr = addAgent(k, new Fragmenter("F", cfg({"max_size(i)=3", "max_unsplit(i)=3"})));
        Buffer *bf = addAgent(k, new Buffer("B", cfg({"depth(i)=2"})));
        MemSlave *ra = memslave(k, "ra"), *rb = memslave(k, "rb");
        ra->lifo = true; ra->latency = 3;
        rb->latency = 1;
        if (mutant)
        {
            // every 10th clean AccessAckData from rb returns a wrong byte, corrupt LOW: a lie
            unsigned n = 0;
            rb->mutate = [n](Msg &r) mutable
            {
                if (r.opcode == unsigned(OpD::AccessAckData) && !r.denied && ++n % 10 == 0)
                    r.data[size_t(lanePos(r, r.address).beat) * r.w + lanePos(r, r.address).lane] ^= 0x80;
            };
        }
        ValueCheck vc;
        uint64_t violations = 0;
        vc.onViolation = [&](const ValueViolation &v) { violations++; if (violations < 4 && !mutant) std::printf("    %s\n", v.describe().c_str()); };
        for (int i = 0; i < 4; i++)
        {
            Link &l = k.connect("M" + std::to_string(i), lp(8, Level::UH, 2), *ms[size_t(i)], *ths[size_t(i)]);
            vc.attach(l);
            k.connect("T" + std::to_string(i), lp(8, Level::UH, 2), *ths[size_t(i)], *x);
        }
        k.connect("XA", lp(8, Level::UH, 4), *x, *ww);
        k.connect("WF", lp(4, Level::UH, 4), *ww, *fr);
        k.connect("FA", lp(4, Level::UH, 8), *fr, *ra);
        k.connect("XB", lp(8, Level::UH, 4), *x, *bf);
        k.connect("BB", lp(8, Level::UH, 4), *bf, *rb);
        std::ostringstream o;
        if (transcript) k.enableTranscript(o);
        uint64_t n = k.runUntilIdle(200000);
        Result r{n < 200000, 0, violations, vc.counters().reads, vc.counters().atomics, vc.counters().writes, 0, o.str()};
        for (auto *m : ms) { r.answered += m->answered; r.reuse += m->reuse; }
        return r;
    }
}

TEST(fuzz_ten_seeds_value_check_clean)
{
    for (uint64_t seed = 1; seed <= 10; seed++)
    {
        Result r = runSystem(seed, 150);
        CHECK(r.done);
        CHECK_EQ(r.answered, 600u);
        CHECK_EQ(r.violations, uint64_t(0));
        CHECK_EQ(r.reuse, uint64_t(0));
        CHECK(r.reads > 100);
        CHECK(r.atomics > 50);
        CHECK(r.writes > 100);
    }
}

TEST(mutant_slave_is_caught_end_to_end)
{
    // The checker is live through the whole topology: a RAM that silently flips a returned
    // byte (corrupt LOW) is reported (V-READ or V-ATOMIC), on every seed.
    for (uint64_t seed = 1; seed <= 3; seed++)
    {
        Result r = runSystem(seed, 150, false, true);
        CHECK(r.done);
        CHECK(r.violations > 0);
    }
}

TEST(same_seed_same_transcript)
{
    // TL-DET for this system (plan §6): identical transcripts.
    Result a = runSystem(42, 40, true), b = runSystem(42, 40, true), c = runSystem(43, 40, true);
    CHECK(a.transcript.size() > 1000);
    CHECK(a.transcript == b.transcript);
    CHECK(a.transcript != c.transcript);
}

TL_TEST_MAIN("test_system")
