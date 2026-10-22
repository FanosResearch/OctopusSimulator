/*
 * File  :      test_uncached.cpp
 * TL-UNCACHED (plan §6; plan §8 decision 8: the manager invalidates on write). An uncached FuzzMaster
 * on its own TL-UH link beside 2 and 4 CacheMasters on 16 blocks, full data, Throttles everywhere:
 *   - the end-to-end form of "every Put/Atomic preceded by ProbeBlock toN to every holder, every Get
 *     by at most a toB probe of the owner": when the manager performs an uncached Put or atomic (the
 *     Execute / ForwardIn cell), no CacheMaster holds the block (CoherenceCheck's permissions on the
 *     masters' links); when it answers a Get from its copy, none holds it at Tip (the response may then
 *     wait on the uncached link while the block is legally granted again);
 *   - CoherenceCheck clean (SWMR and C-VALUE: the uncached accesses are reported by the manager, so no
 *     holder ever reads a value older than an acknowledged Put), Monitor clean, no watchdog trip;
 *   - a run with CBOs in the mix (FuzzMaster Intent), data mode none (CBOInval discards dirty data by
 *     definition, so values are not compared there);
 *   - the directed case of decision 8 (Branch holders, an uncached Put, then the holder reads the Put)
 *     and an uncached Get of a dirty Tip (probed toB, the AccessAckData carries the dirty data).
 */
#include "../fixtures/TlcSystem.h"
#include "../flows/FlowTest.h"
#include "../topology/TopoTest.h"
#include <map>

using namespace octopus::tl;

namespace
{
    struct Fuzz { uint64_t ops = 0, cycles = 0, puts = 0, gets = 0, bad = 0, viol = 0, cv = 0, trips = 0; };

    Fuzz run(unsigned masters, uint64_t seed, bool cmos)
    {
        tlc::Options o;
        o.masters = masters; o.seed = seed; o.ops = 800; o.uncached = 1; o.uncached_ops = 600;
        o.uncached_cfg = {"w_intent(i)=" + std::string(cmos ? "3" : "0"), "w_arith(i)=2", "w_logic(i)=2", "w_put(i)=3", "w_putpartial(i)=2"};
        if (cmos) o.mode = DataMode::None;
        tlc::System s;
        s.build(o);
        Fuzz f;
        // the end-to-end check: when the manager performs an uncached access (the cell that answers it:
        // Execute, AccessAckData_in, ForwardIn), no master holds the block for a write, none at Tip for a read
        s.mgr->onTransition = [&](const coh::Transition &t)
        {
            if (t.stalled) return;
            bool write = t.cell.find("Execute") != std::string::npos;
            bool read = t.cell.find("AccessAckData_in") != std::string::npos;
            bool fwd = t.cell.find("ForwardIn") != std::string::npos;
            if (!write && !read && !fwd) return;
            (read ? f.gets : f.puts)++;
            for (unsigned m = 0; m < masters; m++)
            {
                char p = s.cc->permission("L" + std::to_string(m), t.block);
                if (read ? p == 'T' : p != 'N')
                {
                    if (f.bad < 5) std::printf("    %s: master %u still holds %c\n", t.line().c_str(), m, p);
                    f.bad++;
                }
            }
        };
        f.cycles = s.run(o.max_cycles);
        CHECK(s.allDone());
        f.ops = s.opsDone() + s.ums[0]->answered();
        CHECK_EQ(s.ums[0]->answered(), uint64_t(o.uncached_ops));
        f.viol = s.monitorViolations();
        if (f.viol) std::printf("%s", s.monitorSummary().c_str());
        f.cv = s.cviol.size() + s.vviol.size();
        f.trips = s.wd->trips().size();
        return f;
    }

    void sweep(unsigned masters, bool cmos)
    {
        Fuzz t;
        for (uint64_t seed = 1; seed <= 10; seed++)
        {
            Fuzz f = run(masters, seed, cmos);
            t.ops += f.ops; t.cycles += f.cycles; t.puts += f.puts; t.gets += f.gets; t.bad += f.bad; t.viol += f.viol; t.cv += f.cv; t.trips += f.trips;
        }
        CHECK_EQ(t.bad, uint64_t(0));
        CHECK_EQ(t.viol, uint64_t(0));
        CHECK_EQ(t.cv, uint64_t(0));
        CHECK_EQ(t.trips, uint64_t(0));
        CHECK(t.puts > 0 && t.gets > 0);
        std::printf("  %u masters + 1 uncached%s: 10 seeds, %llu ops, %llu cycles, %llu uncached writes/atomics, %llu Gets checked; "
                    "holder violations %llu, monitor %llu, coherence/value %llu, trips %llu\n", masters, cmos ? " (with CBOs)" : "",
                    (unsigned long long)t.ops, (unsigned long long)t.cycles, (unsigned long long)t.puts, (unsigned long long)t.gets,
                    (unsigned long long)t.bad, (unsigned long long)t.viol, (unsigned long long)t.cv, (unsigned long long)t.trips);
    }

    const uint64_t X = 0x1c0;

    MsgPtr put(Kernel &k, uint64_t addr, uint64_t value, uint64_t src)
    {
        MsgPtr m = topo::make(Channel::A, unsigned(OpA::PutFullData), 3, addr, src, 8, DataMode::Full,
                              [value, addr](uint64_t x) { return (value >> (8 * (x - addr))) & 0xff; });
        m->id = k.newMsgId();
        return m;
    }
}

TEST(fuzz_2_masters) { sweep(2, false); }
TEST(fuzz_4_masters) { sweep(4, false); }
TEST(fuzz_4_masters_cmos) { sweep(4, true); }

TEST(directed_put_invalidates_branches)
{
    flow::Rig r;
    topo::Endpoint *u = new topo::Endpoint("u");
    r.build(2, "MESI", nullptr, {u});
    r.op(0, CpuOp::Load, X, 110);
    r.op(1, CpuOp::Load, X, 160);
    r.runTo(280);
    CHECK_EQ(r.mgr->blockState(X), std::string("B"));
    size_t m = r.mark();
    u->push(put(*r.k, X, 0x1122334455667788ull, 1), 300);
    r.op(0, CpuOp::Load, X, 400);         // the holder reads after the AccessAck
    r.settle();
    CHECK(r.sequence({"llc B + PutAtomic_u -> BI_u", "c0 S + ProbeBlock_toN -> I", "c1 S + ProbeBlock_toN -> I",
                      "llc BI_u + last_ProbeAck_toN -> I", "c0 I + Load -> IS_a", "llc I + AcquireBlock_NtoB -> T_g", "c0 IS_a + GrantData_toT -> E"}, m));
    auto acks = u->messages(Channel::D);
    CHECK_EQ(acks.size(), size_t(1));
    // the AccessAck is accepted after the manager received both ProbeAcks (the Execute cell)
    uint64_t exec = 0;
    for (auto &t : r.trans) if (t.agent == "llc" && t.from == "BI_u") exec = t.cycle;
    CHECK(!acks.empty() && acks[0].cycle > exec && exec > 0);
    CHECK_EQ(r.value(0, 1), uint64_t(0x1122334455667788ull));   // no stale read (§6.5)
    CHECK_EQ(r.violations(), uint64_t(0));
}

TEST(directed_get_probes_dirty_tip_toB)
{
    flow::Rig r;
    topo::Endpoint *u = new topo::Endpoint("u");
    r.build(2, "MESI", nullptr, {u});
    r.op(1, CpuOp::Store, X, 110);
    r.runTo(200);
    size_t m = r.mark();
    MsgPtr g = topo::make(Channel::A, unsigned(OpA::Get), 3, X, 2, 8, DataMode::Full);
    g->id = r.k->newMsgId();
    u->push(g, 300);
    r.settle();
    CHECK(r.sequence({"llc T + Get_u -> TB_u", "c1 M + ProbeBlock_toB -> S", "llc TB_u + ProbeAckData_toB -> B"}, m));
    auto d = u->messages(Channel::D);
    CHECK_EQ(d.size(), size_t(1));
    if (!d.empty())
    {
        uint64_t v = 0;
        for (unsigned b = 0; b < 8; b++) v |= uint64_t(d[0].m->data[b]) << (8 * b);   // X is 8-aligned on an 8-byte bus
        CHECK_EQ(v, r.value(1, 0));   // the dirty data, via the ProbeAckData
    }
    CHECK_EQ(r.cms[1]->lineState(X), std::string("S"));   // Branches stay (decision 8)
    CHECK_EQ(r.violations(), uint64_t(0));
}

TL_TEST_MAIN("TL-UNCACHED")
