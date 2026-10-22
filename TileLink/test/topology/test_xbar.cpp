/*
 * File  :      test_xbar.cpp
 * TL-TOPO: the Xbar of plan §3.5. Routing by Table 14 (A/C by address, B/D by source, E by
 * sink), the §6.4 remapping into one namespace per side, arbitration (round-robin, fixed,
 * TDM) holding the grant for a whole burst (§4.1), the one-cycle path (plan §3.2), the
 * independence of C/D/E from A/B occupancy (§4.1), the elaboration errors, and the build
 * from a configuration through the registry.
 */
#include "TopoTest.h"
#include "tilelink/agents/RegistryTopology.h"
#include "tilelink/agents/Xbar.h"
#include "tilelink/agents/Buffer.h"

using namespace topo;

namespace
{
    // m0, m1 -> X -> s0 (0x0000..0x0fff), s1 (0x1000..0x1fff). Upstream o=2 (4 sources each),
    // downstream o=3 (the 8 of both masters, §6.4).
    template <typename S> struct TwoByTwo
    {
        Kernel k;
        Endpoint *m0, *m1;
        Xbar *x;
        S *s0, *s1;
        Link *u0, *u1, *d0, *d1;
        explicit TwoByTwo(Level lvl = Level::UH, const char *arb = "arbitration(s)=rr", unsigned up_i = 0, unsigned down_i = 0)
            : k(opts())
        {
            m0 = endpoint(k, "m0");
            m1 = endpoint(k, "m1");
            x = addAgent(k, new Xbar("X", cfg({"routes(vs)=D0:0x0:0x1000,D1:0x1000:0x1000", arb})));
            s0 = static_cast<S *>(&k.add(std::unique_ptr<Agent>(new S("s0"))));
            s1 = static_cast<S *>(&k.add(std::unique_ptr<Agent>(new S("s1"))));
            u0 = &k.connect("U0", lp(8, lvl, 2, up_i), *m0, *x);
            u1 = &k.connect("U1", lp(8, lvl, 2, up_i), *m1, *x);
            d0 = &k.connect("D0", lp(8, lvl, 3, down_i), *x, *s0);
            d1 = &k.connect("D1", lp(8, lvl, 3, down_i), *x, *s1);
        }
    };
}

TEST(routing_by_address_and_source_remap)
{
    TwoByTwo<MemSlave> t;
    for (uint64_t a = 0x1040; a < 0x1048; a++) t.s1->mem[a] = uint8_t(0xa0 + (a & 7));
    t.m0->push(make(Channel::A, unsigned(OpA::Get), 3, 0x40, 1, 8));
    t.m1->push(make(Channel::A, unsigned(OpA::Get), 3, 0x1040, 1, 8));
    t.m1->push(make(Channel::A, unsigned(OpA::PutFullData), 3, 0x80, 2, 8, DataMode::Full, lowByte));
    CHECK(t.k.runUntilIdle(100) < 100);
    // Table 14: A by address. §6.4: m1's sources at base 4 in the downstream namespace.
    CHECK_EQ(t.x->sourceBase(*t.u0), uint64_t(0));
    CHECK_EQ(t.x->sourceBase(*t.u1), uint64_t(4));
    CHECK_EQ(t.s0->requests.size(), size_t(2));
    CHECK_EQ(t.s1->requests.size(), size_t(1));
    if (t.s0->requests.size() == 2)
    {
        CHECK_EQ(t.s0->requests[0]->source, uint64_t(1));     // m0's Get
        CHECK_EQ(t.s0->requests[1]->source, uint64_t(6));     // m1's Put: 4 + 2
        CHECK_EQ(t.s0->requests[1]->address, uint64_t(0x80));
    }
    if (!t.s1->requests.empty()) CHECK_EQ(t.s1->requests[0]->source, uint64_t(5));
    CHECK_EQ(t.s0->byteAt(0x83), uint64_t(0x83));             // the Put's data arrived unchanged
    // Table 14: D by source, back in the master's own namespace.
    auto d0 = t.m0->messages(Channel::D), d1 = t.m1->messages(Channel::D);
    CHECK_EQ(d0.size(), size_t(1));
    CHECK_EQ(d1.size(), size_t(2));
    if (d0.size() == 1) CHECK_EQ(d0[0].m->source, uint64_t(1));
    bool saw_data = false;
    for (auto &g : d1)
    {
        CHECK(g.m->source == 1 || g.m->source == 2);
        if (g.m->source == 1) { saw_data = true; CHECK_EQ(unsigned(g.m->data[0]), 0xa0u); CHECK_EQ(unsigned(g.m->data[7]), 0xa7u); }
    }
    CHECK(saw_data);
    // Plan §3.2: one cycle through the crossbar. m0's Get accepted at 0 upstream, at 1 downstream.
    CHECK_EQ(t.m0->sent_beats[0].first, uint64_t(0));
    CHECK_EQ(t.s0->sources_seen[0].first, uint64_t(1));
    CHECK(t.x->routeOf(0x1fff) == t.d1);
    CHECK(t.x->routeOf(0x2000) == nullptr);
}

TEST(tlc_routing_b_by_source_c_by_address_e_by_sink)
{
    // TL-C: downstream i=1 per slave (2 sinks), so the upstream namespace needs 4 (i=2).
    TwoByTwo<Endpoint> t(Level::C, "arbitration(s)=rr", 2, 1);
    CHECK_EQ(t.x->sinkBase(*t.d0), uint64_t(0));
    CHECK_EQ(t.x->sinkBase(*t.d1), uint64_t(2));
    // B by source: s1 probes m1's source 3 (downstream 4+3).
    MsgPtr probe = make(Channel::B, unsigned(OpB::ProbeBlock), 6, 0x1040, 7, 8);
    t.s1->push(probe);
    // C by address: m1's ProbeAck for 0x1040 goes to s1, whatever its source (§6.4).
    MsgPtr pack = make(Channel::C, unsigned(OpC::ProbeAck), 6, 0x1040, 3, 8);
    t.m1->push(pack, 3);
    // D by source; a Grant's sink remapped into the upstream namespace.
    MsgPtr g0 = make(Channel::D, unsigned(OpD::Grant), 6, 0, 2, 8);  g0->sink = 1;
    MsgPtr g1 = make(Channel::D, unsigned(OpD::Grant), 6, 0, 5, 8);  g1->sink = 1;
    MsgPtr ra = make(Channel::D, unsigned(OpD::ReleaseAck), 6, 0, 0, 8); ra->sink = 1;   // not a Grant: sink ignored, untouched
    t.s0->push(g0); t.s1->push(g1); t.s0->push(ra);
    // E by sink: m1 acknowledges sink 3 (= s1's 1), m0 sink 1 (= s0's 1).
    MsgPtr e1 = make(Channel::E, unsigned(OpE::GrantAck), 0, 0, 0, 8); e1->sink = 3;
    MsgPtr e0 = make(Channel::E, unsigned(OpE::GrantAck), 0, 0, 0, 8); e0->sink = 1;
    t.m1->push(e1, 5); t.m0->push(e0, 5);
    CHECK(t.k.runUntilIdle(100) < 100);
    auto b1 = t.m1->messages(Channel::B);
    CHECK_EQ(b1.size(), size_t(1));
    if (!b1.empty()) { CHECK_EQ(b1[0].m->source, uint64_t(3)); CHECK_EQ(b1[0].m->address, uint64_t(0x1040)); }
    CHECK(t.m0->messages(Channel::B).empty());
    auto c1 = t.s1->messages(Channel::C);
    CHECK_EQ(c1.size(), size_t(1));
    if (!c1.empty()) CHECK_EQ(c1[0].m->source, uint64_t(7));
    CHECK(t.s0->messages(Channel::C).empty());
    auto dm0 = t.m0->messages(Channel::D), dm1 = t.m1->messages(Channel::D);
    CHECK_EQ(dm0.size(), size_t(2));
    CHECK_EQ(dm1.size(), size_t(1));
    if (dm0.size() == 2)
    {
        CHECK_EQ(dm0[0].m->source, uint64_t(2)); CHECK_EQ(dm0[0].m->sink, uint64_t(1));   // s0's sink 1 at base 0
        CHECK_EQ(dm0[1].m->opcode, unsigned(OpD::ReleaseAck)); CHECK_EQ(dm0[1].m->sink, uint64_t(1));
    }
    if (dm1.size() == 1) { CHECK_EQ(dm1[0].m->source, uint64_t(1)); CHECK_EQ(dm1[0].m->sink, uint64_t(3)); }   // s1's sink 1 at base 2
    auto es0 = t.s0->messages(Channel::E), es1 = t.s1->messages(Channel::E);
    CHECK_EQ(es0.size(), size_t(1)); CHECK_EQ(es1.size(), size_t(1));
    if (es0.size() == 1) CHECK_EQ(es0[0].m->sink, uint64_t(1));
    if (es1.size() == 1) CHECK_EQ(es1[0].m->sink, uint64_t(1));
}

namespace
{
    // Two masters to one recording slave; returns the downstream sources in arrival order and
    // the cycles of every downstream beat.
    struct OneSlave
    {
        Kernel k;
        Endpoint *m0, *m1, *s;
        Xbar *x;
        explicit OneSlave(const char *arb, const char *slot = "tdm_slot(i)=1") : k(opts())
        {
            m0 = endpoint(k, "m0"); m1 = endpoint(k, "m1");
            x = addAgent(k, new Xbar("X", cfg({arb, slot})));
            s = endpoint(k, "s");
            k.connect("U0", lp(8, Level::UH, 2), *m0, *x);
            k.connect("U1", lp(8, Level::UH, 2), *m1, *x);
            k.connect("D", lp(8, Level::UH, 3), *x, *s);
        }
        std::vector<uint64_t> order() const
        {
            std::vector<uint64_t> v;
            for (auto &g : s->messages(Channel::A)) v.push_back(g.m->source);
            return v;
        }
    };
    MsgPtr put32(uint64_t addr, uint64_t src) { return make(Channel::A, unsigned(OpA::PutFullData), 5, addr, src, 8, DataMode::Full, lowByte); }
}

TEST(round_robin_holds_grant_for_burst)
{
    OneSlave t("arbitration(s)=rr");
    t.m0->push(put32(0x000, 0)); t.m0->push(put32(0x100, 1));
    t.m1->push(put32(0x200, 0)); t.m1->push(put32(0x300, 1));
    CHECK(t.k.runUntilIdle(100) < 100);   // a burst interleaving would have been fatal (§4.1 burst lock)
    std::vector<uint64_t> expect = {0, 4, 1, 5};
    CHECK(t.order() == expect);
    auto beats = t.s->beats(Channel::A);
    CHECK_EQ(beats.size(), size_t(16));
    // The grant is held: each message's 4 beats arrive contiguously, back to back, and the
    // next message follows at once (cycles 1..16).
    for (size_t i = 0; i < beats.size(); i++)
    {
        CHECK_EQ(beats[i].cycle, uint64_t(i + 1));
        CHECK_EQ(beats[i].beat, unsigned(i % 4));
        CHECK(beats[i].m == beats[i - i % 4].m);
        // the data travelled with the beat (§4.5): lane l of beat k = byte address & 0xff
        CHECK_EQ(unsigned(beats[i].m->beatData(beats[i].beat)[3]), unsigned((beats[i].m->address + 8 * beats[i].beat + 3) & 0xff));
    }
}

TEST(fixed_priority)
{
    OneSlave t("arbitration(s)=fixed");
    t.m0->push(put32(0x000, 0)); t.m0->push(put32(0x100, 1));
    t.m1->push(put32(0x200, 0)); t.m1->push(put32(0x300, 1));
    CHECK(t.k.runUntilIdle(100) < 100);
    std::vector<uint64_t> expect = {0, 1, 4, 5};
    CHECK(t.order() == expect);
}

TEST(tdm_slots)
{
    // Slot of 2 cycles: cycles 0,1 belong to m0, 2,3 to m1, 4,5 to m0 ...
    OneSlave t("arbitration(s)=tdm", "tdm_slot(i)=2");
    for (uint64_t i = 0; i < 3; i++) t.m0->push(make(Channel::A, unsigned(OpA::Get), 3, 0x40 * i, i, 8));
    t.m1->push(make(Channel::A, unsigned(OpA::Get), 3, 0x400, 0, 8));
    CHECK(t.k.runUntilIdle(100) < 100);
    std::vector<uint64_t> expect = {0, 1, 4, 2};
    CHECK(t.order() == expect);
    auto beats = t.s->beats(Channel::A);
    std::vector<uint64_t> cycles;
    for (auto &b : beats) cycles.push_back(b.cycle);
    std::vector<uint64_t> expect_cycles = {1, 2, 3, 5};   // accepted at 0, 1 (m0), 2 (m1), 4 (m0); +1 through
    CHECK(cycles == expect_cycles);
    CHECK(t.x->policy() == Xbar::Policy::TDM);
}

TEST(d_flows_while_a_is_stalled)
{
    // §4.1: a high-priority ready never depends on a low-priority ready. The slave never
    // accepts A; the crossbar's A stage fills and m0's A stalls; the slave's D responses
    // (to sources of m0 and m1) still go through every cycle.
    Kernel k(opts());
    Endpoint *m0 = endpoint(k, "m0"), *m1 = endpoint(k, "m1"), *s = endpoint(k, "s");
    Xbar *x = addAgent(k, new Xbar("X", Config()));
    // evaluation order matters for nothing (plan §3.2); the crossbar added last here
    Link &u0 = k.connect("U0", lp(8, Level::UH, 2), *m0, *x);
    k.connect("U1", lp(8, Level::UH, 2), *m1, *x);
    k.connect("D", lp(8, Level::UH, 3), *x, *s);
    s->ready = [](Channel ch, uint64_t) { return ch != Channel::A; };
    for (uint64_t i = 0; i < 4; i++) m0->push(put32(0x40 * i, i));
    for (uint64_t i = 0; i < 3; i++) s->push(make(Channel::D, unsigned(OpD::AccessAckData), 4, 0, i, 8, DataMode::Full, lowByte), 5);
    s->push(make(Channel::D, unsigned(OpD::AccessAck), 3, 0, 6, 8), 5);   // m1's source 2
    k.run(30);
    auto dm0 = m0->messages(Channel::D), dm1 = m1->messages(Channel::D);
    CHECK_EQ(dm0.size(), size_t(3));
    CHECK_EQ(dm1.size(), size_t(1));
    if (dm1.size() == 1) CHECK_EQ(dm1[0].m->source, uint64_t(2));
    // D beats: 3 x 2 beats + 1, back to back from cycle 6, while A is stalled throughout
    auto db = m0->beats(Channel::D);
    if (!db.empty()) CHECK_EQ(db[0].cycle, uint64_t(6));
    CHECK(u0.chan(Channel::A).valid());                       // m0 still offering its first Put
    CHECK(u0.chan(Channel::A).stats().stall > 20);
    CHECK_EQ(u0.chan(Channel::A).stats().beats, uint64_t(2)); // the 2-deep stage took 2 beats, then held
    CHECK(s->messages(Channel::A).empty());
}

TEST(c_d_e_flow_while_a_and_b_are_stalled)
{
    // TL-C: the slave refuses A, the master refuses B. The crossbar's A and B stages fill;
    // C (ProbeAcks), D (Grants) and E (GrantAcks) still pass in both directions.
    Kernel k(opts());
    Endpoint *m = endpoint(k, "m"), *s = endpoint(k, "s");
    Xbar *x = addAgent(k, new Xbar("X", Config()));
    Link &u = k.connect("U", lp(8, Level::C, 2, 1), *m, *x);
    Link &d = k.connect("D", lp(8, Level::C, 2, 1), *x, *s);
    s->ready = [](Channel ch, uint64_t) { return ch != Channel::A; };
    m->ready = [](Channel ch, uint64_t) { return ch != Channel::B; };
    for (uint64_t i = 0; i < 4; i++) m->push(make(Channel::A, unsigned(OpA::AcquireBlock), 6, 0x40 * i, i, 8));
    for (uint64_t i = 0; i < 4; i++) s->push(make(Channel::B, unsigned(OpB::ProbeBlock), 6, 0x400 + 0x40 * i, 0, 8));
    for (uint64_t i = 0; i < 3; i++) m->push(make(Channel::C, unsigned(OpC::ProbeAck), 6, 0x800 + 0x40 * i, 0, 8), 4);
    for (uint64_t i = 0; i < 3; i++) { MsgPtr g = make(Channel::D, unsigned(OpD::Grant), 6, 0, i, 8); g->sink = i & 1; s->push(g, 4); }
    for (uint64_t i = 0; i < 2; i++) { MsgPtr e = make(Channel::E, unsigned(OpE::GrantAck), 0, 0, 0, 8); e->sink = i; m->push(e, 4); }
    k.run(30);
    CHECK_EQ(s->messages(Channel::C).size(), size_t(3));
    CHECK_EQ(m->messages(Channel::D).size(), size_t(3));
    CHECK_EQ(s->messages(Channel::E).size(), size_t(2));
    CHECK(u.chan(Channel::A).stats().stall > 20);
    CHECK(d.chan(Channel::B).stats().stall > 20);
    CHECK(s->messages(Channel::A).empty());
    CHECK(m->messages(Channel::B).empty());
}

TEST(elaboration_errors)
{
    auto build = [](const std::vector<const char *> &keys, unsigned down_o, unsigned w1, Level l1, int slaves)
    {
        Kernel k(opts());
        Endpoint *m0 = endpoint(k, "m0"), *m1 = endpoint(k, "m1");
        Config c;
        for (auto kk : keys) c.set(kk);
        Xbar *x = addAgent(k, new Xbar("X", c));
        k.connect("U0", lp(8, Level::UH, 2), *m0, *x);
        k.connect("U1", lp(w1, l1, 2), *m1, *x);
        for (int j = 0; j < slaves; j++) k.connect("D" + std::to_string(j), lp(8, Level::UH, down_o), *x, *endpoint(k, "s" + std::to_string(j)));
        x->validate();
    };
    CHECK(throwsConfig([&] { build({"routes(vs)=D0:0x0:0x1000,D1:0x800:0x1000"}, 3, 8, Level::UH, 2); }, "routes overlap"));
    CHECK(throwsConfig([&] { build({}, 3, 8, Level::UH, 2); }, "no routes(vs) and no default(s)"));
    CHECK(throwsConfig([&] { build({"routes(vs)=D9:0x0:0x1000"}, 3, 8, Level::UH, 2); }, "not a downstream link"));
    CHECK(throwsConfig([&] { build({}, 2, 8, Level::UH, 1); }, "need 8 ids in one namespace"));
    CHECK(throwsConfig([&] { build({}, 3, 4, Level::UH, 1); }, "width and level"));
    CHECK(throwsConfig([&] { build({}, 3, 8, Level::UL, 1); }, "width and level"));
    CHECK(throwsConfig([] { Xbar x("X", cfg({"arbitration(s)=lottery"})); }, "arbitration must be"));
    CHECK(throwsConfig([] { Xbar x("X", cfg({"routes(vs)=D0:0x0"})); }, "link:base:size"));
    // the legal ones pass: a default route, one slave without routes
    build({"routes(vs)=D0:0x0:0x1000", "default(s)=D1"}, 3, 8, Level::UH, 2);
    build({}, 3, 8, Level::UH, 1);
    // TL-C sinks: two slaves with i=1 need upstream i >= 2
    Kernel k(opts());
    Endpoint *m = endpoint(k, "m");
    Xbar *x = addAgent(k, new Xbar("X", cfg({"routes(vs)=D0:0x0:0x1000,D1:0x1000:0x1000"})));
    k.connect("U", lp(8, Level::C, 2, 1), *m, *x);
    k.connect("D0", lp(8, Level::C, 2, 1), *x, *endpoint(k, "s0"));
    k.connect("D1", lp(8, Level::C, 2, 1), *x, *endpoint(k, "s1"));
    CHECK(throwsConfig([&] { x->validate(); }, "the slaves' sinks need 4 ids"));
}

TEST(build_from_config_through_registry)
{
    // Plan §4: agent[k].type(s) by name, the agent's keys from its sub-map.
    AgentRegistry reg = AgentRegistry::withBuiltins();
    registerTopologyAgents(reg);
    reg.add("Endpoint", [](const std::string &n, const Config &) { return std::unique_ptr<Agent>(new Endpoint(n)); });
    reg.add("MemSlave", [](const std::string &n, const Config &) { return std::unique_ptr<Agent>(new MemSlave(n)); });
    for (auto t : {"Xbar", "Buffer", "WidthWidget", "Fragmenter", "SourceShrinker", "Throttle"}) CHECK(reg.has(t));
    Config c = cfg({"reset_cycles(i)=0", "graph_check(i)=1", "watchdog(i)=1",
                    "agent[0].type(s)=Endpoint", "agent[0].name(s)=m",
                    "agent[1].type(s)=Xbar", "agent[1].name(s)=X", "agent[1].routes(vs)=LA:0x0:0x1000,LB:0x1000:0x1000",
                    "agent[2].type(s)=Buffer", "agent[2].name(s)=B", "agent[2].depth(i)=3",
                    "agent[3].type(s)=MemSlave", "agent[3].name(s)=ra",
                    "agent[4].type(s)=MemSlave", "agent[4].name(s)=rb",
                    "link[0].master(s)=m", "link[0].slave(s)=X", "link[0].level(s)=UH", "link[0].o(i)=2",
                    "link[1].name(s)=LA", "link[1].master(s)=X", "link[1].slave(s)=B", "link[1].level(s)=UH", "link[1].o(i)=2",
                    "link[2].master(s)=B", "link[2].slave(s)=ra", "link[2].level(s)=UH", "link[2].o(i)=2",
                    "link[3].name(s)=LB", "link[3].master(s)=X", "link[3].slave(s)=rb", "link[3].level(s)=UH", "link[3].o(i)=2"});
    Kernel k;
    k.build(c, reg);
    auto *m = static_cast<Endpoint *>(k.agent("m"));
    auto *ra = static_cast<MemSlave *>(k.agent("ra"));
    auto *rb = static_cast<MemSlave *>(k.agent("rb"));
    auto *b = static_cast<Buffer *>(k.agent("B"));
    CHECK(m && ra && rb && b);
    if (!(m && ra && rb && b)) return;
    CHECK_EQ(b->depth(Channel::A), 3u);
    m->push(make(Channel::A, unsigned(OpA::PutFullData), 3, 0x10, 0, 8, DataMode::Full, lowByte));
    m->push(make(Channel::A, unsigned(OpA::PutFullData), 3, 0x1010, 1, 8, DataMode::Full, lowByte));
    CHECK(k.runUntilIdle(200) < 200);
    CHECK_EQ(ra->byteAt(0x15), uint64_t(0x15));
    CHECK_EQ(rb->byteAt(0x1015), uint64_t(0x15));
    CHECK_EQ(m->messages(Channel::D).size(), size_t(2));
}

TEST(interleave_routes_line_banks)
{
    // Plan §5.3 I-A: Octopus's LLC banks are line-interleaved, (line >> shift) % count, which no
    // set of ranges expresses. link:interleave:<shift>:<count>:<index>, block_bytes 64.
    auto route = [](const std::vector<const char *> &keys, int slaves, std::vector<int> &out)
    {
        Kernel k(opts());
        Endpoint *m = endpoint(k, "m");
        Config c;
        for (auto kk : keys) c.set(kk);
        Xbar *x = addAgent(k, new Xbar("X", c));
        k.connect("U", lp(8, Level::C, 2, 3), *m, *x);   // up to 3 slaves x 2 sinks
        std::vector<Link *> d;
        for (int j = 0; j < slaves; j++) d.push_back(&k.connect("D" + std::to_string(j), lp(8, Level::C, 2, 1), *x, *endpoint(k, "s" + std::to_string(j))));
        x->validate();
        out.clear();
        for (uint64_t line = 0; line < 16; line++)
        {
            Link *l = x->routeOf(line * 64 + 8);   // any byte of the line
            int j = -1;
            for (int q = 0; q < slaves; q++) if (l == d[size_t(q)]) j = q;
            out.push_back(j);
        }
    };
    std::vector<int> r;
    route({"routes(vs)=D0:interleave:0:2:0,D1:interleave:0:2:1"}, 2, r);         // consecutive lines alternate
    for (int line = 0; line < 16; line++) CHECK_EQ(r[size_t(line)], line % 2);
    route({"routes(vs)=D1:interleave:1:2:1,D0:interleave:1:2:0"}, 2, r);         // pairs of lines; order of entries free
    for (int line = 0; line < 16; line++) CHECK_EQ(r[size_t(line)], (line >> 1) % 2);
    route({"routes(vs)=D0:interleave:0:3:0,D1:interleave:0:3:1,D2:interleave:0:3:2"}, 3, r);
    for (int line = 0; line < 16; line++) CHECK_EQ(r[size_t(line)], line % 3);
    route({"routes(vs)=D0:interleave:0:2:0,D1:interleave:0:2:1", "block_bytes(i)=128"}, 2, r);   // 128-byte lines: two 64-byte steps per bank
    for (int line = 0; line < 16; line++) CHECK_EQ(r[size_t(line)], (line >> 1) % 2);
    route({"routes(vs)=D0:interleave:0:2:0"}, 2, r);                              // an index nobody takes: unmapped (no default)
    for (int line = 0; line < 16; line++) CHECK_EQ(r[size_t(line)], line % 2 == 0 ? 0 : -1);
    // §6.3, exactly one slave per address: refused at elaboration
    CHECK(throwsConfig([&] { route({"routes(vs)=D0:interleave:0:2:0,D1:interleave:0:2:0"}, 2, r); }, "same index"));
    CHECK(throwsConfig([&] { route({"routes(vs)=D0:interleave:0:2:0,D1:interleave:1:2:1"}, 2, r); }, "differ in shift or count"));
    CHECK(throwsConfig([&] { route({"routes(vs)=D0:interleave:0:2:0,D1:0x0:0x1000"}, 2, r); }, "mixes"));
    CHECK(throwsConfig([] { Xbar x("X", cfg({"routes(vs)=D0:interleave:0:2:2"})); }, "index < count"));
    CHECK(throwsConfig([] { Xbar x("X", cfg({"routes(vs)=D0:interleave:0:2"})); }, "link:interleave"));
    CHECK(throwsConfig([] { Xbar x("X", cfg({"block_bytes(i)=48"})); }, "power of two"));
    // traffic: an A and a C to alternating lines reach the bank of their line, the D comes back by source
    Kernel k(opts());
    Endpoint *m = endpoint(k, "m");
    Xbar *x = addAgent(k, new Xbar("X", cfg({"routes(vs)=D0:interleave:0:2:0,D1:interleave:0:2:1"})));
    k.connect("U", lp(8, Level::UH, 2), *m, *x);
    MemSlave *s0 = static_cast<MemSlave *>(&k.add(std::unique_ptr<Agent>(new MemSlave("s0"))));
    MemSlave *s1 = static_cast<MemSlave *>(&k.add(std::unique_ptr<Agent>(new MemSlave("s1"))));
    k.connect("D0", lp(8, Level::UH, 3), *x, *s0);
    k.connect("D1", lp(8, Level::UH, 3), *x, *s1);
    m->push(make(Channel::A, unsigned(OpA::Get), 3, 0x1000, 0, 8));   // line 0x40: bank 0
    m->push(make(Channel::A, unsigned(OpA::Get), 3, 0x1040, 1, 8));   // line 0x41: bank 1
    m->push(make(Channel::A, unsigned(OpA::Get), 3, 0x10c8, 2, 8));   // line 0x43: bank 1
    CHECK(k.runUntilIdle(100) < 100);
    CHECK_EQ(s0->requests.size(), size_t(1));
    CHECK_EQ(s1->requests.size(), size_t(2));
    CHECK_EQ(m->messages(Channel::D).size(), size_t(3));
}

TL_TEST_MAIN("test_xbar")
