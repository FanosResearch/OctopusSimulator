// TL-ATOMIC (docs/TileLink_Review_Action_Plan.md problem 4): the atomic operations of §8.2.1-§8.2.2 on the
// whole 2^size-byte operand, in the shared helper (literal expected vectors, independent of the helper) and
// end to end: the real RamSlave executes, ValueCheck models, and the two agree. Stamp data: a stamp names a
// write, an atomic installs the operand's stamp (no arithmetic on stamps).
#include "TestMain.h"
#include "../fixtures/ScriptedMaster.h"
#include "tilelink/Atomic.h"
#include "tilelink/Kernel.h"
#include "tilelink/Monitor.h"
#include "tilelink/ValueCheck.h"
#include "tilelink/agents/RamSlave.h"
#include "tilelink/agents/Pipe.h"
#include "tilelink/agents/CacheMaster.h"
#include "tilelink/agents/CoherentManager.h"
#include "../fixtures/TlcSystem.h"

using namespace octopus::tl;
using namespace octopus::tl::fixture;

namespace
{
    std::vector<uint8_t> bytes(std::initializer_list<int> v) { std::vector<uint8_t> o; for (int x : v) o.push_back(uint8_t(x)); return o; }
    std::vector<uint8_t> rep(size_t n, uint8_t v) { return std::vector<uint8_t>(n, v); }
    const unsigned AR = unsigned(OpA::ArithmeticData), LG = unsigned(OpA::LogicalData);

    // a master and a RamSlave on one TL-UH link of w bytes, a Monitor and a ValueCheck on it
    struct Ram
    {
        Kernel k;
        ScriptedMaster *M; RamSlave *R; Link *l;
        std::unique_ptr<Monitor> mon;
        ValueCheck vc;
        std::vector<ValueViolation> v;
        std::map<uint64_t, MsgPtr> resp;   // source -> the D response
        DataMode mode;
        uint64_t src = 0;
        Ram(DataMode md, unsigned w) : k(KernelOptions{0, 1, md}), mode(md)
        {
            M = static_cast<ScriptedMaster *>(&k.add(std::unique_ptr<Agent>(new ScriptedMaster("M"))));
            Config c; c.set("latency(i)=1"); c.set("addr_size(i)=4096"); c.set("max_size(i)=6");
            R = static_cast<RamSlave *>(&k.add(std::unique_ptr<Agent>(new RamSlave("R", c))));
            LinkParams p; p.w = w; p.z = 3; p.o = 4; p.level = Level::UH;
            l = &k.connect("L0", p, *M, *R);
            MonitorOptions mo; mo.mode = MonitorOptions::Report; mo.print = false;
            mon.reset(new Monitor(k, *l, mo));
            vc.attach(*l);
            vc.onViolation = [this](const ValueViolation &x) { v.push_back(x); };
            auto prev = l->chan(Channel::D).onAccept;
            l->chan(Channel::D).onAccept = [this, prev](const ChannelState &ch, const Beat &b, uint64_t t) { if (prev) prev(ch, b, t); resp[b.msg->source] = b.msg; };
        }
        // a message carrying `val` (little-endian bytes, Full) or one stamp per group (Stamp)
        MsgPtr msg(unsigned op, unsigned size, uint64_t addr, const std::vector<uint8_t> &val, uint64_t stamp_v = 0, unsigned param = 0)
        {
            MsgPtr m = makeMsg(k, Channel::A, op, size, l->params().w, addr, src++, mode, param);
            if (mode == DataMode::Full)
                for (size_t i = 0; i < val.size(); i++)
                {
                    LanePos p = lanePos(*m, addr + i);
                    m->data[size_t(p.beat) * m->w + p.lane] = val[i];
                }
            else if (mode == DataMode::Stamp)
                for (auto &s : m->stamp) s = stamp_v;
            return m;
        }
        std::vector<uint8_t> got(const MsgPtr &d, uint64_t addr, size_t n) const
        {
            std::vector<uint8_t> o;
            for (size_t i = 0; i < n; i++) { LanePos p = lanePos(d->address, d->w, addr + i); o.push_back(d->data[size_t(p.beat) * d->w + p.lane]); }
            return o;
        }
        void run() { k.runUntilIdle(400); }
    };
}

TEST(helper_wide_add_carries_across_bytes)
{
    // 16 bytes: low half all 0xff + 1 carries into byte 8; whole-operand wraparound drops only the last carry
    std::vector<uint8_t> lo_ff = rep(8, 0xff); lo_ff.insert(lo_ff.end(), 8, 0);
    std::vector<uint8_t> one = rep(16, 0); one[0] = 1;
    std::vector<uint8_t> want = rep(16, 0); want[8] = 1;
    CHECK(applyAtomic(AR, unsigned(Arith::ADD), lo_ff, one) == want);
    CHECK(applyAtomic(AR, unsigned(Arith::ADD), rep(16, 0xff), one) == rep(16, 0));
    // 32 bytes: a carry through bytes 0..23 into byte 24
    std::vector<uint8_t> a32 = rep(24, 0xff); a32.insert(a32.end(), 8, 0x10);
    std::vector<uint8_t> one32 = rep(32, 0); one32[0] = 1;
    std::vector<uint8_t> w32 = rep(24, 0); w32.push_back(0x11); w32.insert(w32.end(), 7, 0x10);
    CHECK(applyAtomic(AR, unsigned(Arith::ADD), a32, one32) == w32);
}

TEST(helper_min_max_compare_the_whole_operand)
{
    // 16-byte values differing only in byte 15 (the sign byte) and in byte 9
    std::vector<uint8_t> neg = rep(16, 0); neg[15] = 0x80;          // the most negative
    std::vector<uint8_t> pos = rep(16, 0); pos[9] = 1;              // a small positive
    CHECK(applyAtomic(AR, unsigned(Arith::MIN), pos, neg) == neg);  // signed: neg < pos
    CHECK(applyAtomic(AR, unsigned(Arith::MAX), pos, neg) == pos);
    CHECK(applyAtomic(AR, unsigned(Arith::MINU), pos, neg) == pos); // unsigned: 0x80.. is the larger
    CHECK(applyAtomic(AR, unsigned(Arith::MAXU), pos, neg) == neg);
    // equal high bytes, decided by byte 9
    std::vector<uint8_t> big = pos; big[9] = 2;
    CHECK(applyAtomic(AR, unsigned(Arith::MAXU), pos, big) == big);
    CHECK(applyAtomic(AR, unsigned(Arith::MIN), big, pos) == pos);
}

TEST(helper_logical_is_per_byte)
{
    CHECK(applyAtomic(LG, unsigned(Logic::XOR), bytes({0x0f, 0xf0}), bytes({0xff, 0xff})) == bytes({0xf0, 0x0f}));
    CHECK(applyAtomic(LG, unsigned(Logic::OR), bytes({0x01, 0x00}), bytes({0x10, 0x02})) == bytes({0x11, 0x02}));
    CHECK(applyAtomic(LG, unsigned(Logic::AND), bytes({0x33, 0xff}), bytes({0x0f, 0x00})) == bytes({0x03, 0x00}));
    CHECK(applyAtomic(LG, unsigned(Logic::SWAP), bytes({1, 2}), bytes({3, 4})) == bytes({3, 4}));
    CHECK(applyAtomic(LG, unsigned(Logic::XOR), bytes({1, 2}), bytes({3})).empty());   // a length mismatch
}

TEST(wide_add_end_to_end_agrees_with_the_checker)
{
    // the review's reproduction: a 16-byte ADD over an 8-byte bus (two beats) carrying across byte 7
    for (unsigned w : {4u, 8u, 16u})
    {
        Ram r(DataMode::Full, w);
        std::vector<uint8_t> init = rep(8, 0xff); init.insert(init.end(), 8, 0);
        std::vector<uint8_t> one = rep(16, 0); one[0] = 1;
        r.M->send(1, Channel::A, r.msg(unsigned(OpA::PutFullData), 4, 0x40, init));
        r.M->send(20, Channel::A, r.msg(AR, 4, 0x40, one, 0, unsigned(Arith::ADD)));
        MsgPtr g = r.msg(unsigned(OpA::Get), 4, 0x40, {});
        r.M->send(40, Channel::A, g);
        r.run();
        std::vector<uint8_t> want = rep(16, 0); want[8] = 1;
        CHECK(r.resp.count(g->source) && r.got(r.resp[g->source], 0x40, 16) == want);   // RamSlave: byte 8 = 1
        CHECK_EQ(r.v.size(), size_t(0));                                                  // ValueCheck agrees
        CHECK_EQ(r.mon->violations(), uint64_t(0));
    }
}

TEST(signed_min_end_to_end)
{
    Ram r(DataMode::Full, 8);
    std::vector<uint8_t> pos = rep(16, 0); pos[9] = 1;
    std::vector<uint8_t> neg = rep(16, 0); neg[15] = 0x80;
    r.M->send(1, Channel::A, r.msg(unsigned(OpA::PutFullData), 4, 0x80, pos));
    r.M->send(20, Channel::A, r.msg(AR, 4, 0x80, neg, 0, unsigned(Arith::MIN)));
    r.M->send(40, Channel::A, r.msg(AR, 4, 0x80, pos, 0, unsigned(Arith::MAXU)));   // unsigned: neg (0x80..) stays
    MsgPtr g = r.msg(unsigned(OpA::Get), 4, 0x80, {});
    r.M->send(60, Channel::A, g);
    r.run();
    CHECK(r.resp.count(g->source) && r.got(r.resp[g->source], 0x80, 16) == neg);
    CHECK_EQ(r.v.size(), size_t(0));
    CHECK_EQ(r.mon->violations(), uint64_t(0));
}

TEST(stamp_atomic_installs_the_operand_stamp)
{
    // the review's reproduction: stamp 10, then ADD with operand stamp 5: RamSlave stores 5 (a stamp names a
    // write), the checker must expect 5, not 15; the old stamp returned (10) is checked
    for (unsigned w : {4u, 8u})
    {
        Ram r(DataMode::Stamp, w);
        r.M->send(1, Channel::A, r.msg(unsigned(OpA::PutFullData), 3, 0x40, {}, 10));
        MsgPtr a = r.msg(AR, 3, 0x40, {}, 5, unsigned(Arith::ADD));
        r.M->send(20, Channel::A, a);
        MsgPtr g = r.msg(unsigned(OpA::Get), 3, 0x40, {});
        r.M->send(40, Channel::A, g);
        r.run();
        CHECK(r.resp.count(a->source) && !r.resp[a->source]->stamp.empty() && r.resp[a->source]->stamp[0] == 10);
        CHECK(r.resp.count(g->source) && !r.resp[g->source]->stamp.empty() && r.resp[g->source]->stamp[0] == 5);
        CHECK_EQ(r.v.size(), size_t(0));
        CHECK_EQ(r.mon->violations(), uint64_t(0));
    }
}

TEST(stamp_sub_group_write_replaces_the_group)
{
    // a 4-byte stamp write replaces its whole 8-byte group (RamSlave): a read of the other half returns the
    // new stamp, which the checker must admit (it models the group, not only the written bytes)
    Ram r(DataMode::Stamp, 8);
    r.M->send(1, Channel::A, r.msg(unsigned(OpA::PutFullData), 3, 0x40, {}, 7));
    r.M->send(20, Channel::A, r.msg(unsigned(OpA::PutFullData), 2, 0x40, {}, 9));     // bytes 0x40..0x43
    MsgPtr g = r.msg(unsigned(OpA::Get), 2, 0x44, {});                                 // bytes 0x44..0x47
    r.M->send(40, Channel::A, g);
    r.run();
    CHECK(r.resp.count(g->source) && !r.resp[g->source]->stamp.empty() && r.resp[g->source]->stamp[0] == 9);
    CHECK_EQ(r.v.size(), size_t(0));
}

TEST(manager_executes_wide_atomics)
{
    // CoherentManager's Execute (TLC_Tables §3.3: I + PutAtomic_u): a CacheMaster loads block 0x40 and evicts it,
    // leaving the LLC copy with no holder (state I); an uncached master then writes 16 bytes, ADDs across byte 7,
    // takes a signed MIN and an unsigned MAXU of 16-byte values, and reads back. The manager executes all of it
    // (no outer write before the end); ValueCheck on the uncached link agrees
    for (unsigned w : {4u, 8u, 16u})
    {
        Kernel k(KernelOptions{100, 1, DataMode::Full});
        CacheMaster *c = static_cast<CacheMaster *>(&k.add(std::unique_ptr<Agent>(new CacheMaster("c0", tlc::cfg({"table(s)=" + tlc::table("TLC_MESI_L1.csv"), "cpu(s)=script", "sets(i)=2", "ways(i)=2"})))));
        ScriptedMaster *M = static_cast<ScriptedMaster *>(&k.add(std::unique_ptr<Agent>(new ScriptedMaster("M"))));
        CoherentManager *mgr = static_cast<CoherentManager *>(&k.add(std::unique_ptr<Agent>(new CoherentManager("llc", tlc::cfg({"table(s)=" + tlc::table("TLC_MESI_LLC.csv"), "sets(i)=2", "ways(i)=4"})))));
        RamSlave *R = static_cast<RamSlave *>(&k.add(std::unique_ptr<Agent>(new RamSlave("mem", tlc::cfg({"latency(i)=2"})))));
        LinkParams lc; lc.level = Level::C; lc.w = w; lc.o = 3; lc.i = 4; lc.z = 3;
        LinkParams lu; lu.level = Level::UH; lu.w = w; lu.o = 4; lu.z = 3;
        k.connect("L0", lc, *c, *mgr);
        Link &u = k.connect("U0", lu, *M, *mgr);
        k.connect("M0", lu, *mgr, *R);
        MonitorOptions mo; mo.mode = MonitorOptions::Report; mo.print = false;
        std::vector<std::unique_ptr<Monitor>> mons;
        for (const char *n : {"L0", "U0", "M0"}) mons.emplace_back(new Monitor(k, *k.link(n), mo));
        ValueCheck vc; std::vector<ValueViolation> vv;
        vc.onViolation = [&](const ValueViolation &x) { vv.push_back(x); };
        vc.attach(u);
        std::map<uint64_t, MsgPtr> resp;
        auto prev = u.chan(Channel::D).onAccept;
        u.chan(Channel::D).onAccept = [&, prev](const ChannelState &ch, const Beat &b, uint64_t t) { if (prev) prev(ch, b, t); resp[b.msg->source] = b.msg; };
        CpuOp ld; ld.kind = CpuOp::Load; ld.addr = 0x40; ld.bytes = 8; c->pushOp(ld);
        CpuOp ev; ev.kind = CpuOp::Evict; ev.addr = 0x40; ev.bytes = 8; ev.not_before = 300; c->pushOp(ev);
        uint64_t src = 0;
        auto msg = [&](unsigned op, const std::vector<uint8_t> &val, unsigned param) {
            MsgPtr m = makeMsg(k, Channel::A, op, 4, w, 0x40, src++, DataMode::Full, param);
            for (size_t i = 0; i < val.size(); i++) { LanePos p = lanePos(*m, 0x40 + i); m->data[size_t(p.beat) * m->w + p.lane] = val[i]; }
            return m;
        };
        std::vector<uint8_t> init = rep(8, 0xff); init.insert(init.end(), 8, 0);
        std::vector<uint8_t> one = rep(16, 0); one[0] = 1;
        std::vector<uint8_t> neg = rep(16, 0); neg[15] = 0x80;
        std::vector<uint8_t> big = rep(16, 0xff); big[15] = 0x7f;   // the largest signed, below 0x80.. unsigned
        M->send(1000, Channel::A, msg(unsigned(OpA::PutFullData), init, 0));
        MsgPtr add = msg(AR, one, unsigned(Arith::ADD));
        M->send(1100, Channel::A, add);
        MsgPtr g1 = msg(unsigned(OpA::Get), {}, 0);
        M->send(1200, Channel::A, g1);
        M->send(1300, Channel::A, msg(AR, neg, unsigned(Arith::MIN)));    // signed: neg is smaller, installed
        M->send(1400, Channel::A, msg(AR, big, unsigned(Arith::MAXU)));   // unsigned: 0x80.. is larger, stays
        MsgPtr g2 = msg(unsigned(OpA::Get), {}, 0);
        M->send(1500, Channel::A, g2);
        k.runUntilIdle(5000);
        std::vector<uint8_t> after_add = rep(16, 0); after_add[8] = 1;
        auto bytesOf = [&](const MsgPtr &m) { std::vector<uint8_t> o; for (size_t i = 0; i < 16; i++) { LanePos p = lanePos(m->address, m->w, 0x40 + i); o.push_back(m->data[size_t(p.beat) * m->w + p.lane]); } return o; };
        CHECK(resp.count(add->source) && bytesOf(resp[add->source]) == init);   // the old value
        CHECK(resp.count(g1->source) && bytesOf(resp[g1->source]) == after_add);
        CHECK(resp.count(g2->source) && bytesOf(resp[g2->source]) == neg);
        CHECK(mgr->counters().uncached >= 6);
        CHECK_EQ(mgr->counters().outer_writes, uint64_t(0));   // executed in the LLC copy, not forwarded
        CHECK_EQ(vv.size(), size_t(0));
        uint64_t mv = 0; for (auto &m : mons) mv += m->violations();
        CHECK_EQ(mv, uint64_t(0));
        mons.clear();
    }
}

TL_TEST_MAIN("TL-ATOMIC")
