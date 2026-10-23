// TL-POISON (docs/TileLink_Review_Action_Plan.md problem 2, standalone half): corruption (§4.4) survives the
// TL-C agents' storage. A poisoned byte in RAM reaches a cache as a corrupt GrantData beat (only the beats that
// carry poisoned bytes), a Load consuming it completes as an access error, the poison survives a ReleaseData,
// the manager's copy, a Grant to another master and the manager's eviction to RAM, and a clean write clears
// only the bytes it writes (in the cache and, per byte, in RAM). Full and Stamp data, link widths 4, 8, 16.
#include "TestMain.h"
#include "../fixtures/TlcSystem.h"
#include "../fixtures/ScriptedMaster.h"
#include "tilelink/agents/RamSlave.h"
#include <map>

using namespace octopus::tl;
using namespace tlc;

namespace
{
    // the corrupt flags of every GrantData beat accepted on a link, per message
    struct GrantBeats
    {
        std::map<uint64_t, std::vector<int>> per_msg;   // msg id -> corrupt per beat
        void attach(Link &l)
        {
            ChannelState &d = l.chan(Channel::D);
            auto prev = d.onAccept;
            d.onAccept = [this, prev](const ChannelState &c, const Beat &b, uint64_t t) {
                if (prev) prev(c, b, t);
                if (b.msg->opcode == unsigned(OpD::GrantData)) per_msg[b.msg->id].push_back(b.msg->beatCorrupt(b.index) ? 1 : 0);
            };
        }
        std::vector<int> last() const { return per_msg.empty() ? std::vector<int>() : per_msg.rbegin()->second; }
    };
    CpuOp op(CpuOp::Kind k, uint64_t addr, unsigned bytes, uint64_t not_before = 0)
    {
        CpuOp o; o.kind = k; o.addr = addr; o.bytes = bytes; o.not_before = not_before; return o;
    }
    Options base(unsigned masters, unsigned w, DataMode mode)
    {
        Options o; o.masters = masters; o.cpu_script = true; o.throttle = false; o.w = w; o.mode = mode;
        o.mem_jitter = 0; o.llc_sets = 1; o.llc_ways = 1;   // a second block evicts the first from the LLC
        return o;
    }
    bool clean(System &s) { return s.monitorViolations() == 0 && s.cviol.empty() && s.vviol.empty(); }
    const CpuOp *done(const CacheMaster *c, uint64_t addr, unsigned bytes, CpuOp::Kind k)
    {
        for (auto &o : c->finished()) if (o.addr == addr && o.bytes == bytes && o.kind == k) return &o;
        return nullptr;
    }
}

TEST(grantdata_carries_poison_on_the_beats_that_hold_it)
{
    for (DataMode mode : {DataMode::Full, DataMode::Stamp})
        for (unsigned w : {4u, 8u, 16u})
        {
            System s; Options o = base(1, w, mode); s.build(o);
            GrantBeats gb; gb.attach(*s.k->link("L0"));
            s.mem->poisonByte(0x13, true);
            s.cms[0]->pushOp(op(CpuOp::Load, 0x00, 8));            // clean bytes of the block
            s.cms[0]->pushOp(op(CpuOp::Load, 0x10, 8, 200));       // the poisoned byte
            s.run(100000);
            CHECK(s.allDone());
            std::vector<int> g = gb.last();
            CHECK_EQ(g.size(), size_t(64 / w));
            for (size_t k = 0; k < g.size(); k++) CHECK_EQ(g[k], int(k == 0x13 / w));   // only the beat carrying 0x13
            const CpuOp *a = done(s.cms[0], 0x00, 8, CpuOp::Load), *b = done(s.cms[0], 0x10, 8, CpuOp::Load);
            CHECK(a && !a->error);                                  // a clean byte range reads clean
            CHECK(b && b->error);                                   // the poisoned one is an access error
            CHECK_EQ(s.cms[0]->counters().poisoned_loads, uint64_t(1));
            CHECK(clean(s));
        }
}

TEST(poison_survives_release_and_a_second_master)
{
    System s; Options o = base(2, 8, DataMode::Full); s.build(o);
    GrantBeats g1; g1.attach(*s.k->link("L1"));
    s.mem->poisonByte(0x13, true);
    s.cms[0]->pushOp(op(CpuOp::Load, 0x00, 8));
    s.cms[0]->pushOp(op(CpuOp::Evict, 0x00, 8, 300));            // Release: the poisoned copy goes back to the manager
    s.cms[1]->pushOp(op(CpuOp::Load, 0x10, 8, 1000));            // from the manager's copy
    s.run(100000);
    CHECK(s.allDone());
    std::vector<int> g = g1.last();
    CHECK_EQ(g.size(), size_t(8));
    if (g.size() == 8) CHECK_EQ(g[2], 1);
    const CpuOp *b = done(s.cms[1], 0x10, 8, CpuOp::Load);
    CHECK(b && b->error);
    CHECK(clean(s));
}

TEST(poison_survives_the_managers_eviction_to_ram)
{
    System s; Options o = base(1, 8, DataMode::Full); s.build(o);
    s.mem->poisonByte(0x13, true);
    s.cms[0]->pushOp(op(CpuOp::Load, 0x00, 8));
    s.cms[0]->pushOp(op(CpuOp::Evict, 0x00, 8, 300));
    s.cms[0]->pushOp(op(CpuOp::Load, 0x40, 8, 600));            // another block: the one-way LLC evicts block 0
    s.run(100000);
    CHECK(s.allDone());
    // written back corrupt. §4.4's corrupt is per beat, so the write-back marks the whole beat that carries
    // 0x13 and RAM poisons every byte of it; the other beats of the block stay clean
    for (uint64_t x = 0x10; x < 0x18; x++) CHECK(s.mem->poisoned(x));
    CHECK(!s.mem->poisoned(0x0f)); CHECK(!s.mem->poisoned(0x18));
    CHECK(clean(s));
}

TEST(a_clean_store_clears_only_its_bytes)
{
    // in the cache: 0x10..0x17 poisoned in RAM; the line is fetched, a 1-byte Store at 0x13 makes 0x13 valid
    System s; Options o = base(1, 8, DataMode::Full); s.build(o);
    for (uint64_t x = 0x10; x < 0x18; x++) s.mem->poisonByte(x, true);
    s.cms[0]->pushOp(op(CpuOp::Store, 0x13, 1));
    s.cms[0]->pushOp(op(CpuOp::Load, 0x13, 1, 300));
    s.cms[0]->pushOp(op(CpuOp::Load, 0x12, 1, 300));
    s.cms[0]->pushOp(op(CpuOp::Evict, 0x00, 8, 600));
    s.cms[0]->pushOp(op(CpuOp::Load, 0x40, 8, 900));            // the LLC writes block 0 back to RAM
    s.run(100000);
    CHECK(s.allDone());
    const CpuOp *l13 = done(s.cms[0], 0x13, 1, CpuOp::Load), *l12 = done(s.cms[0], 0x12, 1, CpuOp::Load);
    CHECK(l13 && !l13->error);                                   // the stored byte is valid
    CHECK(l12 && l12->error);                                    // its neighbour is still poisoned
    // after the write-back the beat 0x10..0x17 still carries poisoned bytes, so it is corrupt on the wire and
    // RAM keeps the whole beat poisoned (per-beat corrupt, §4.4); the precision that survives is the cache's
    CHECK(s.mem->poisoned(0x12)); CHECK(s.mem->poisoned(0x17)); CHECK(!s.mem->poisoned(0x18));
    CHECK(clean(s));
}

TEST(ram_partial_clean_write_clears_only_its_bytes)
{
    // RamSlave per byte: a corrupt PutFullData of 0x10..0x17, then a clean 1-byte PutPartialData at 0x13. Only
    // 0x13 is valid again; a Get of the beat is still corrupt (its other bytes), a Get of 0x13 alone is not.
    // (With 8-byte poison groups the 1-byte write cleared the whole group: corruption laundered.)
    using namespace octopus::tl::fixture;
    Kernel k(KernelOptions{0, 1, DataMode::Full});
    ScriptedMaster *M = static_cast<ScriptedMaster *>(&k.add(std::unique_ptr<Agent>(new ScriptedMaster("M"))));
    RamSlave *R = static_cast<RamSlave *>(&k.add(std::unique_ptr<Agent>(new RamSlave("R", tlc::cfg({"latency(i)=1", "addr_size(i)=4096"})))));
    LinkParams p; p.w = 8; p.z = 3; p.o = 3; p.level = Level::UH;
    Link &l = k.connect("L0", p, *M, *R);
    MonitorOptions mo; mo.mode = MonitorOptions::Report; mo.print = false;
    Monitor mon(k, l, mo);
    MsgPtr put = makeMsg(k, Channel::A, unsigned(OpA::PutFullData), 3, 8, 0x10, 0, DataMode::Full);
    setData(*put, 0, 0x1111111111111111ull); put->corrupt[0] = 1;
    MsgPtr part = makeMsg(k, Channel::A, unsigned(OpA::PutPartialData), 0, 8, 0x13, 1, DataMode::Full);
    setData(*part, 0, 0x22);
    MsgPtr g8 = makeMsg(k, Channel::A, unsigned(OpA::Get), 3, 8, 0x10, 2, DataMode::Full);
    MsgPtr g1 = makeMsg(k, Channel::A, unsigned(OpA::Get), 0, 8, 0x13, 3, DataMode::Full);
    MsgPtr d8, d1;
    l.chan(Channel::D).onAccept = [&](const ChannelState &, const Beat &b, uint64_t) {
        if (b.msg->source == 2) d8 = b.msg;
        if (b.msg->source == 3) d1 = b.msg;
    };
    M->send(1, Channel::A, put);
    M->send(10, Channel::A, part);
    M->send(20, Channel::A, g8);
    M->send(30, Channel::A, g1);
    k.runUntilIdle(200);
    CHECK(!R->poisoned(0x13)); CHECK(R->poisoned(0x12)); CHECK(R->poisoned(0x14));
    CHECK(d8 && d8->beatCorrupt(0));                             // the beat still carries poisoned bytes
    CHECK(d1 && !d1->beatCorrupt(0));                            // the byte the clean write established
    CHECK_EQ(mon.violations(), uint64_t(0));
}

TL_TEST_MAIN("TL-POISON")
