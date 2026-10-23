// TL-TOPO test_forwarded (docs/TileLink_Review_Followup.md R2): an uncached access the CoherentManager forwards to
// memory (N_u, ForwardIn) is reported to CoherenceCheck at its response. Corruption (§4.4) is honoured there as in
// the manager's own copy: a corrupt response beat's bytes are not compared, a clean beat of the same response
// still is; a corrupt write's bytes, and an atomic's result from corrupt input, become unknown to the checker
// (any value admitted until the next clean store), they are not left at the previous value. An unflagged wrong
// value is still caught.
#include "TestMain.h"
#include "TopoTest.h"
#include "tilelink/CoherenceCheck.h"
#include "tilelink/agents/CoherentManager.h"
#include "tilelink/agents/RamSlave.h"

using namespace topo;

namespace
{
    // an uncached endpoint -> CoherentManager -> RamSlave, eight bytes at 0..15 initialised to 0x42 (RAM and the
    // checker's initial image); `edit` changes each beat the RAM offers on D
    struct Sys
    {
        Kernel k{opts()};
        Endpoint *u; CoherentManager *mgr; RamSlave *ram; Link *outer;
        CoherenceCheck cc;
        std::vector<CoherenceViolation> v;
        std::function<void(const Beat &)> edit;
        Sys()
        {
            u = endpoint(k, "uncached");
            mgr = addAgent(k, new CoherentManager("manager", cfg({})));
            ram = addAgent(k, new RamSlave("ram", cfg({"level(s)=UH"})));
            k.connect("U", lp(8), *u, *mgr);
            outer = &k.connect("M", lp(8), *mgr, *ram);
            uint8_t init[16]; for (auto &b : init) b = 0x42;
            for (unsigned i = 0; i < 16; i++) ram->writeByte(i, 0x42);
            k.addObserver(&cc); cc.setInitial(0, init, 16);
            cc.onViolation = [this](const CoherenceViolation &x) { v.push_back(x); };
            outer->chan(Channel::D).onOffer = [this](const ChannelState &, const Beat &b, uint64_t) { if (edit) edit(b); };
        }
        MsgPtr get(uint64_t src, unsigned size = 3) { return make(Channel::A, unsigned(OpA::Get), size, 0, src, 8); }
        MsgPtr put(uint64_t src, uint8_t val) { return make(Channel::A, unsigned(OpA::PutFullData), 3, 0, src, 8, DataMode::Full, [val](uint64_t) { return val; }); }
        unsigned corruptBeats(uint64_t src) const
        {
            unsigned n = 0;
            for (auto &g : u->got) if (g.ch == Channel::D && g.m->source == src) n += g.m->beatCorrupt(g.beat);
            return n;
        }
        bool answered(uint64_t src) const { for (auto &g : u->got) if (g.ch == Channel::D && g.m->source == src) return true; return false; }
    };
}

TEST(corrupt_get_is_not_compared_clean_beat_is)
{
    // a 16-byte Get over two beats: memory's beat 0 comes back corrupt with byte 0 changed to 0x99; beat 1 is clean
    Sys s;
    s.edit = [](const Beat &b) { if (b.msg->opcode == unsigned(OpD::AccessAckData) && b.index == 0) { b.msg->corrupt[0] = 1; b.msg->data[0] = 0x99; } };
    s.u->push(s.get(0, 4));
    s.k.run(60);
    CHECK(s.answered(0));
    CHECK_EQ(s.corruptBeats(0), 1u);                              // forwarded with its flag
    CHECK_EQ(s.v.size(), size_t(0));                              // its bytes are not compared
    CHECK_EQ(s.cc.counters().bytes_checked, uint64_t(8));         // the clean beat's eight bytes are
}

TEST(unflagged_wrong_value_still_caught)
{
    Sys s;
    s.edit = [](const Beat &b) { if (b.msg->opcode == unsigned(OpD::AccessAckData)) b.msg->data[0] = 0x99; };
    s.u->push(s.get(0));
    s.k.run(60);
    CHECK_EQ(s.v.size(), size_t(1));
    if (!s.v.empty()) CHECK_EQ(s.v[0].rule, std::string("C-VALUE"));
}

TEST(corrupt_put_makes_its_bytes_unknown)
{
    // a Put of 0x99 whose beat is corrupt (RAM stores and poisons it), then a Get that memory answers clean (the
    // flag removed on the wire): the checker admits 0x99, its value is unknown, not still the initial 0x42. A
    // clean Put of 0x55 then makes the bytes known again
    Sys s;
    MsgPtr p = s.put(0, 0x99); p->corrupt[0] = 1;
    s.u->push(p);
    s.edit = [](const Beat &b) { if (b.msg->opcode == unsigned(OpD::AccessAckData)) for (auto &c : b.msg->corrupt) c = 0; };
    s.u->push(s.get(1), 20);
    s.u->push(s.put(2, 0x55), 40);
    s.u->push(s.get(3), 60);
    s.k.run(120);
    CHECK(s.answered(1) && s.answered(3));
    CHECK_EQ(s.v.size(), size_t(0));
    CHECK_EQ(s.cc.counters().bytes_checked, uint64_t(8));         // only the last Get's bytes (0x55, known)
}

TEST(corrupt_atomic_old_value_and_result)
{
    // an 8-byte ADD: memory returns its old value corrupt and changed (not compared), and the result, computed from
    // invalid input, is unknown: a later Get answered clean with any value raises nothing
    Sys s;
    MsgPtr a = make(Channel::A, unsigned(OpA::ArithmeticData), 3, 0, 0, 8, DataMode::Full, [](uint64_t x) { return x == 0 ? 1 : 0; });
    a->param = unsigned(Arith::ADD);
    s.u->push(a);
    s.edit = [](const Beat &b) {
        if (b.msg->opcode != unsigned(OpD::AccessAckData)) return;
        if (b.msg->source == 0) { b.msg->corrupt[0] = 1; b.msg->data[0] = 0x07; }        // the atomic's old value
        else { for (auto &c : b.msg->corrupt) c = 0; b.msg->data[0] = 0x13; }            // the later Get
    };
    s.u->push(s.get(1), 30);
    s.k.run(100);
    CHECK(s.answered(0) && s.answered(1));
    CHECK_EQ(s.v.size(), size_t(0));
}

TL_TEST_MAIN("TL-FORWARDED")
