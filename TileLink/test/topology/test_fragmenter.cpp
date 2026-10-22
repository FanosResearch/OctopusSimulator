/*
 * File  :      test_fragmenter.cpp
 * TL-TOPO: Fragmenter (plan §3.5). Get/PutFullData/PutPartialData split into max_size
 * fragments in address order with the fragment index in the high source bits; responses
 * (answered out of order, §6.5) reassembled in address order; mixed sizes (small ones pass
 * through unchanged); the upstream ID held until the last fragment's response; fragments
 * smaller than the bus; TL-ADAPT-ERR: one fragment denied gives a denied response corrupt on
 * that fragment's beats; the elaboration errors (an atomic larger than max_size, o too small).
 */
#include "TopoTest.h"
#include "tilelink/agents/Fragmenter.h"

using namespace topo;

namespace
{
    struct Frag
    {
        Kernel k;
        Endpoint *m;
        Fragmenter *f;
        MemSlave *r;
        Link *u, *d;
        // upstream o=2, z=3 (sizes up to 7): k = 7 - max_size index bits, downstream o = 2 + k
        Frag(unsigned w, unsigned max_size, std::vector<const char *> extra = {}) : k(opts())
        {
            m = endpoint(k, "m");
            Config c;
            c.set("max_size(i)=" + std::to_string(max_size));
            c.set("max_unsplit(i)=" + std::to_string(max_size));
            for (auto e : extra) c.set(e);
            f = addAgent(k, new Fragmenter("F", c));
            r = memslave(k, "r");
            r->lifo = true;                                    // answer newest first: out of order (§6.5)
            u = &k.connect("U", lp(w, Level::UH, 2), *m, *f);
            d = &k.connect("D", lp(w, Level::UH, 2 + 7 - max_size), *f, *r);
        }
        // the latest response the master received for that source (null if none)
        MsgPtr resp(uint64_t src) const
        {
            MsgPtr r;
            for (auto &g : m->messages(Channel::D)) if (g.m->source == src) r = g.m;
            return r;
        }
    };
    MsgPtr put(unsigned size, uint64_t a, uint64_t src, unsigned w, std::function<uint64_t(uint64_t)> fill = lowByte)
    {
        return make(Channel::A, unsigned(OpA::PutFullData), size, a, src, w, DataMode::Full, fill);
    }
    MsgPtr get(unsigned size, uint64_t a, uint64_t src, unsigned w) { return make(Channel::A, unsigned(OpA::Get), size, a, src, w); }
    // every byte of a data response, placed by the request's address (§4.5)
    bool bytesAre(const Msg &r, uint64_t addr, std::function<uint64_t(uint64_t)> want)
    {
        for (uint64_t x = addr; x < addr + r.bytes(); x++)
        {
            LanePos p = lanePos(addr, r.w, x);
            if (size_t(p.beat) * r.w + p.lane >= r.data.size()) return false;
            if (r.data[size_t(p.beat) * r.w + p.lane] != uint8_t(want(x))) return false;
        }
        return true;
    }
}

TEST(split_put_and_get_reassembled_in_address_order)
{
    Frag t(8, 3);
    CHECK_EQ(t.f->indexBits(), 4u);
    t.m->push(put(5, 0x40, 1, 8));
    t.m->push(get(5, 0x40, 2, 8), 20);
    CHECK(t.k.runUntilIdle(200) < 200);
    // the slave saw 4 Puts then 4 Gets of 8 bytes, in address order, index n-1-f in bits 2..
    CHECK_EQ(t.r->requests.size(), size_t(8));
    for (size_t i = 0; i < t.r->requests.size(); i++)
    {
        const Msg &q = *t.r->requests[i];
        uint64_t f = i % 4, src = i < 4 ? 1 : 2;
        CHECK_EQ(q.size, uint8_t(3));
        CHECK_EQ(q.address, 0x40 + 8 * f);
        CHECK_EQ(q.source, src | ((3 - f) << 2));
        CHECK_EQ(q.opcode, uint8_t(i < 4 ? 0 : 4));
    }
    for (uint64_t x = 0x40; x < 0x60; x++) CHECK_EQ(t.r->byteAt(x), x & 0xff);
    auto p = t.resp(1), g = t.resp(2);
    CHECK(p && g);
    if (p) { CHECK_EQ(p->opcode, uint8_t(OpD::AccessAck)); CHECK_EQ(p->size, uint8_t(5)); CHECK(!p->denied); }
    if (g)
    {
        CHECK_EQ(g->opcode, uint8_t(OpD::AccessAckData));
        CHECK_EQ(g->size, uint8_t(5));
        CHECK_EQ(g->beats(), 4u);
        CHECK(bytesAre(*g, 0x40, lowByte));
    }
    CHECK_EQ(t.m->messages(Channel::D).size(), size_t(2));
    CHECK_EQ(t.f->fragmentsOutstanding(), uint64_t(0));
}

TEST(mixed_sizes_small_ones_pass_through)
{
    Frag t(8, 3);
    auto val = [](uint64_t x) { return (x * 7 + 3) & 0xff; };
    t.m->push(put(6, 0x100, 0, 8, val));                                 // 8 fragments
    t.m->push(put(2, 0x204, 1, 8, val));                                 // passes, sub-bus
    t.m->push(put(3, 0x208, 2, 8, val));                                 // passes, exactly max_size
    MsgPtr add = make(Channel::A, unsigned(OpA::ArithmeticData), 3, 0x208, 3, 8, DataMode::Full, [](uint64_t x) { return x == 0x208 ? 5u : 0u; });
    add->param = uint8_t(Arith::ADD);
    t.m->push(add, 40);                                                   // an atomic of max_size: passes
    t.m->push(get(4, 0x200, 1, 8), 60);                                  // 2 fragments
    t.m->push(get(6, 0x100, 0, 8), 60);
    CHECK(t.k.runUntilIdle(400) < 400);
    // pass-through messages are the very same Msg objects, source unchanged
    bool same = false;
    for (auto &q : t.r->requests) if (q->address == 0x204) { same = q->source == 1 && q->size == 2; }
    CHECK(same);
    auto g16 = t.resp(1), g64 = t.resp(0), a = t.resp(3);
    CHECK(g16 && g64 && a);
    if (a) CHECK_EQ(unsigned(a->data[0]), unsigned(val(0x208)));       // the old value
    if (g64) CHECK(bytesAre(*g64, 0x100, val));
    if (g16)
    {
        // 0x200..0x203 never written (0); 0x204..0x207 the sub-bus Put; 0x208 the ADD result
        CHECK(bytesAre(*g16, 0x200, [&](uint64_t x) -> uint64_t
        {
            if (x < 0x204) return 0;
            if (x == 0x208) return (val(0x208) + 5) & 0xff;
            return val(x);
        }));
    }
    CHECK_EQ(t.m->messages(Channel::D).size(), size_t(6));
}

TEST(id_held_until_last_fragment_answered)
{
    // Plan §3.5: the upstream source stays outstanding until the reassembled response, which
    // follows the last fragment's response. Watched cycle by cycle.
    Frag t(8, 3);
    t.r->latency = 3;
    t.m->push(get(6, 0x0, 1, 8));
    bool ok = true;
    uint64_t last_frag_resp = 0, up_resp = 0;
    for (int c = 0; c < 200 && t.m->messages(Channel::D).empty(); c++)
    {
        t.k.step();
        const Beat &b = t.d->chan(Channel::D).accepted();
        if (b.msg && b.last()) last_frag_resp = t.k.cycle() - 1;
        if (!t.m->messages(Channel::D).empty()) up_resp = t.m->messages(Channel::D)[0].cycle;
        if (!t.r->outstanding.empty() && !t.m->messages(Channel::D).empty()) ok = false;
    }
    CHECK(ok);
    CHECK(up_resp > last_frag_resp);
    CHECK_EQ(t.r->requests.size(), size_t(8));
    CHECK(t.r->max_outstanding >= 2);                                     // the fragments overlapped downstream
}

TEST(fragments_smaller_than_the_bus)
{
    // w = 16, max_size 2: a 16-byte Get/Put (one beat) becomes four 4-byte operations, each on
    // its own lanes (§4.5); a PutPartialData keeps its holes.
    Frag t(16, 2);
    auto val = [](uint64_t x) { return (x ^ 0x5a) & 0xff; };
    MsgPtr pp = make(Channel::A, unsigned(OpA::PutPartialData), 4, 0x30, 0, 16, DataMode::Full, val);
    for (unsigned l = 0; l < 16; l++) pp->mask[l] = uint8_t(l % 2);
    t.m->push(put(4, 0x30, 1, 16, lowByte));
    t.m->push(pp, 10);
    t.m->push(get(4, 0x30, 2, 16), 30);
    CHECK(t.k.runUntilIdle(300) < 300);
    std::vector<uint64_t> addrs;
    for (auto &q : t.r->requests) addrs.push_back(q->address);
    std::vector<uint64_t> expect = {0x30, 0x34, 0x38, 0x3c, 0x30, 0x34, 0x38, 0x3c, 0x30, 0x34, 0x38, 0x3c};
    CHECK(addrs == expect);
    if (t.r->requests.size() == 12) CHECK_EQ(t.r->requests[5]->maskHex(0), std::string("0x00a0"));   // lanes 5, 7 of the 2nd fragment
    auto g = t.resp(2);
    CHECK(g != nullptr);
    if (g) CHECK(bytesAre(*g, 0x30, [&](uint64_t x) { return (x - 0x30) % 2 ? val(x) : (x & 0xff); }));
}

TEST(adapt_err_one_fragment_denied)
{
    // TL-ADAPT-ERR: fragment 2 of 4 denied. With deny_corrupts_all=0 (the non-conforming
    // opt-out) the reassembled response is denied (OR) and corrupt on that fragment's beat only,
    // as returned; the other beats carry their data. The Put shows the recorded semantic gap:
    // fragments 0, 1, 3 were written.
    Frag t(8, 3, {"deny_corrupts_all(i)=0"});
    t.r->deny.push_back({0x50, 8});
    t.m->push(put(5, 0x40, 1, 8));
    t.m->push(get(5, 0x40, 2, 8), 20);
    CHECK(t.k.runUntilIdle(200) < 200);
    auto p = t.resp(1), g = t.resp(2);
    CHECK(p && g);
    if (p) CHECK(p->denied);
    if (g)
    {
        CHECK(g->denied);
        for (unsigned k = 0; k < 4; k++) CHECK_EQ(g->beatCorrupt(k), k == 2);
        CHECK_EQ(unsigned(g->data[8]), 0x48u);
    }
    CHECK_EQ(t.r->byteAt(0x40), uint64_t(0x40));
    CHECK_EQ(t.r->byteAt(0x50), uint64_t(0));                              // the denied fragment had no effect
    CHECK_EQ(t.r->byteAt(0x58), uint64_t(0x58));
    // the default: every beat of a denied data response corrupt (§4.4 for the whole message)
    Frag a(8, 3);
    a.r->deny.push_back({0x50, 8});
    a.m->push(get(5, 0x40, 2, 8));
    CHECK(a.k.runUntilIdle(200) < 200);
    auto ga = a.resp(2);
    CHECK(ga != nullptr);
    if (ga) { CHECK(ga->denied); for (unsigned k = 0; k < 4; k++) CHECK(ga->beatCorrupt(k)); }
}

TEST(corrupt_per_beat_as_returned)
{
    Frag t(8, 3);
    t.r->corrupt_at.insert(0x48);
    t.m->push(get(5, 0x40, 0, 8));
    CHECK(t.k.runUntilIdle(200) < 200);
    auto g = t.resp(0);
    CHECK(g != nullptr);
    if (g) { CHECK(!g->denied); for (unsigned k = 0; k < 4; k++) CHECK_EQ(g->beatCorrupt(k), k == 1); }
}

TEST(elaboration_errors)
{
    auto make_sys = [](Level lvl, unsigned down_o, std::vector<const char *> keys, unsigned down_z = 3)
    {
        Kernel k(opts());
        Endpoint *m = endpoint(k, "m"), *s = endpoint(k, "s");
        Config c;
        for (auto e : keys) c.set(e);
        Fragmenter *f = addAgent(k, new Fragmenter("F", c));
        k.connect("U", lp(8, lvl, 2), *m, *f);
        k.connect("D", lp(8, lvl, down_o, 0, down_z), *f, *s);
        f->validate();
    };
    // plan §3.5: an atomic larger than max_size cannot be split: TL-UH with sizes up to 7
    CHECK(throwsConfig([&] { make_sys(Level::UH, 6, {"max_size(i)=3"}); }, "cannot be split"));
    CHECK(throwsConfig([&] { make_sys(Level::UH, 6, {"max_size(i)=3", "max_unsplit(i)=4"}); }, "ArithmeticData, LogicalData or Intent"));
    CHECK(throwsConfig([&] { make_sys(Level::C, 6, {"max_size(i)=3"}); }, "Acquire, Release, atomic or Intent"));
    make_sys(Level::UH, 6, {"max_size(i)=3", "max_unsplit(i)=3"});      // declared: fine
    make_sys(Level::UL, 6, {"max_size(i)=3"});                           // TL-UL has nothing unsplittable
    // §6.4: the fragment index needs o + k bits downstream
    CHECK(throwsConfig([&] { make_sys(Level::UL, 5, {"max_size(i)=3"}); }, "needs o >= 6"));
    CHECK(throwsConfig([&] { make_sys(Level::UL, 6, {"max_size(i)=6"}, 2); }, "exceeds the downstream link's largest size"));
    CHECK(throwsConfig([] { Fragmenter f("F", Config()); }, "max_size(i) is required"));
}

TL_TEST_MAIN("test_fragmenter")
