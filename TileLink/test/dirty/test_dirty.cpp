/*
 * File  :      test_dirty.cpp
 * TL-DIRTY (docs/TileLink_LLC_Dirty_Writeback_Plan.md, DW-1 to DW-5): the v2 manager tables
 * (TLC_MESI_LLC_v2, TLC_MSI_LLC_v2) write a block back to memory on eviction only when the LLC's copy is
 * dirty (PutFullData_dirty_out). The LLC is one set of a few ways, so each case evicts the block it means
 * to; every PutFullData on the outer link is recorded, and each case checks exactly which blocks were
 * written, the memory's value afterwards, and Monitor and CoherenceCheck. Each case runs on MESI and MSI.
 *   DW-1 a block filled from memory and only read is evicted: no write
 *   DW-2 an L1's ReleaseData makes the LLC copy dirty (state I); evicted from I: written
 *   DW-3 a ProbeAckData answering the eviction's probe: written
 *   DW-4 dirty in the LLC, granted to another L1, released clean (dataless Release): still dirty, written
 *   DW-5 an uncached PutFullData performed in the LLC (Execute): written with the Put's bytes (the Put, an
 *        inner A, makes X the most recently used, so two clean evictions come first)
 */
#include "../flows/FlowTest.h"
#include "../topology/TopoTest.h"

using namespace octopus::tl;

namespace
{
    // X is in L1 set 3 (4 sets of 64-byte blocks); A, B, P, Q also; R, S, U in sets 0, 1, 2.
    const uint64_t X = 0x1c0, A = X + 0x100, B = X + 0x200, P = X + 0x300, Q = X + 0x400;
    const uint64_t R = X + 0x40, S = X + 0x80, U = X + 0xc0;

    struct Writes
    {
        std::vector<uint64_t> addr;   // PutFullData on the outer link, in order
        void attach(flow::Rig &r)
        {
            Link *l = r.outer;
            auto prev = l->chan(Channel::A).onAccept;
            l->chan(Channel::A).onAccept = [this, prev](const ChannelState &c, const Beat &b, uint64_t t)
            {
                if (prev) prev(c, b, t);
                if (b.index == 0 && b.msg->opcode == unsigned(OpA::PutFullData)) addr.push_back(b.msg->address & ~uint64_t(63));
            };
        }
    };

    void build(flow::Rig &r, const std::string &proto, unsigned ways, std::vector<Agent *> extra = {})
    {
        r.build(2, proto, nullptr, extra,
                {"table(s)=" + flow::table(proto == "MSI" ? "TLC_MSI_LLC_v2.csv" : "TLC_MESI_LLC_v2.csv"), "sets(i)=1",
                 "ways(i)=" + std::to_string(ways)});
    }
    uint64_t memWord(flow::Rig &r, uint64_t a) { return static_cast<RamSlave *>(r.mem)->readWord(a, 8); }
    uint64_t at(flow::Rig &r) { return r.k->cycle() + 10; }
    void step(flow::Rig &r, unsigned m, CpuOp::Kind k, uint64_t a) { r.op(m, k, a, at(r)); r.settle(); }

    // DW-1: fill X, read it, evict it (one way: Y takes X's way). The L1 holds it E (MESI) or S (MSI).
    void dw1(const std::string &proto)
    {
        flow::Rig r; build(r, proto, 1);
        Writes w; w.attach(r);
        step(r, 0, CpuOp::Load, X);
        step(r, 0, CpuOp::Load, R);   // evicts X from the LLC (and, inclusive, from c0)
        CHECK_EQ(r.mgr->blockState(X), std::string("N"));
        CHECK_EQ(w.addr.size(), size_t(0));
        CHECK_EQ(r.mgr->counters().clean_evictions, uint64_t(1));
        CHECK_EQ(r.violations(), uint64_t(0));
    }

    // DW-2: c0 stores X, then two loads in X's L1 set push X out of c0 (ReleaseData): LLC X in I, dirty.
    // Two more blocks fill the 4 ways; the next evicts the least recently acquired, X.
    void dw2(const std::string &proto)
    {
        flow::Rig r; build(r, proto, 4);
        Writes w; w.attach(r);
        step(r, 0, CpuOp::Store, X);
        uint64_t v = r.value(0, 0);
        step(r, 0, CpuOp::Load, A);
        step(r, 0, CpuOp::Load, B);   // c0 evicts X: ReleaseData_TtoN
        CHECK_EQ(r.cms[0]->lineState(X), std::string("I"));
        CHECK_EQ(r.mgr->blockState(X), std::string("I"));
        CHECK(w.addr.empty());
        step(r, 0, CpuOp::Load, R);   // fourth way
        step(r, 0, CpuOp::Load, S);   // evicts X (I, Replacement)
        CHECK_EQ(r.mgr->blockState(X), std::string("N"));
        CHECK_EQ(w.addr.size(), size_t(1));
        if (!w.addr.empty()) CHECK_EQ(w.addr[0], X);
        CHECK_EQ(memWord(r, X), v);
        CHECK_EQ(r.violations(), uint64_t(0));
    }

    // DW-3: c0 stores X (M); the eviction probes c0, which answers ProbeAckData: written.
    void dw3(const std::string &proto)
    {
        flow::Rig r; build(r, proto, 1);
        Writes w; w.attach(r);
        step(r, 0, CpuOp::Store, X);
        uint64_t v = r.value(0, 0);
        step(r, 0, CpuOp::Load, R);   // evicts X: probe, ProbeAckData_toN
        CHECK_EQ(r.mgr->blockState(X), std::string("N"));
        CHECK_EQ(w.addr.size(), size_t(1));
        if (!w.addr.empty()) CHECK_EQ(w.addr[0], X);
        CHECK_EQ(memWord(r, X), v);
        CHECK_EQ(r.mgr->counters().clean_evictions, uint64_t(0));
        CHECK_EQ(r.violations(), uint64_t(0));
    }

    // DW-4: as DW-2 to get X dirty in I; c1 loads X (MESI: GrantData_toT, E; MSI: GrantData_toB, S) and drops
    // it with a dataless Release (two loads in its set 3). X is still dirty. Five ways hold X, A, B, P, Q; R
    // and S evict A and B (clean: no writes), U evicts X: one write, X.
    void dw4(const std::string &proto)
    {
        flow::Rig r; build(r, proto, 5);
        Writes w; w.attach(r);
        step(r, 0, CpuOp::Store, X);
        uint64_t v = r.value(0, 0);
        step(r, 0, CpuOp::Load, A);
        step(r, 0, CpuOp::Load, B);   // c0: ReleaseData of X
        step(r, 1, CpuOp::Load, X);   // granted to c1
        CHECK_EQ(r.value(1, 0), v);
        step(r, 1, CpuOp::Load, P);
        step(r, 1, CpuOp::Load, Q);   // c1: a dataless Release of X
        CHECK_EQ(r.cms[1]->lineState(X), std::string("I"));
        CHECK_EQ(r.mgr->blockState(X), std::string("I"));
        CHECK(w.addr.empty());
        step(r, 1, CpuOp::Load, R);   // evicts A (clean)
        step(r, 1, CpuOp::Load, S);   // evicts B (clean)
        CHECK(w.addr.empty());
        step(r, 1, CpuOp::Load, U);   // evicts X (dirty)
        CHECK_EQ(r.mgr->blockState(X), std::string("N"));
        CHECK_EQ(w.addr.size(), size_t(1));
        if (!w.addr.empty()) CHECK_EQ(w.addr[0], X);
        CHECK_EQ(memWord(r, X), v);
        CHECK_EQ(r.mgr->counters().clean_evictions, uint64_t(2));
        CHECK_EQ(r.violations(), uint64_t(0));
    }

    // DW-5: X clean in I (loaded by c0, dropped with a dataless Release); an uncached PutFullData of 8 bytes
    // is performed in the LLC (I, PutAtomic_u: Execute): dirty; the eviction writes the Put's bytes.
    void dw5(const std::string &proto)
    {
        flow::Rig r;
        topo::Endpoint *u = new topo::Endpoint("u");
        build(r, proto, 4, {u});
        Writes w; w.attach(r);
        step(r, 0, CpuOp::Load, X);
        step(r, 0, CpuOp::Load, A);
        step(r, 0, CpuOp::Load, B);   // c0 drops X: a dataless Release
        CHECK_EQ(r.mgr->blockState(X), std::string("I"));
        MsgPtr m = topo::make(Channel::A, unsigned(OpA::PutFullData), 3, X, 1, 8, DataMode::Full, [](uint64_t x) { return 0xa0 + (x & 7); });
        m->id = r.k->newMsgId();
        u->push(m, at(r));
        r.settle();
        CHECK_EQ(r.mgr->blockState(X), std::string("I"));
        std::vector<uint8_t> b;
        CHECK(r.mgr->blockValue(X, 8, b));
        uint64_t put = 0;
        for (size_t i = 0; i < 8 && i < b.size(); i++) put |= uint64_t(b[i]) << (8 * i);
        CHECK_EQ(put, uint64_t(0xa7a6a5a4a3a2a1a0ull));
        CHECK(w.addr.empty());
        // the Put is an inner A, so it made X the most recently used: A and B go first (clean), then X
        step(r, 1, CpuOp::Load, R);   // fourth way
        step(r, 1, CpuOp::Load, S);   // evicts A
        step(r, 1, CpuOp::Load, U);   // evicts B
        CHECK(w.addr.empty());
        step(r, 1, CpuOp::Load, P);   // evicts X
        CHECK_EQ(r.mgr->blockState(X), std::string("N"));
        CHECK_EQ(w.addr.size(), size_t(1));
        if (!w.addr.empty()) CHECK_EQ(w.addr[0], X);
        CHECK_EQ(memWord(r, X), put);
        CHECK_EQ(r.mgr->counters().clean_evictions, uint64_t(2));
        CHECK_EQ(r.violations(), uint64_t(0));
    }
}

TEST(dirty_dw1_clean_eviction_not_written) { dw1("MESI"); dw1("MSI"); }
TEST(dirty_dw2_released_data_written) { dw2("MESI"); dw2("MSI"); }
TEST(dirty_dw3_probed_data_written) { dw3("MESI"); dw3("MSI"); }
TEST(dirty_dw4_dirty_survives_clean_release) { dw4("MESI"); dw4("MSI"); }
TEST(dirty_dw5_uncached_put_written) { dw5("MESI"); dw5("MSI"); }

TL_TEST_MAIN("TL-DIRTY")
