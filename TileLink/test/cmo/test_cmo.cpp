/*
 * File  :      test_cmo.cpp
 * TL-CMO (plan §6): cache management operations at the CoherentManager (spec §8.2.3; plan §3.5;
 * docs/TileLink_TLC_Tables.md rows BN_i/TN_i, TB_c/B_w/I_w, BN_f/TN_f/N_w), sent as Intent by an
 * uncached master on its own TL-UH link, against 1, 2 and 3 holders, clean (E, or Branches over a clean
 * LLC copy) and dirty (M, or Branches over a dirty LLC copy after a probed writer). Checked:
 *   - the HintAck is accepted after every ProbeAck the operation caused and, for Clean and Flush, after
 *     the outer AccessAck of its write (§8.2.3: HintAck carries completion);
 *   - the permissions afterwards: Inval N everywhere, Clean at most B (no Tip left), Flush N;
 *   - memory: the dirty data is in memory after Clean and Flush, absent after Inval (discarded);
 *   - a racing Acquire is served after the HintAck (the CMO window blocks the block);
 *   - a racing voluntary ReleaseData (Figure 30 style) is merged: Flush keeps its data, Inval drops it;
 *   - a denied outer write gives a denied HintAck and keeps the copy;
 *   - Monitor and CoherenceCheck clean throughout.
 */
#include "../flows/FlowTest.h"
#include "../topology/TopoTest.h"

using namespace octopus::tl;

namespace
{
    const uint64_t X = 0x1c0;
    enum Cbo { Inval = 5, Clean = 6, Flush = 7 };
    const char *name(Cbo c) { return c == Inval ? "CBOInval" : c == Clean ? "CBOClean" : "CBOFlush"; }

    struct Probe
    {
        std::vector<uint64_t> probe_acks, outer_acks, hint_acks;   // accept cycles (block X, after `from`)
        std::vector<bool> hint_denied;
        uint64_t from = 0;
        void attach(flow::Rig &r)
        {
            for (auto &lp : r.k->links())
            {
                Link *l = lp.get();
                auto hook = [this, l](Channel ch)
                {
                    auto prev = l->chan(ch).onAccept;
                    l->chan(ch).onAccept = [this, prev, l, ch](const ChannelState &c, const Beat &b, uint64_t t)
                    {
                        if (prev) prev(c, b, t);
                        if (b.index != 0 || t < from) return;
                        const Msg &m = *b.msg;
                        const std::string &n = l->name();
                        if (ch == Channel::C && n[0] == 'G' && (m.opcode == unsigned(OpC::ProbeAck) || m.opcode == unsigned(OpC::ProbeAckData)) &&
                            (m.address & ~uint64_t(63)) == X)
                            probe_acks.push_back(t);
                        if (ch == Channel::D && n == "M" && m.opcode == unsigned(OpD::AccessAck)) outer_acks.push_back(t);
                        if (ch == Channel::D && n[0] == 'U' && m.opcode == unsigned(OpD::HintAck)) { hint_acks.push_back(t); hint_denied.push_back(m.denied); }
                    };
                };
                if (l->has(Channel::C)) hook(Channel::C);
                hook(Channel::D);
            }
        }
    };

    MsgPtr intent(Kernel &k, Cbo c, uint64_t src)
    {
        MsgPtr m = topo::make(Channel::A, unsigned(OpA::Intent), 6, X, src, 8, DataMode::Full);
        m->param = uint8_t(c);
        m->id = k.newMsgId();
        return m;
    }

    // Sets up `holders` (1..3) on X, clean or dirty; returns the dirty value (c0's store) or 0.
    uint64_t setup(flow::Rig &r, unsigned holders, bool dirty)
    {
        uint64_t t = 110;
        r.op(0, dirty ? CpuOp::Store : CpuOp::Load, X, t);
        for (unsigned h = 1; h < holders; h++) r.op(h, CpuOp::Load, X, t += 60);
        r.runTo(t + 120);
        return dirty ? r.value(0, 0) : 0;
    }

    void runCase(Cbo cbo, unsigned holders, bool dirty)
    {
        flow::Rig r;
        topo::Endpoint *u = new topo::Endpoint("u");
        r.build(3, "MESI", nullptr, {u});
        uint64_t v = setup(r, holders, dirty);
        const std::string before = r.mgr->blockState(X);
        Probe p;
        p.from = r.k->cycle();
        p.attach(r);
        const uint64_t T0 = r.k->cycle() + 10;
        u->push(intent(*r.k, cbo, 3), T0);
        r.settle();
        RamSlave *mem = static_cast<RamSlave *>(r.mem);
        std::string what = std::string(name(cbo)) + " " + std::to_string(holders) + (dirty ? " dirty" : " clean") + " from " + before;
        CHECK_EQ(p.hint_acks.size(), size_t(1));
        if (p.hint_acks.empty()) return;
        uint64_t h = p.hint_acks[0];
        CHECK(!p.hint_denied[0]);
        for (uint64_t a : p.probe_acks) CHECK(a < h);
        if (cbo != Inval) { CHECK_EQ(p.outer_acks.size(), size_t(1)); for (uint64_t a : p.outer_acks) CHECK(a < h); }
        else CHECK_EQ(p.outer_acks.size(), size_t(0));
        // the probes the table sends: Clean of Branches none (TLC_Tables §11.2), of a Tip one; Inval/Flush every holder
        size_t probes = cbo == Clean ? (before == "T" ? 1u : 0u) : holders;
        CHECK_EQ(p.probe_acks.size(), probes);
        for (unsigned m = 0; m < 3; m++)
        {
            std::string st = r.cms[m]->lineState(X);
            if (cbo == Clean) CHECK(st == "I" || st == "S");   // at most Branch
            else CHECK_EQ(st, std::string("I"));
        }
        std::string after = r.mgr->blockState(X);
        if (cbo == Clean) CHECK(after == "B" || after == "I");
        else CHECK_EQ(after, std::string("N"));
        uint64_t memv = mem->readWord(X, 8);
        if (dirty)
        {
            if (cbo == Inval) CHECK(memv != v);   // the dirty data is discarded
            else CHECK_EQ(memv, v);               // written outward before the HintAck
        }
        CHECK_EQ(r.violations(), uint64_t(0));
        std::printf("  %-28s probes %zu, outer writes %zu, HintAck at %llu, block %s -> %s\n", what.c_str(), p.probe_acks.size(), p.outer_acks.size(),
                    (unsigned long long)h, before.c_str(), after.c_str());
    }
}

TEST(cmo_holders_clean_and_dirty)
{
    for (Cbo c : {Inval, Clean, Flush})
        for (unsigned h = 1; h <= 3; h++)
            for (bool dirty : {false, true}) runCase(c, h, dirty);
}

TEST(cmo_racing_acquire_served_after_hintack)
{
    for (Cbo c : {Inval, Clean, Flush})
    {
        flow::Rig r;
        topo::Endpoint *u = new topo::Endpoint("u");
        r.build(2, "MESI", nullptr, {u});
        setup(r, 1, true);                       // c0 M
        size_t m = r.mark();
        uint64_t T0 = r.k->cycle() + 10;
        u->push(intent(*r.k, c, 3), T0);
        r.op(1, CpuOp::Store, X, T0 + 2);        // arrives while the CMO is in progress
        r.settle();
        size_t hint = 0, grant = 0, stall = 0;
        for (size_t i = m; i < r.trans.size(); i++)
        {
            const auto &t = r.trans[i];
            if (t.agent != "llc") continue;
            if (!hint && t.cell.find("HintAck") != std::string::npos && !t.stalled) hint = i;
            if (t.event.find("AcquireBlock") == 0 && t.stalled && !stall) stall = i;
            if (t.event.find("AcquireBlock") == 0 && !t.stalled && !grant) grant = i;
        }
        CHECK(hint > 0 && stall > 0 && grant > hint);   // parked in the CMO state, served after the HintAck
        CHECK_EQ(r.cms[1]->lineState(X), std::string("M"));
        CHECK_EQ(r.violations(), uint64_t(0));
    }
}

TEST(cmo_merged_voluntary_release)
{
    // c0 dirty; the CMO's Probe meets c0's ReleaseData (held on C so that it reaches the manager after
    // the probe was sent): TN_i / TN_f + ReleaseData_TtoN (TLC_Tables §3.4).
    for (Cbo c : {Inval, Flush})
    {
        flow::Rig r;
        topo::Endpoint *u = new topo::Endpoint("u");
        r.build(1, "MESI", nullptr, {u});
        uint64_t v = setup(r, 1, true);
        size_t m = r.mark();
        uint64_t T0 = r.k->cycle() + 10;
        r.close(0, Channel::C, T0, T0 + 40);
        r.op(0, CpuOp::Evict, X, T0);
        u->push(intent(*r.k, c, 3), T0);
        r.settle();
        std::string st = c == Inval ? "TN_i" : "TN_f";
        CHECK(r.sequence({"c0 M + Replacement -> MI_r", "llc T + Intent_" + std::string(name(c)) + " -> " + st, "c0 MI_r + ProbeBlock_toN -> Stall",
                          "llc " + st + " + ReleaseData_TtoN -> " + st, "c0 MI_r + ReleaseAck -> I", "c0 I + ProbeBlock_toN -> I",
                          "llc " + st + " + last_ProbeAck_toN -> " + std::string(c == Inval ? "N" : "N_w")}, m));
        uint64_t memv = static_cast<RamSlave *>(r.mem)->readWord(X, 8);
        if (c == Flush) CHECK_EQ(memv, v); else CHECK(memv != v);
        CHECK_EQ(r.mgr->blockState(X), std::string("N"));
        CHECK_EQ(r.violations(), uint64_t(0));
    }
}

TEST(cmo_denied_outer_write)
{
    // A memory that denies writes only: the CBO's PutFullData is denied, so the HintAck is denied and
    // the LLC copy is kept (B_w / I_w / N_w + AccessAck_denied).
    for (Cbo c : {Clean, Flush})
    {
        flow::Rig r;
        topo::Endpoint *u = new topo::Endpoint("u");
        topo::MemSlave *ms = new topo::MemSlave("mem");
        ms->latency = 2;
        ms->mutate = [](Msg &m) { if (m.opcode == unsigned(OpD::AccessAck)) m.denied = true; };
        r.build(1, "MESI", ms, {u});
        setup(r, 1, true);
        Probe p;
        p.from = r.k->cycle();
        p.attach(r);
        u->push(intent(*r.k, c, 3), r.k->cycle() + 10);
        r.settle();
        CHECK_EQ(p.hint_acks.size(), size_t(1));
        CHECK(!p.hint_denied.empty() && p.hint_denied[0]);
        // the copy kept: Clean B_w + AccessAck_denied -> B (the probed owner kept its Branch); Flush N_w -> I
        CHECK_EQ(r.mgr->blockState(X), std::string(c == Clean ? "B" : "I"));
        CHECK_EQ(r.cms[0]->lineState(X), std::string(c == Clean ? "S" : "I"));
        std::vector<uint8_t> b;
        CHECK(r.mgr->blockValue(X, 8, b));
        CHECK_EQ(r.violations(), uint64_t(0));
    }
}

TL_TEST_MAIN("TL-CMO")
