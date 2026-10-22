/*
 * File  :      test_buffer.cpp
 * TL-TOPO: Buffer (plan §3.5) and Throttle (plan §3.5). Buffer: depth per channel, one cycle
 * in and one out (plan §3.2), depth 1 halving the throughput, back-pressure once full, both
 * directions, the configuration errors. Throttle: same seed same schedule (plan §3.2), ready
 * withheld at random, valid withheld only on the first beat of a message not yet accepted
 * (§4.1, plan §3.6 M-BURST): a burst in progress is never interrupted.
 */
#include "TopoTest.h"
#include "tilelink/agents/Buffer.h"
#include "tilelink/agents/Throttle.h"
#include <sstream>

using namespace topo;

namespace
{
    template <typename A> struct Chain
    {
        Kernel k;
        Endpoint *m;
        A *b;
        Endpoint *s;
        Link *u, *d;
        Chain(A *agent, uint64_t seed = 1) : k(opts(seed))
        {
            m = endpoint(k, "m");
            b = addAgent(k, agent);
            s = endpoint(k, "s");
            u = &k.connect("U", lp(8), *m, *b);
            d = &k.connect("D", lp(8), *b, *s);
        }
    };
    std::vector<uint64_t> cyclesOf(const std::vector<Endpoint::Got> &v)
    {
        std::vector<uint64_t> c;
        for (auto &g : v) c.push_back(g.cycle);
        return c;
    }
}

TEST(buffer_one_cycle_full_throughput)
{
    Chain<Buffer> t(new Buffer("B", Config()));
    for (uint64_t i = 0; i < 8; i++) t.m->push(make(Channel::A, unsigned(OpA::Get), 3, 0x40 * i, i, 8));
    CHECK(t.k.runUntilIdle(50) < 50);
    // depth 2 (default): accepted at 0..7 upstream, at 1..8 downstream
    std::vector<uint64_t> expect = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(cyclesOf(t.s->beats(Channel::A)) == expect);
    CHECK_EQ(t.b->depth(Channel::A), 2u);
}

TEST(buffer_depth_one_halves_throughput)
{
    Chain<Buffer> t(new Buffer("B", cfg({"depth(i)=1"})));
    for (uint64_t i = 0; i < 4; i++) t.m->push(make(Channel::A, unsigned(OpA::Get), 3, 0x40 * i, i, 8));
    CHECK(t.k.runUntilIdle(50) < 50);
    // ready comes from the state at the start of the cycle (plan §3.2): with the one slot
    // occupied the buffer refuses, so one beat per two cycles
    std::vector<uint64_t> expect = {1, 3, 5, 7};
    CHECK(cyclesOf(t.s->beats(Channel::A)) == expect);
}

TEST(buffer_depth_holds_then_back_pressures)
{
    Chain<Buffer> t(new Buffer("B", cfg({"depth(i)=1", "depth_A(i)=3"})));
    CHECK_EQ(t.b->depth(Channel::A), 3u);
    CHECK_EQ(t.b->depth(Channel::D), 1u);
    t.s->ready = [](Channel, uint64_t c) { return c >= 10; };
    for (uint64_t i = 0; i < 6; i++) t.m->push(make(Channel::A, unsigned(OpA::PutFullData), 4, 0x40 * i, i, 8, DataMode::Full, lowByte));
    t.k.run(10);
    // 2-beat Puts: the queue holds exactly 3 beats, then ready is LOW
    CHECK_EQ(t.u->chan(Channel::A).stats().beats, uint64_t(3));
    CHECK(t.u->chan(Channel::A).stats().stall >= 6);
    CHECK(t.s->got.empty());
    CHECK(t.k.runUntilIdle(100) < 100);
    auto a = t.s->beats(Channel::A);
    CHECK_EQ(a.size(), size_t(12));
    for (size_t i = 0; i < a.size(); i++)
    {
        CHECK_EQ(a[i].beat, unsigned(i % 2));
        // data unchanged: beat k lane l = address + 8k + l
        CHECK_EQ(unsigned(a[i].m->beatData(a[i].beat)[5]), unsigned((a[i].m->address + 8 * a[i].beat + 5) & 0xff));
    }
}

TEST(buffer_d_path)
{
    // Master -> Buffer -> RAM: the Get's response comes back one cycle later than without it.
    Kernel k(opts());
    Endpoint *m = endpoint(k, "m");
    Buffer *b = addAgent(k, new Buffer("B", Config()));
    MemSlave *r = memslave(k, "r");
    k.connect("U", lp(8), *m, *b);
    k.connect("D", lp(8), *b, *r);
    r->mem[0x41] = 0x5a;
    m->push(make(Channel::A, unsigned(OpA::Get), 3, 0x40, 2, 8));
    CHECK(k.runUntilIdle(50) < 50);
    auto d = m->messages(Channel::D);
    CHECK_EQ(d.size(), size_t(1));
    if (d.size() == 1)
    {
        // A at 0 (up), 1 (down); RAM latency 1: D offered and accepted at 2; up at 3. Without
        // the buffer it would be 1: the buffer adds exactly one cycle each way (plan §3.2).
        CHECK_EQ(d[0].cycle, uint64_t(3));
        CHECK_EQ(unsigned(d[0].m->data[1]), 0x5au);
        CHECK_EQ(d[0].m->source, uint64_t(2));
    }
}

TEST(buffer_config_errors)
{
    CHECK(throwsConfig([] { Buffer b("B", cfg({"depth(i)=0"})); }, "depth must be >= 1"));
    CHECK(throwsConfig([] { Buffer b("B", cfg({"depth_D(i)=0"})); }, "depth_D must be >= 1"));
    Kernel k(opts());
    Endpoint *m = endpoint(k, "m"), *s = endpoint(k, "s");
    Buffer *b = addAgent(k, new Buffer("B", Config()));
    k.connect("U", lp(8), *m, *b);
    k.connect("D", lp(4), *b, *s);
    CHECK(throwsConfig([&] { b->validate(); }, "must have the same width"));
}

// ---------------------------------------------------------------- Throttle

namespace
{
    std::string throttleRun(uint64_t seed)
    {
        Kernel k(opts(seed));
        Endpoint *m = endpoint(k, "m");
        Throttle *th = addAgent(k, new Throttle("T", cfg({"p_ready(d)=0.5", "p_valid(d)=0.5"})));
        MemSlave *r = memslave(k, "r");
        k.connect("U", lp(8), *m, *th);
        k.connect("D", lp(8), *th, *r);
        for (uint64_t i = 0; i < 12; i++)
            m->push(make(Channel::A, i % 3 ? unsigned(OpA::PutFullData) : unsigned(OpA::Get), unsigned(i % 6), 0x40 * i, i, 8,
                         DataMode::Full, lowByte));
        std::ostringstream o;
        k.enableTranscript(o);
        k.runUntilIdle(2000);
        CHECK_EQ(m->messages(Channel::D).size(), size_t(12));
        CHECK(th->readyWithheld() > 0);
        CHECK(th->validWithheld() > 0);
        return o.str();
    }
}

TEST(throttle_same_seed_same_schedule)
{
    std::string a = throttleRun(7), b = throttleRun(7), c = throttleRun(8);
    CHECK_EQ(a, b);
    CHECK(a != c);
    CHECK(a.size() > 100);
}

TEST(throttle_never_withholds_mid_burst)
{
    // p_valid 0.9: beat 0 of each message is withheld most cycles; once accepted, the rest
    // of the burst follows on consecutive cycles (the slave is always ready, the queue holds
    // the beats: the master is never throttled since p_ready is 0).
    Chain<Throttle> t(new Throttle("T", cfg({"p_ready(d)=0", "p_valid(d)=0.9", "depth(i)=16"})), 3);
    for (uint64_t i = 0; i < 6; i++) t.m->push(make(Channel::A, unsigned(OpA::PutFullData), 5, 0x100 * i, i, 8, DataMode::Full, lowByte));
    CHECK(t.k.runUntilIdle(500) < 500);
    auto a = t.s->beats(Channel::A);
    CHECK_EQ(a.size(), size_t(24));
    for (size_t i = 0; i + 1 < a.size(); i++)
        if (a[i].beat + 1 < a[i].m->beats()) CHECK_EQ(a[i + 1].cycle, a[i].cycle + 1);   // in progress: never a gap
    CHECK(t.b->validWithheld() > 6);
    CHECK_EQ(t.b->readyWithheld(), uint64_t(0));
    // and between messages there were gaps (the withholding happened)
    bool gap = false;
    for (size_t i = 0; i + 1 < a.size(); i++) if (a[i].beat + 1 == a[i].m->beats() && a[i + 1].cycle > a[i].cycle + 1) gap = true;
    CHECK(gap);
}

TEST(throttle_ready_withholding_delivers_everything)
{
    Chain<Throttle> t(new Throttle("T", cfg({"p_ready(d)=0.8"})), 5);
    for (uint64_t i = 0; i < 10; i++) t.m->push(make(Channel::A, unsigned(OpA::PutFullData), 4, 0x40 * i, i, 8, DataMode::Full, lowByte));
    CHECK(t.k.runUntilIdle(1000) < 1000);
    CHECK_EQ(t.s->messages(Channel::A).size(), size_t(10));
    CHECK(t.b->readyWithheld() > 5);
    CHECK(t.u->chan(Channel::A).stats().stall > 5);
    CHECK(throwsConfig([] { Throttle x("T", cfg({"p_ready(d)=1.5"})); }, "p_ready must be in [0, 1]"));
    // plan §3.6: the declared latency grows with the withholding probability
    Throttle lo("a", cfg({"p_ready(d)=0.1"})), hi("b", cfg({"p_ready(d)=0.9"}));
    CHECK(lo.hints().latency < hi.hints().latency);
    CHECK(hi.hints().latency > 100);   // 0.9^n < 1e-9 needs n = 197
}

TL_TEST_MAIN("test_buffer")
