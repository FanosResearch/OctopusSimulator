/*
 * File  :      test_kernel.cpp
 * TL-UNIT: the kernel's handshake semantics (§4.1): valid independent of ready; the burst
 * lock; retract before the first accept allowed, after it forbidden; the one-cycle minimum
 * from accept to the next offer (plan §3.2); reset (§3.2.2); elaboration; determinism.
 */
#include "TestMain.h"
#include "tilelink/Kernel.h"
#include <sstream>

using namespace octopus::tl;

namespace
{
    // A master that offers a fixed script of messages on A, one beat per cycle, re-offering a
    // rejected beat. `retract_at` = a cycle at which it lowers valid instead (a retraction);
    // `swap_at` = a cycle at which it offers the next message instead of the current one.
    class ScriptMaster : public Agent
    {
    public:
        explicit ScriptMaster(const std::string &n) : Agent(n) {}
        Level capability(const Link &, Role) const override { return Level::C; }
        std::vector<MsgPtr> script;
        size_t cur = 0; unsigned beat = 0;
        uint64_t retract_at = NEVER, swap_at = NEVER, start = 0;
        std::vector<uint64_t> offers, accepts;   // cycles
        void offer(uint64_t t) override
        {
            if (t < start || cur >= script.size()) return;
            if (t == retract_at) return;                       // valid LOW this cycle
            ChannelState &a = m_links[0].link->chan(Channel::A);
            if (t == swap_at && cur + 1 < script.size()) { a.offer(script[cur + 1], 0); offers.push_back(t); return; }
            a.offer(script[cur], beat);
            offers.push_back(t);
        }
        void accept(uint64_t) override {}
        void commit(uint64_t t) override
        {
            ChannelState &a = m_links[0].link->chan(Channel::A);
            const Beat &b = a.accepted();
            if (!b.msg) return;
            accepts.push_back(t);
            if (b.msg != script[cur]) script.erase(script.begin() + long(cur));   // the swapped-out message is dropped
            if (b.last()) { cur++; beat = 0; } else beat = b.index + 1;
        }
        bool idle() const override { return cur >= script.size(); }
    };

    // A slave with a ready pattern: ready HIGH except on the listed cycles; counts beats.
    class PatternSlave : public Agent
    {
    public:
        explicit PatternSlave(const std::string &n) : Agent(n) {}
        Level capability(const Link &, Role) const override { return Level::C; }
        std::vector<uint64_t> not_ready;
        std::vector<std::pair<uint64_t, Beat>> got;
        bool look_at_offer = false;   // ready only when a beat is offered (legal, §4.1)
        void offer(uint64_t) override {}
        void accept(uint64_t t) override
        {
            ChannelState &a = m_links[0].link->chan(Channel::A);
            bool r = true;
            for (auto c : not_ready) if (c == t) r = false;
            if (look_at_offer && !a.valid()) r = false;
            a.setReady(r);
        }
        void commit(uint64_t t) override
        {
            const Beat &b = m_links[0].link->chan(Channel::A).accepted();
            if (b.msg) got.push_back({t, b});
        }
    };

    // Accepts on its slave link and forwards each beat on its master link: the earliest
    // re-offer is the cycle after the accept (plan §3.2, no combinational pass-through).
    class Relay : public Agent
    {
    public:
        explicit Relay(const std::string &n) : Agent(n) {}
        Level capability(const Link &, Role) const override { return Level::C; }
        std::vector<Beat> queue;
        void offer(uint64_t) override
        {
            if (queue.empty()) return;
            outLink()->chan(Channel::A).offer(queue.front().msg, queue.front().index);
        }
        void accept(uint64_t) override { inLink()->chan(Channel::A).setReady(queue.size() < 4); }
        void commit(uint64_t) override
        {
            const Beat &out = outLink()->chan(Channel::A).accepted();
            if (out.msg) queue.erase(queue.begin());
            const Beat &in = inLink()->chan(Channel::A).accepted();
            if (in.msg) queue.push_back(in);
        }
        bool idle() const override { return queue.empty(); }
        Link *inLink() const { for (auto &l : m_links) if (l.role == Role::Slave) return l.link; return nullptr; }
        Link *outLink() const { for (auto &l : m_links) if (l.role == Role::Master) return l.link; return nullptr; }
    };

    struct System
    {
        Kernel k;
        ScriptMaster *m; PatternSlave *s; Link *l;
        explicit System(uint64_t reset = 0, Level level = Level::UH, unsigned w = 8)
            : k(KernelOptions{reset, 1, DataMode::None})
        {
            m = static_cast<ScriptMaster *>(&k.add(std::unique_ptr<Agent>(new ScriptMaster("m"))));
            s = static_cast<PatternSlave *>(&k.add(std::unique_ptr<Agent>(new PatternSlave("s"))));
            LinkParams p; p.w = w; p.level = level;
            l = &k.connect("L", p, *m, *s);
        }
        MsgPtr put(unsigned size, uint64_t addr, uint64_t source)
        {
            MsgPtr p = Msg::make(Channel::A, unsigned(OpA::PutFullData), size, l->params().w);
            p->address = addr; p->source = source; p->id = k.newMsgId(); p->from_agent = m->id();
            p->deriveMask();
            return p;
        }
        MsgPtr get(unsigned size, uint64_t addr, uint64_t source)
        {
            MsgPtr p = Msg::make(Channel::A, unsigned(OpA::Get), size, l->params().w);
            p->address = addr; p->source = source; p->id = k.newMsgId();
            p->deriveMask();
            return p;
        }
    };

    // Capture violations instead of exiting.
    std::vector<std::string> &capture(ChannelState &c)
    {
        static std::vector<std::string> v;
        v.clear();
        c.onViolation = [](const ChannelState &, const std::string &what, uint64_t) { v.push_back(what); };
        return v;
    }
}

TEST(valid_independent_of_ready)
{
    // §4.1: the sender asserts valid whether or not the receiver is ready; the beat is
    // exchanged only when both are HIGH. Ready LOW on cycles 0..2, the beat transfers at 3.
    System sys;
    sys.m->script = {sys.get(3, 0x40, 1)};
    sys.s->not_ready = {0, 1, 2};
    sys.k.run(6);
    const ChannelState &a = sys.l->chan(Channel::A);
    CHECK_EQ(sys.m->offers.size(), size_t(4));          // offered at 0,1,2,3
    CHECK_EQ(sys.s->got.size(), size_t(1));
    if (!sys.s->got.empty()) CHECK_EQ(sys.s->got[0].first, uint64_t(3));
    CHECK_EQ(a.stats().busy, uint64_t(4));
    CHECK_EQ(a.stats().stall, uint64_t(3));
    CHECK_EQ(a.stats().beats, uint64_t(1));
    CHECK(sys.m->idle());
    MsgPtr g = sys.s->got.empty() ? nullptr : sys.s->got[0].second.msg;
    if (g) { CHECK_EQ(g->t_first_offer, uint64_t(0)); CHECK_EQ(g->t_first_accept, uint64_t(3)); CHECK_EQ(g->t_last_accept, uint64_t(3)); }
    CHECK(!a.valid());                                  // nothing left to offer
}

TEST(ready_may_look_at_valid)
{
    // §4.1: ready may depend on valid and the control signals of the same cycle.
    System sys;
    sys.s->look_at_offer = true;
    sys.m->script = {sys.get(3, 0x40, 1), sys.get(3, 0x48, 2)};
    sys.m->start = 2;
    sys.k.run(6);
    CHECK_EQ(sys.s->got.size(), size_t(2));
    if (sys.s->got.size() == 2) { CHECK_EQ(sys.s->got[0].first, uint64_t(2)); CHECK_EQ(sys.s->got[1].first, uint64_t(3)); }
}

TEST(burst_figure_6)
{
    // Figure 6: F (size 5, 4 beats) rejected once, then accepted beat by beat; G (size 0);
    // H (size 6) rejected on its first beat, never sent again; I, J (Get size 4), K.
    System sys;
    MsgPtr F = sys.put(5, 0x100, 0), G = sys.put(0, 0x200, 1), H = sys.put(6, 0x300, 2),
           I = sys.put(2, 0x400, 3), J = sys.get(4, 0x500, 4), K = sys.put(1, 0x600, 5);
    sys.m->script = {F, G, H, I, J, K};
    sys.s->not_ready = {0, 2, 7};     // F-0 rejected at 0; F-1 rejected at 2; H-0 rejected at 7
    sys.m->swap_at = 8;               // after H's rejection the master offers I instead (allowed)
    sys.k.run(14);
    const ChannelState &a = sys.l->chan(Channel::A);
    // accepted beats: F-0@1 F-1@3 F-2@4 F-3@5 G@6 (H-0@7 rejected) I@8 J@9 K@10
    const uint64_t expect_cycles[8] = {1, 3, 4, 5, 6, 8, 9, 10};
    const MsgPtr expect_msg[8] = {F, F, F, F, G, I, J, K};
    CHECK_EQ(a.stats().beats, uint64_t(8));                    // F(4) G I J K; H never accepted
    CHECK_EQ(sys.s->got.size(), size_t(8));
    for (size_t i = 0; i < 8 && i < sys.s->got.size(); i++)
    {
        CHECK_EQ(sys.s->got[i].first, expect_cycles[i]);
        CHECK(sys.s->got[i].second.msg == expect_msg[i]);
        CHECK_EQ(sys.s->got[i].second.index, unsigned(i < 4 ? i : 0));   // F in order, no interleaving
    }
    CHECK_EQ(F->t_first_offer, uint64_t(0)); CHECK_EQ(F->t_first_accept, uint64_t(1)); CHECK_EQ(F->t_last_accept, uint64_t(5));
    CHECK_EQ(H->t_first_offer, uint64_t(7));
    CHECK_EQ(H->t_first_accept, NEVER);                        // H was retracted before its first accept
    CHECK_EQ(a.stats().busy, uint64_t(11)); CHECK_EQ(a.stats().stall, uint64_t(3));
    CHECK(!a.inProgress());
    CHECK(sys.m->idle());
}

TEST(burst_lock_retract_after_first_accept_forbidden)
{
    // §4.1: once beat 0 is accepted the burst is in progress; offering a different message
    // before the last beat is a violation. Offering nothing (valid LOW) is allowed.
    System sys;
    MsgPtr F = sys.put(5, 0x100, 0), G = sys.put(0, 0x200, 1);
    sys.m->script = {F, G};
    sys.m->retract_at = 2;            // valid LOW mid-burst: legal
    auto &viol = capture(sys.l->chan(Channel::A));
    sys.k.run(3);
    CHECK(viol.empty());
    CHECK(sys.l->chan(Channel::A).inProgress());
    CHECK_EQ(sys.l->chan(Channel::A).nextBeat(), 2u);         // F-0 at 0, F-1 at 1, nothing at 2
    sys.m->swap_at = 3;               // offer G while F is in progress: a violation
    sys.k.run(1);
    CHECK_EQ(viol.size(), size_t(1));
    if (!viol.empty()) CHECK(viol[0].find("burst in progress") != std::string::npos);
}

TEST(burst_lock_wrong_beat_and_changed_control)
{
    System sys;
    MsgPtr F = sys.put(5, 0x100, 0);
    sys.m->script = {F};
    auto &viol = capture(sys.l->chan(Channel::A));
    sys.k.run(2);                     // F-0 and F-1 accepted
    CHECK(viol.empty());
    sys.m->beat = 3;                  // skip a beat
    sys.k.run(1);
    CHECK_EQ(viol.size(), size_t(1));
    if (!viol.empty()) CHECK(viol[0].find("expects beat 2") != std::string::npos);
    // control fields must stay constant during the burst (§4.1)
    System sysc;
    MsgPtr Fc = sysc.put(5, 0x100, 0);
    sysc.m->script = {Fc};
    auto &vc = capture(sysc.l->chan(Channel::A));
    sysc.k.run(2);
    Fc->address = 0x108;
    sysc.k.run(1);
    CHECK_EQ(vc.size(), size_t(1));
    if (!vc.empty()) CHECK(vc[0].find("control fields changed") != std::string::npos);
    // a beat > 0 with no burst in progress
    System sys2;
    MsgPtr P = sys2.put(5, 0x100, 0);
    sys2.m->script = {P}; sys2.m->beat = 1;
    auto &v2 = capture(sys2.l->chan(Channel::A));
    sys2.k.run(1);
    CHECK_EQ(v2.size(), size_t(1));
    if (!v2.empty()) CHECK(v2[0].find("no burst in progress") != std::string::npos);
    // a message for another channel, and a beat index out of range
    System sys3;
    MsgPtr D = Msg::make(Channel::D, 1, 3, 8);
    sys3.m->script = {D};
    auto &v3 = capture(sys3.l->chan(Channel::A));
    sys3.k.run(1);
    CHECK_EQ(v3.size(), size_t(1));
    if (!v3.empty()) CHECK(v3[0].find("offered on A") != std::string::npos);
}

TEST(burst_lock_survives_interleaved_beat)
{
    // §4.1: an interleaved beat is a violation, reported when it is offered; in report mode the
    // run carries on and the beat may be accepted. It must not start, advance or end the lock:
    // the burst's own later beats are then legal and raise nothing. Schedule: F-0 F-1 G F-2 F-3,
    // with G a single-beat message (its last beat once cleared the lock).
    struct Sched : public Agent
    {
        explicit Sched(const std::string &n) : Agent(n) {}
        Level capability(const Link &, Role) const override { return Level::C; }
        std::vector<Beat> plan;
        void offer(uint64_t t) override
        {
            if (t < plan.size()) m_links[0].link->chan(Channel::A).offer(plan[t].msg, plan[t].index);
        }
        void accept(uint64_t) override {}
        void commit(uint64_t) override {}
    };
    Kernel k(KernelOptions{0, 1, DataMode::None});
    Sched *m = static_cast<Sched *>(&k.add(std::unique_ptr<Agent>(new Sched("m"))));
    PatternSlave *s = static_cast<PatternSlave *>(&k.add(std::unique_ptr<Agent>(new PatternSlave("s"))));
    LinkParams p; p.w = 8; p.level = Level::UH;
    Link &l = k.connect("L", p, *m, *s);
    auto mk = [&](unsigned size, uint64_t addr, uint64_t src) {
        MsgPtr x = Msg::make(Channel::A, unsigned(OpA::PutFullData), size, 8);
        x->address = addr; x->source = src; x->deriveMask(); return x;
    };
    MsgPtr F = mk(5, 0x100, 0), G = mk(0, 0x200, 1);
    m->plan = {Beat{F, 0}, Beat{F, 1}, Beat{G, 0}, Beat{F, 2}, Beat{F, 3}};
    auto &viol = capture(l.chan(Channel::A));
    k.run(5);
    CHECK_EQ(viol.size(), size_t(1));                           // G only
    if (!viol.empty()) CHECK(viol[0].find("another message was offered") != std::string::npos);
    CHECK_EQ(s->got.size(), size_t(5));
    CHECK(!l.chan(Channel::A).inProgress());                    // F completed at its own last beat
    CHECK_EQ(F->t_last_accept, uint64_t(4));
}

TEST(one_cycle_minimum_relay)
{
    // plan §3.2: a beat accepted at t is re-offered by its receiver at t+1 at the earliest.
    Kernel k(KernelOptions{0, 1, DataMode::None});
    auto *m = static_cast<ScriptMaster *>(&k.add(std::unique_ptr<Agent>(new ScriptMaster("m"))));
    auto *r = static_cast<Relay *>(&k.add(std::unique_ptr<Agent>(new Relay("r"))));
    auto *s = static_cast<PatternSlave *>(&k.add(std::unique_ptr<Agent>(new PatternSlave("s"))));
    LinkParams p; p.level = Level::UH;
    Link &l1 = k.connect("L1", p, *m, *r);
    Link &l2 = k.connect("L2", p, *r, *s);
    MsgPtr F = Msg::make(Channel::A, 0, 5, 8); F->address = 0x100; F->deriveMask();
    MsgPtr G = Msg::make(Channel::A, 4, 3, 8); G->address = 0x200; G->deriveMask();
    m->script = {F, G};
    uint64_t n = k.runUntilIdle(50);
    CHECK(n < 50);
    CHECK_EQ(l1.chan(Channel::A).stats().beats, uint64_t(5));
    CHECK_EQ(l2.chan(Channel::A).stats().beats, uint64_t(5));
    CHECK_EQ(s->got.size(), size_t(5));
    // link 1: F-0..3 at 0..3, G at 4; link 2: each one cycle later, 1..5
    for (size_t i = 0; i < s->got.size(); i++) CHECK_EQ(s->got[i].first, uint64_t(i + 1));
    // the same Msg object travels both links: its timestamps are end to end, the per-link
    // latency comes from the channel (Msg.h)
    CHECK_EQ(F->t_first_offer, uint64_t(0));
    CHECK_EQ(F->t_first_accept, uint64_t(0));               // first accepted on link 1 at 0
    CHECK_EQ(F->t_last_accept, uint64_t(4));                // last beat on link 2 at 4
    k.finishStats();
    const LatencyStats *l1put = k.stats().message("L1", Channel::A, 0), *l2put = k.stats().message("L2", Channel::A, 0);
    CHECK(l1put && l2put);
    if (l1put && l2put) { CHECK_EQ(l1put->max, uint64_t(3)); CHECK_EQ(l2put->max, uint64_t(3)); }   // 4 beats back to back on each link
    // the same system evaluated with the relay registered FIRST gives the same timing: the
    // phase order, not the agent order, decides (no pass-through)
    Kernel k2(KernelOptions{0, 1, DataMode::None});
    auto *r2 = static_cast<Relay *>(&k2.add(std::unique_ptr<Agent>(new Relay("r"))));
    auto *s2 = static_cast<PatternSlave *>(&k2.add(std::unique_ptr<Agent>(new PatternSlave("s"))));
    auto *m2 = static_cast<ScriptMaster *>(&k2.add(std::unique_ptr<Agent>(new ScriptMaster("m"))));
    k2.connect("L1", p, *m2, *r2);
    k2.connect("L2", p, *r2, *s2);
    MsgPtr F2 = Msg::make(Channel::A, 0, 5, 8); F2->address = 0x100; F2->deriveMask();
    m2->script = {F2};
    k2.runUntilIdle(50);
    CHECK_EQ(s2->got.size(), size_t(4));
    for (size_t i = 0; i < s2->got.size(); i++) CHECK_EQ(s2->got[i].first, uint64_t(i + 1));
}

TEST(reset_valid_low)
{
    // §3.2.2: valid LOW during reset. A well-behaved master waits; a misbehaving one offers
    // during reset and the kernel reports it on that channel.
    System good(10);
    good.m->script = {good.get(3, 0x40, 1)};
    good.m->start = 10;
    auto &v = capture(good.l->chan(Channel::A));
    CHECK(good.k.inReset());
    good.k.run(10);
    CHECK(!good.k.inReset());
    CHECK(v.empty());
    CHECK_EQ(good.l->chan(Channel::A).stats().busy, uint64_t(0));
    good.k.run(1);
    CHECK_EQ(good.s->got.size(), size_t(1));
    if (!good.s->got.empty()) CHECK_EQ(good.s->got[0].first, uint64_t(10));

    System bad(10);
    bad.m->script = {bad.get(3, 0x40, 1)};
    bad.m->start = 7;
    auto &vb = capture(bad.l->chan(Channel::A));
    bad.k.run(8);
    CHECK_EQ(vb.size(), size_t(1));
    if (!vb.empty()) CHECK(vb[0].find("valid HIGH during reset") != std::string::npos);
    // the default is 100 cycles standalone (plan §3.2)
    CHECK_EQ(KernelOptions().reset_cycles, uint64_t(100));
}

TEST(determinism)
{
    // Two runs of a randomized master/slave pair give the same transcript.
    struct RandomMaster : Agent
    {
        explicit RandomMaster(const std::string &n) : Agent(n) {}
        Level capability(const Link &, Role) const override { return Level::C; }
        MsgPtr cur; unsigned beat = 0; int sent = 0;
        void offer(uint64_t) override
        {
            if (m_kernel->inReset()) return;
            if (!cur)
            {
                if (sent >= 20 || (rng()() % 3) == 0) return;
                unsigned size = unsigned(rng()() % 6);
                bool data = (rng()() & 1) != 0;
                cur = Msg::make(Channel::A, data ? 0u : 4u, size, 8);
                cur->address = (rng()() % 64) << 6; cur->source = rng()() % 4; cur->id = m_kernel->newMsgId();
                cur->deriveMask(); beat = 0;
            }
            m_links[0].link->chan(Channel::A).offer(cur, beat);
        }
        void accept(uint64_t) override {}
        void commit(uint64_t) override
        {
            const Beat &b = m_links[0].link->chan(Channel::A).accepted();
            if (!b.msg) return;
            if (b.last()) { cur = nullptr; sent++; } else beat = b.index + 1;
        }
        bool idle() const override { return !cur && sent >= 20; }
    };
    struct RandomSlave : Agent
    {
        explicit RandomSlave(const std::string &n) : Agent(n) {}
        Level capability(const Link &, Role) const override { return Level::C; }
        void offer(uint64_t) override {}
        void accept(uint64_t) override { m_links[0].link->chan(Channel::A).setReady((rng()() & 1) != 0); }
        void commit(uint64_t) override {}
    };
    auto runOnce = [](std::ostream &out) -> uint64_t
    {
        Kernel k(KernelOptions{3, 1234, DataMode::None});
        Agent &m = k.add(std::unique_ptr<Agent>(new RandomMaster("m")));
        Agent &s = k.add(std::unique_ptr<Agent>(new RandomSlave("s")));
        LinkParams p; p.level = Level::UH;
        k.connect("L", p, m, s);
        k.enableTranscript(out);
        k.runUntilIdle(5000);
        return k.transcript()->lines();
    };
    std::ostringstream a, b;
    uint64_t la = runOnce(a), lb = runOnce(b);
    CHECK_EQ(la, lb);
    CHECK(la > 20);
    CHECK_EQ(a.str(), b.str());
    CHECK(a.str().find("PutFullData") != std::string::npos || a.str().find("Get") != std::string::npos);
}

TEST(elaboration_checks)
{
    // A capability below the link's level, a bad parameter: both refused with a message.
    struct ULOnly : NullSlave
    {
        explicit ULOnly(const std::string &n) : NullSlave(n) {}
        Level capability(const Link &, Role) const override { return Level::UL; }
    };
    // elaborate() calls fatal (exit 2) on failure, so the checks that must fail run in a child.
    // Here: the passing path and the validate() message that elaborate would print.
    Kernel k;
    Agent &m = k.add(std::unique_ptr<Agent>(new NullMaster("m")));
    Agent &s = k.add(std::unique_ptr<Agent>(new ULOnly("s")));
    LinkParams p; p.level = Level::UL;
    Link &l = k.connect("L", p, m, s);
    k.elaborate();
    CHECK_EQ(int(s.capability(l, Role::Slave)), int(Level::UL));
    CHECK_EQ(l.masterId(), 0); CHECK_EQ(l.slaveId(), 1);
    CHECK(k.link("L") == &l); CHECK(k.link("nope") == nullptr); CHECK(k.agent("m") == &m);
    LinkParams bad; bad.w = 12;
    CHECK(!bad.validate().empty());
}

TL_TEST_MAIN("test_kernel")
