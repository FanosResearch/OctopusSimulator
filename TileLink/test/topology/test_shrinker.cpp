/*
 * File  :      test_shrinker.cpp
 * TL-TOPO: SourceShrinker (plan §3.5) and TL-ID (plan §6), the part that needs only the
 * topology agents. Shrinker: at most `pool` downstream sources outstanding, A held with ready
 * LOW while the pool is empty and released when a response frees one, sources mapped back
 * on D (§6.4), refused on TL-C. TL-ID: masters with o = 1 and o = 2 at full load through an
 * Xbar and a SourceShrinker: the ID pools block the senders (ready LOW), no source is ever
 * reused while outstanding, every request is answered, and the default watchdog of plan §3.6
 * (armed by Kernel::build) never trips.
 */
#include "TopoTest.h"
#include "tilelink/agents/RegistryTopology.h"
#include "tilelink/agents/SourceShrinker.h"
#include "tilelink/agents/Xbar.h"

using namespace topo;

TEST(blocks_when_pool_empty_and_unblocks)
{
    Kernel k(opts());
    Endpoint *m = endpoint(k, "m");
    SourceShrinker *sh = addAgent(k, new SourceShrinker("S", cfg({"pool(i)=2"})));
    MemSlave *r = memslave(k, "r");
    Link &u = k.connect("U", lp(8, Level::UH, 3), *m, *sh);
    k.connect("D", lp(8, Level::UH, 1), *sh, *r);
    r->latency = 6;
    for (uint64_t a = 0; a < 0x30; a++) r->mem[a] = uint8_t(a + 1);
    for (uint64_t i = 0; i < 6; i++) m->push(make(Channel::A, unsigned(OpA::Get), 3, 8 * i, 7 - i, 8));
    m->push(make(Channel::A, unsigned(OpA::PutFullData), 5, 0x40, 0, 8, DataMode::Full, lowByte));   // a burst through the shrinker
    CHECK(k.runUntilIdle(300) < 300);
    CHECK_EQ(sh->poolSize(), uint64_t(2));
    CHECK_EQ(r->max_outstanding, uint64_t(2));                // never more than the pool
    for (auto &s : r->sources_seen) CHECK(s.second < 2);
    CHECK(sh->blockedCycles() > 5);                           // A held with ready LOW (plan §3.5)
    CHECK(u.chan(Channel::A).stats().stall > 5);
    auto d = m->messages(Channel::D);
    CHECK_EQ(d.size(), size_t(7));
    for (auto &g : d)
    {
        if (g.m->opcode != unsigned(OpD::AccessAckData)) { CHECK_EQ(g.m->source, uint64_t(0)); continue; }
        uint64_t i = 7 - g.m->source;                         // the master's own source back (§6.4)
        CHECK_EQ(unsigned(g.m->data[0]), unsigned(8 * i + 1));
    }
    CHECK_EQ(r->byteAt(0x5f), uint64_t(0x5f));
    CHECK_EQ(sh->poolFree(), size_t(2));
    CHECK(sh->idle());
}

TEST(refused_on_tlc_and_oversized_pool)
{
    auto sys = [](Level lvl, const char *pool)
    {
        Kernel k(opts());
        Endpoint *m = endpoint(k, "m"), *s = endpoint(k, "s");
        SourceShrinker *sh = addAgent(k, new SourceShrinker("S", cfg({pool})));
        k.connect("U", lp(8, lvl, 3), *m, *sh);
        k.connect("D", lp(8, lvl, 1), *sh, *s);
        sh->validate();
    };
    CHECK(throwsConfig([&] { sys(Level::C, "pool(i)=2"); }, "refused on TL-C links"));
    CHECK(throwsConfig([&] { sys(Level::UH, "pool(i)=4"); }, "exceeds the downstream link's 2^o = 2"));
    sys(Level::UL, "pool(i)=2");
    sys(Level::UH, "pool(i)=1");
}

namespace
{
    // TL-ID: a master with its own 2^o pool; it issues whenever a source is free, so it runs
    // at full load and is blocked by its own pool (and by the network's ready).
    class PoolMaster : public Agent
    {
    public:
        PoolMaster(const std::string &n, const Config &c) : Agent(n), total(unsigned(c.get<int>("requests", 40))),
                                                            base(uint64_t(c.get<int>("base", 0))) {}
        Level capability(const Link &, Role) const override { return Level::UH; }
        unsigned total, issued = 0, answered = 0;
        uint64_t base;
        std::set<uint64_t> busy;
        uint64_t reuse = 0, blocked = 0;
        MsgPtr cur;
        Link &link() const { return *m_links.at(0).link; }
        void offer(uint64_t) override
        {
            if (m_kernel->inReset()) return;
            if (!cur && issued < total)
            {
                uint64_t n = link().params().numSources(), src = n;
                for (uint64_t s = 0; s < n; s++) if (!busy.count(s)) { src = s; break; }
                if (src == n) { blocked++; return; }
                bool put = issued % 2;
                cur = make(Channel::A, put ? unsigned(OpA::PutFullData) : unsigned(OpA::Get), 4, base + 16 * (issued % 16), src,
                           link().params().w, DataMode::Full, lowByte);
            }
            if (cur) link().chan(Channel::A).offer(cur, link().chan(Channel::A).inProgress() ? link().chan(Channel::A).nextBeat() : 0);
        }
        void accept(uint64_t) override { link().chan(Channel::D).setReady(true); }
        void commit(uint64_t) override
        {
            const Beat &a = link().chan(Channel::A).accepted();
            if (a.msg && a.last()) { if (busy.count(a.msg->source)) reuse++; busy.insert(a.msg->source); cur = nullptr; issued++; }
            const Beat &d = link().chan(Channel::D).accepted();
            if (d.msg && d.last()) { busy.erase(d.msg->source); answered++; }
        }
        bool idle() const override { return answered == total; }
    };
}

namespace
{
    struct IdRun { uint64_t blocked1, blocked2, reuse, shrink_blocked, max_out; unsigned answered1, answered2; size_t ram_sources; };

    // m1 (o=1) and m2 (o=2) -> Xbar (downstream o=3: 2 + 4 sources, §6.4) [-> SourceShrinker
    // (pool 3)] -> RAM. Built by Kernel::build, so the default watchdog (fatal) and GraphCheck
    // are on (plan §3.6): a trip would exit the test with status 2.
    IdRun idRun(bool shrinker)
    {
        AgentRegistry reg = AgentRegistry::withBuiltins();
        registerTopologyAgents(reg);
        reg.add("PoolMaster", [](const std::string &n, const Config &c) { return std::unique_ptr<Agent>(new PoolMaster(n, c)); });
        reg.add("MemSlave", [](const std::string &n, const Config &) { return std::unique_ptr<Agent>(new MemSlave(n)); });
        Config c = cfg({"reset_cycles(i)=0", "seed(i)=11",
                        "agent[0].type(s)=PoolMaster", "agent[0].name(s)=m1", "agent[0].requests(i)=40",
                        "agent[1].type(s)=PoolMaster", "agent[1].name(s)=m2", "agent[1].requests(i)=40", "agent[1].base(i)=256",
                        "agent[2].type(s)=Xbar", "agent[2].name(s)=X",
                        "agent[4].type(s)=MemSlave", "agent[4].name(s)=r",
                        "link[0].master(s)=m1", "link[0].slave(s)=X", "link[0].level(s)=UH", "link[0].o(i)=1",
                        "link[1].master(s)=m2", "link[1].slave(s)=X", "link[1].level(s)=UH", "link[1].o(i)=2"});
        if (shrinker)
        {
            for (auto l : {"agent[3].type(s)=SourceShrinker", "agent[3].name(s)=S", "agent[3].pool(i)=3",
                           "link[2].master(s)=X", "link[2].slave(s)=S", "link[2].level(s)=UH", "link[2].o(i)=3",
                           "link[3].master(s)=S", "link[3].slave(s)=r", "link[3].level(s)=UH", "link[3].o(i)=2"})
                c.set(l);
        }
        else
            for (auto l : {"link[2].master(s)=X", "link[2].slave(s)=r", "link[2].level(s)=UH", "link[2].o(i)=3"}) c.set(l);
        Kernel k;
        k.build(c, reg);
        auto *m1 = static_cast<PoolMaster *>(k.agent("m1"));
        auto *m2 = static_cast<PoolMaster *>(k.agent("m2"));
        auto *r = static_cast<MemSlave *>(k.agent("r"));
        auto *sh = static_cast<SourceShrinker *>(k.agent("S"));
        r->latency = 8;
        CHECK(k.runUntilIdle(5000) < 5000);
        std::set<uint64_t> xs;
        for (auto &q : r->requests) xs.insert(q->source);
        return IdRun{m1->blocked, m2->blocked, m1->reuse + m2->reuse, sh ? sh->blockedCycles() : 0, r->max_outstanding,
                     m1->answered, m2->answered, xs.size()};
    }
}

TEST(tl_id_pools_block_the_sender_at_full_load)
{
    // Without the shrinker: each master is blocked by its own 2^o pool (ready is never what
    // holds it, the slave keeps up), and the crossbar's one namespace carries all 2 + 4
    // sources outstanding at once.
    IdRun a = idRun(false);
    CHECK_EQ(a.answered1, 40u); CHECK_EQ(a.answered2, 40u);
    CHECK_EQ(a.reuse, uint64_t(0));                            // no source reused while outstanding (§6.4)
    CHECK(a.blocked1 > 0); CHECK(a.blocked2 > 0);
    CHECK_EQ(a.max_out, uint64_t(6));
    CHECK_EQ(a.ram_sources, size_t(6));                         // 0, 1 (m1) and 2..5 (m2's 0..3 at base 2)
    // With the shrinker: the downstream pool of 3 holds A with ready LOW (plan §3.5); the o=1
    // master is still blocked by its own pool; at most 3 outstanding at the RAM.
    IdRun b = idRun(true);
    CHECK_EQ(b.answered1, 40u); CHECK_EQ(b.answered2, 40u);
    CHECK_EQ(b.reuse, uint64_t(0));
    CHECK(b.blocked1 > 0);
    CHECK(b.shrink_blocked > 0);
    CHECK_EQ(b.max_out, uint64_t(3));
    CHECK(b.ram_sources <= 3);
}

TL_TEST_MAIN("test_shrinker")
