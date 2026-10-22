/*
 * File  :      test_coherence.cpp
 * TL-COH: CoherenceCheck (plan §3.6) driven by scripted agents, so every permission change happens at
 * a known cycle. Two masters M1 and M2, each on its own TL-C link (L1, L2: w=64 a=32 z=3 o=2 i=2) to
 * a scripted manager half (S1, S2); the checker attaches to both links, as plan §3.6 places it (the
 * masters' own links). X = block 0x1000. Every size-6 message is one beat on w = 64.
 *
 *   C-SWMR   two Tips on one block; a Tip and a Branch; two Branches are legal
 *   C-GRANT  a Grant toT offered while another master's ProbeAck is outstanding; the legal order
 *   denied   a denied Grant changes no permission and raises no violation
 *   lowering ProbeAck[Data] / Release[Data] lower the permission at their first beat
 *   C-VALUE  a load returning a stale value after another agent's store; the initial image
 *   modes    report mode (onViolation, counters, the run continues) and fatal mode (child: exit 2,
 *            "TileLink: coherence <rule>: ...")
 *   attach   TL-C only, leaf masters only (ConfigError), attachAll, auto-attach at elaboration, of()
 *   chaining a Monitor, a ValueCheck and the CoherenceCheck on the same link, each reporting
 */
#include "Subprocess.h"
#include "ScriptedSlave.h"
#include "tilelink/CoherenceCheck.h"
#include "tilelink/Monitor.h"
#include "tilelink/ValueCheck.h"
#include <memory>

using namespace octopus::tl;
using namespace octopus::tl::fixture;

namespace
{
    const uint64_t X = 0x1000;

    LinkParams lpC()
    {
        LinkParams p; p.w = 64; p.a = 32; p.z = 3; p.o = 2; p.i = 2; p.level = Level::C;
        return p;
    }

    struct Sys
    {
        Kernel k;
        ScriptedMaster *M1, *M2;
        ScriptedSlave *S1, *S2;
        Link *L1, *L2;
        CoherenceCheck cc;
        std::vector<CoherenceViolation> v;
        explicit Sys(bool report = true, DataMode mode = DataMode::None) : k(KernelOptions{100, 1, mode}), cc(64)
        {
            M1 = static_cast<ScriptedMaster *>(&k.add(std::unique_ptr<Agent>(new ScriptedMaster("M1"))));
            M2 = static_cast<ScriptedMaster *>(&k.add(std::unique_ptr<Agent>(new ScriptedMaster("M2"))));
            S1 = static_cast<ScriptedSlave *>(&k.add(std::unique_ptr<Agent>(new ScriptedSlave("S1"))));
            S2 = static_cast<ScriptedSlave *>(&k.add(std::unique_ptr<Agent>(new ScriptedSlave("S2"))));
            L1 = &k.connect("L1", lpC(), *M1, *S1);
            L2 = &k.connect("L2", lpC(), *M2, *S2);
            cc.attach(*L1);
            cc.attach(*L2);
            if (report) cc.onViolation = [this](const CoherenceViolation &x) { v.push_back(x); };
        }
        DataMode mode() const { return k.options().data_mode; }
        // ---- the TL-C messages, each offered once at an absolute cycle (the receivers are always ready
        // unless a test says otherwise)
        MsgPtr acquire(ScriptedMaster *m, uint64_t t, uint64_t src, uint64_t addr, Grow g)
        {
            MsgPtr a = makeMsg(k, Channel::A, unsigned(OpA::AcquireBlock), 6, 64, addr, src, mode(), unsigned(g));
            m->at(t, Channel::A, a);
            return a;
        }
        MsgPtr grant(ScriptedSlave *s, uint64_t t, uint64_t src, uint64_t sink, Cap c, bool data = true, bool denied = false)
        {
            MsgPtr d = makeMsg(k, Channel::D, unsigned(data ? OpD::GrantData : OpD::Grant), 6, 64, 0, src, mode(), unsigned(c));
            d->sink = sink;
            d->denied = denied;
            if (denied && data) for (auto &cr : d->corrupt) cr = 1;   // §4.4
            s->at(t, Channel::D, d);
            return d;
        }
        void grantAck(ScriptedMaster *m, uint64_t t, uint64_t sink)
        {
            MsgPtr e = makeMsg(k, Channel::E, unsigned(OpE::GrantAck), 0, 64, 0, 0, mode());
            e->sink = sink;
            m->at(t, Channel::E, e);
        }
        void probe(ScriptedSlave *s, uint64_t t, uint64_t src, uint64_t addr, Cap c)
        { s->at(t, Channel::B, makeMsg(k, Channel::B, unsigned(OpB::ProbeBlock), 6, 64, addr, src, mode(), unsigned(c))); }
        void probeAck(ScriptedMaster *m, uint64_t t, uint64_t addr, unsigned param, bool data)
        { m->at(t, Channel::C, makeMsg(k, Channel::C, unsigned(data ? OpC::ProbeAckData : OpC::ProbeAck), 6, 64, addr, 0, mode(), param)); }
        void release(ScriptedMaster *m, uint64_t t, uint64_t src, uint64_t addr, unsigned param, bool data)
        { m->at(t, Channel::C, makeMsg(k, Channel::C, unsigned(data ? OpC::ReleaseData : OpC::Release), 6, 64, addr, src, mode(), param)); }
        void releaseAck(ScriptedSlave *s, uint64_t t, uint64_t src)
        { s->at(t, Channel::D, makeMsg(k, Channel::D, unsigned(OpD::ReleaseAck), 6, 64, 0, src, mode())); }
        // A complete acquisition: Acquire at t, Grant[Data] at t + 1, GrantAck at t + 2.
        void own(ScriptedMaster *m, ScriptedSlave *s, uint64_t t, Grow g, Cap c, uint64_t sink = 0)
        {
            acquire(m, t, 0, X, g);
            grant(s, t + 1, 0, sink, c);
            grantAck(m, t + 2, sink);
        }
        void run(uint64_t until) { k.run(until); }
        std::string fired() const
        {
            std::string s;
            for (auto &x : v) s += (s.empty() ? "" : " ") + x.rule + "@" + std::to_string(x.cycle);
            return s;
        }
    };
}

// ------------------------------------------------------------------ C-SWMR
TEST(swmr_two_tips_same_cycle)
{
    // Both Grants toT accepted in cycle 101: neither master held the block when the Grants were
    // offered (no C-GRANT); the second accept finds L1 already at T.
    Sys s;
    s.acquire(s.M1, 100, 0, X, Grow::NtoT); s.acquire(s.M2, 100, 0, X, Grow::NtoT);
    s.grant(s.S1, 101, 0, 0, Cap::toT); s.grant(s.S2, 101, 0, 0, Cap::toT);
    s.grantAck(s.M1, 102, 0); s.grantAck(s.M2, 102, 0);
    s.run(110);
    CHECK_EQ(s.fired(), std::string("C-SWMR@101"));
    CHECK_EQ(s.cc.permission("L1", X), 'T');
    CHECK_EQ(s.cc.permission("L2", X), 'T');
    CHECK_EQ(s.cc.counters().grants, uint64_t(2));
    CHECK_EQ(s.cc.counters().violations, uint64_t(1));
    if (!s.v.empty())
    {
        CHECK_EQ(s.v[0].block, X);
        CHECK(s.v[0].detail.find("link L2 granted T") != std::string::npos);
        CHECK(s.v[0].detail.find("link L1 holds T") != std::string::npos);
    }
}

TEST(swmr_two_tips_staggered)
{
    // M1 is a Tip from 101; M2's Grant toT offered at 104 is already the manager's error (C-GRANT),
    // and its accept the SWMR violation.
    Sys s;
    s.own(s.M1, s.S1, 100, Grow::NtoT, Cap::toT);
    s.acquire(s.M2, 103, 0, X, Grow::NtoT); s.grant(s.S2, 104, 0, 0, Cap::toT); s.grantAck(s.M2, 105, 0);
    s.run(110);
    CHECK_EQ(s.fired(), std::string("C-GRANT@104 C-SWMR@104"));
}

TEST(swmr_tip_and_branch)
{
    {   // same cycle: L1 toT, L2 toB
        Sys s;
        s.acquire(s.M1, 100, 0, X, Grow::NtoT); s.acquire(s.M2, 100, 0, X, Grow::NtoB);
        s.grant(s.S1, 101, 0, 0, Cap::toT); s.grant(s.S2, 101, 0, 0, Cap::toB);
        s.grantAck(s.M1, 102, 0); s.grantAck(s.M2, 102, 0);
        s.run(110);
        CHECK_EQ(s.fired(), std::string("C-SWMR@101"));
        CHECK(!s.v.empty() && s.v[0].detail.find("link L2 granted B") != std::string::npos);
    }
    {   // M2 a Branch first, then M1 granted toT without probing it
        Sys s;
        s.own(s.M2, s.S2, 100, Grow::NtoB, Cap::toB);
        s.acquire(s.M1, 103, 0, X, Grow::NtoT); s.grant(s.S1, 104, 0, 0, Cap::toT); s.grantAck(s.M1, 105, 0);
        s.run(110);
        CHECK_EQ(s.fired(), std::string("C-GRANT@104 C-SWMR@104"));
    }
    {   // M1 a Tip first, then M2 granted toB without probing it (a toB Grant beside a Tip)
        Sys s;
        s.own(s.M1, s.S1, 100, Grow::NtoT, Cap::toT);
        s.acquire(s.M2, 103, 0, X, Grow::NtoB); s.grant(s.S2, 104, 0, 0, Cap::toB); s.grantAck(s.M2, 105, 0);
        s.run(110);
        CHECK_EQ(s.fired(), std::string("C-GRANT@104 C-SWMR@104"));
    }
}

TEST(swmr_two_branches_legal)
{
    Sys s;
    s.own(s.M1, s.S1, 100, Grow::NtoB, Cap::toB);
    s.own(s.M2, s.S2, 103, Grow::NtoB, Cap::toB);
    s.run(110);
    CHECK_EQ(s.fired(), std::string(""));
    CHECK_EQ(s.cc.permission("L1", X), 'B');
    CHECK_EQ(s.cc.permission("L2", X), 'B');
    CHECK_EQ(s.cc.permission("L1", X + 0x3f), 'B');   // any byte address of the block
    CHECK_EQ(s.cc.permission("L1", X + 0x40), 'N');   // the next block
    CHECK_EQ(s.cc.permission("L3", X), 'N');          // an unknown link
}

// ------------------------------------------------------------------ C-GRANT
TEST(grant_while_probeack_outstanding)
{
    // M2 holds B. M1 acquires NtoT; the manager probes M2 toN at 104 and offers M1's GrantData toT at
    // 105, before M2's ProbeAck (accepted at 105): C-GRANT at 105. M1 rejects that offer (d_ready
    // LOW) and accepts the re-offer at 106, after the ProbeAck: no C-SWMR.
    Sys s;
    s.own(s.M2, s.S2, 100, Grow::NtoB, Cap::toB);
    s.acquire(s.M1, 103, 0, X, Grow::NtoT);
    s.probe(s.S2, 104, 0, X, Cap::toN);
    MsgPtr g = s.grant(s.S1, 105, 0, 0, Cap::toT);
    s.M1->notReady(Channel::D, 105);
    s.S1->at(106, Channel::D, g);
    s.probeAck(s.M2, 105, X, unsigned(Prune::BtoN), false);
    s.grantAck(s.M1, 107, 0);
    s.run(112);
    CHECK_EQ(s.fired(), std::string("C-GRANT@105"));
    CHECK(!s.v.empty() && s.v[0].detail.find("link L2 still holds B") != std::string::npos);
    CHECK_EQ(s.cc.permission("L1", X), 'T');
    CHECK_EQ(s.cc.permission("L2", X), 'N');
    CHECK_EQ(s.cc.counters().probe_acks, uint64_t(1));
}

TEST(grant_after_probeack_legal)
{
    // The same flow in the legal order (Figure 29 step 4): the Grant is offered the cycle after the
    // ProbeAck[Data]; M2 a Tip with dirty data answering ProbeAckData TtoN.
    Sys s;
    s.own(s.M2, s.S2, 100, Grow::NtoT, Cap::toT);
    s.acquire(s.M1, 103, 0, X, Grow::NtoT);
    s.probe(s.S2, 104, 0, X, Cap::toN);
    s.probeAck(s.M2, 105, X, unsigned(Prune::TtoN), true);
    s.grant(s.S1, 106, 0, 0, Cap::toT);
    s.grantAck(s.M1, 107, 0);
    s.run(112);
    CHECK_EQ(s.fired(), std::string(""));
    CHECK_EQ(s.cc.permission("L1", X), 'T');
    CHECK_EQ(s.cc.permission("L2", X), 'N');
}

TEST(grant_after_probe_tob_legal)
{
    // A reader joins a Tip: ProbeAckData TtoB leaves M2 a Branch, then M1 gets toB.
    Sys s;
    s.own(s.M2, s.S2, 100, Grow::NtoT, Cap::toT);
    s.acquire(s.M1, 103, 0, X, Grow::NtoB);
    s.probe(s.S2, 104, 0, X, Cap::toB);
    s.probeAck(s.M2, 105, X, unsigned(Prune::TtoB), true);
    s.grant(s.S1, 106, 0, 0, Cap::toB);
    s.grantAck(s.M1, 107, 0);
    s.run(112);
    CHECK_EQ(s.fired(), std::string(""));
    CHECK_EQ(s.cc.permission("L1", X), 'B');
    CHECK_EQ(s.cc.permission("L2", X), 'B');
}

// ------------------------------------------------------------------ denied Grant (§9.3.7)
TEST(denied_grant_changes_nothing)
{
    {   // M2 a Tip; M1's Grant toT denied: no C-GRANT, no C-SWMR, M1 still N
        Sys s;
        s.own(s.M2, s.S2, 100, Grow::NtoT, Cap::toT);
        s.acquire(s.M1, 103, 0, X, Grow::NtoT);
        s.grant(s.S1, 104, 0, 0, Cap::toT, true, true);
        s.grantAck(s.M1, 105, 0);
        s.run(110);
        CHECK_EQ(s.fired(), std::string(""));
        CHECK_EQ(s.cc.permission("L1", X), 'N');
        CHECK_EQ(s.cc.permission("L2", X), 'T');
        CHECK_EQ(s.cc.counters().grants, uint64_t(2));   // counted, not applied
    }
    {   // a Branch whose BtoT upgrade is denied stays a Branch, beside another Branch
        Sys s;
        s.own(s.M1, s.S1, 100, Grow::NtoB, Cap::toB);
        s.own(s.M2, s.S2, 100, Grow::NtoB, Cap::toB);
        s.acquire(s.M1, 103, 0, X, Grow::BtoT);
        s.grant(s.S1, 104, 0, 0, Cap::toT, false, true);
        s.grantAck(s.M1, 105, 0);
        s.run(110);
        CHECK_EQ(s.fired(), std::string(""));
        CHECK_EQ(s.cc.permission("L1", X), 'B');
    }
}

// ------------------------------------------------------------------ lowering
TEST(release_and_probeack_lower)
{
    // M1 a Tip releases (ReleaseData TtoN): from its first beat M2 may be a Tip. A Report param
    // (BtoB) never raises: a ProbeAck BtoB from N leaves N (the monitor's M-PERM-2 would flag it).
    Sys s;
    s.own(s.M1, s.S1, 100, Grow::NtoT, Cap::toT);
    s.release(s.M1, 103, 0, X, unsigned(Prune::TtoN), true);
    s.releaseAck(s.S1, 104, 0);
    s.own(s.M2, s.S2, 104, Grow::NtoT, Cap::toT);
    s.probe(s.S1, 108, 0, X, Cap::toB);
    s.probeAck(s.M1, 109, X, unsigned(Report::BtoB), false);
    s.run(115);
    CHECK_EQ(s.fired(), std::string(""));
    CHECK_EQ(s.cc.permission("L1", X), 'N');
    CHECK_EQ(s.cc.permission("L2", X), 'T');
    CHECK_EQ(s.cc.counters().releases, uint64_t(1));
    CHECK_EQ(s.cc.counters().probe_acks, uint64_t(1));
}

// ------------------------------------------------------------------ C-VALUE
TEST(value_stale_load)
{
    Sys s;
    const unsigned m1 = unsigned(s.M1->id()), m2 = unsigned(s.M2->id());
    uint8_t a[4] = {1, 2, 3, 4}, b[4] = {9, 2, 3, 4}, any[4] = {7, 7, 7, 7};
    s.cc.loadPerformed(m2, X + 0x100, any, 4, 105);    // never stored: any value
    CHECK_EQ(s.cc.counters().bytes_checked, uint64_t(0));
    s.cc.storePerformed(m1, X, a, 4, 110);
    s.cc.loadPerformed(m2, X, a, 4, 111);              // the latest store
    s.cc.storePerformed(m1, X, b, 1, 112);             // byte X becomes 9
    s.cc.loadPerformed(m2, X, a, 4, 113);              // stale: still 1
    CHECK_EQ(s.fired(), std::string("C-VALUE@113"));
    if (!s.v.empty())
    {
        CHECK_EQ(s.v[0].address, X);
        CHECK_EQ(s.v[0].block, X);
        CHECK(s.v[0].detail.find("M2 load of 4 byte(s) at 0x1000 returned 0x1") != std::string::npos);
        CHECK(s.v[0].detail.find("latest store wrote 0x9 (M1 at cycle 112)") != std::string::npos);
        CHECK(s.v[0].detail.find("1 byte(s) differ") != std::string::npos);
    }
    s.cc.loadPerformed(m2, X, b, 4, 114);              // the current value
    CHECK_EQ(s.cc.counters().loads, uint64_t(4));
    CHECK_EQ(s.cc.counters().stores, uint64_t(2));
    CHECK_EQ(s.cc.counters().bytes_checked, uint64_t(12));
    CHECK_EQ(s.cc.counters().violations, uint64_t(1));
}

TEST(value_initial_image)
{
    Sys s;
    uint8_t zero[8] = {0}, five[8] = {5, 0, 0, 0, 0, 0, 0, 0};
    s.cc.setInitial(X, zero, 8);
    s.cc.loadPerformed(0, X, zero, 8, 101);
    s.cc.loadPerformed(0, X, five, 8, 102);
    CHECK_EQ(s.fired(), std::string("C-VALUE@102"));
    CHECK(!s.v.empty() && s.v[0].detail.find("initial image") != std::string::npos);
}

// ------------------------------------------------------------------ report mode, fatal mode
TEST(report_mode_continues)
{
    // Report mode: every violation reaches onViolation and the checker keeps tracking.
    Sys s;
    s.acquire(s.M1, 100, 0, X, Grow::NtoT); s.acquire(s.M2, 100, 0, X, Grow::NtoT);
    s.grant(s.S1, 101, 0, 0, Cap::toT); s.grant(s.S2, 101, 0, 0, Cap::toT);
    s.grantAck(s.M1, 102, 0); s.grantAck(s.M2, 102, 0);
    s.release(s.M1, 104, 0, X, unsigned(Prune::TtoN), true);   // M1 gives up: only M2 is left
    s.releaseAck(s.S1, 105, 0);
    s.acquire(s.M1, 107, 1, X, Grow::NtoB); s.grant(s.S1, 108, 1, 1, Cap::toB); s.grantAck(s.M1, 109, 1);
    s.run(115);
    CHECK_EQ(s.fired(), std::string("C-SWMR@101 C-GRANT@108 C-SWMR@108"));
    CHECK_EQ(s.cc.counters().violations, uint64_t(3));
    CHECK_EQ(s.cc.permission("L1", X), 'B');
}

CHILD_CASE(fatal_swmr)
{
    Sys s(false);
    s.acquire(s.M1, 100, 0, X, Grow::NtoT); s.acquire(s.M2, 100, 0, X, Grow::NtoT);
    s.grant(s.S1, 101, 0, 0, Cap::toT); s.grant(s.S2, 101, 0, 0, Cap::toT);
    s.run(110);
}
CHILD_CASE(fatal_grant)
{
    Sys s(false);
    s.own(s.M2, s.S2, 100, Grow::NtoB, Cap::toB);
    s.acquire(s.M1, 103, 0, X, Grow::NtoT); s.grant(s.S1, 104, 0, 0, Cap::toT);
    s.run(110);
}
CHILD_CASE(fatal_value)
{
    Sys s(false);
    uint8_t a = 1, b = 2;
    s.cc.storePerformed(0, X, &a, 1, 10);
    s.cc.loadPerformed(1, X, &b, 1, 11);
}

TEST(fatal_mode_exit_2)
{
    struct { const char *c, *want; } cases[] = {
        {"fatal_swmr", "TileLink: coherence C-SWMR: cycle=101 block=0x1000 addr=0x1000: link L2 granted T"},
        {"fatal_grant", "TileLink: coherence C-GRANT: cycle=104 block=0x1000 addr=0x1000: GrantData toT offered to link L1"},
        {"fatal_value", "TileLink: coherence C-VALUE: cycle=11 block=0x1000 addr=0x1000: M2 load"},
    };
    for (auto &c : cases)
    {
        tltest::ChildResult r = tltest::runCase(c.c);
        CHECK_EQ(r.status, 2);
        std::string line = r.firstErrLine();
        CHECK(line.compare(0, std::string(c.want).size(), c.want) == 0);
        if (line.compare(0, std::string(c.want).size(), c.want) != 0) std::printf("  %s: %s\n", c.c, line.c_str());
    }
}

// ------------------------------------------------------------------ attaching
TEST(attach_rules)
{
    {   // a link below TL-C has no permissions to track
        Kernel k(KernelOptions{100, 1, DataMode::None});
        Agent &m = k.add(std::unique_ptr<Agent>(new NullMaster("m")));
        Agent &sl = k.add(std::unique_ptr<Agent>(new NullSlave("s")));
        LinkParams p = lpC(); p.level = Level::UH;
        Link &l = k.connect("U", p, m, sl);
        CoherenceCheck cc;
        bool thrown = false;
        try { cc.attach(l); } catch (ConfigError &e) { thrown = std::string(e.what()).find("not TL-C") != std::string::npos; }
        CHECK(thrown);
    }
    {   // m1, m2 -> hub -> s: the hub's output link carries both masters
        Kernel k(KernelOptions{100, 1, DataMode::None});
        Agent &m1 = k.add(std::unique_ptr<Agent>(new NullMaster("m1")));
        Agent &m2 = k.add(std::unique_ptr<Agent>(new NullMaster("m2")));
        Agent &hub = k.add(std::unique_ptr<Agent>(new NullMaster("hub")));
        Agent &sl = k.add(std::unique_ptr<Agent>(new NullSlave("s")));
        k.connect("Lm1", lpC(), m1, hub);
        k.connect("Lm2", lpC(), m2, hub);
        Link &out = k.connect("Lout", lpC(), hub, sl);
        CoherenceCheck cc;
        std::string what;
        try { cc.attach(out); } catch (ConfigError &e) { what = e.what(); }
        CHECK(what.find("link Lout: its master hub is a slave on link Lm1") != std::string::npos);
        CHECK(what.find("upstream of any Xbar") != std::string::npos);
        CHECK_EQ(cc.attachAll(k), 2u);                  // Lm1, Lm2; Lout skipped
        CHECK_EQ(cc.attached().size(), size_t(2));
        CHECK_EQ(cc.attachAll(k), 0u);                  // attaching twice is a no-op
    }
    {   // auto-attach at elaboration and of()
        Kernel k(KernelOptions{100, 1, DataMode::None});
        Agent &m = k.add(std::unique_ptr<Agent>(new NullMaster("m")));
        Agent &sl = k.add(std::unique_ptr<Agent>(new NullSlave("s")));
        k.connect("L", lpC(), m, sl);
        CHECK(CoherenceCheck::of(k) == nullptr);
        CoherenceCheck cc;
        k.addObserver(&cc);
        CHECK(CoherenceCheck::of(k) == &cc);
        CHECK(cc.attached().empty());
        k.step();
        CHECK_EQ(cc.attached().size(), size_t(1));
    }
    {
        bool thrown = false;
        try { CoherenceCheck bad(48); } catch (ConfigError &) { thrown = true; }
        CHECK(thrown);
    }
}

CHILD_CASE(leaf_rechecked_at_elaboration)
{
    // Attached while m was a leaf; m then became a slave on another link: refused at elaboration.
    Kernel k(KernelOptions{100, 1, DataMode::None});
    Agent &m = k.add(std::unique_ptr<Agent>(new NullMaster("m")));
    Agent &up = k.add(std::unique_ptr<Agent>(new NullMaster("up")));
    Agent &sl = k.add(std::unique_ptr<Agent>(new NullSlave("s")));
    Link &l = k.connect("L", lpC(), m, sl);
    CoherenceCheck cc;
    cc.attach(l);
    k.addObserver(&cc);
    k.connect("Lup", lpC(), up, m);
    k.step();
}

TEST(leaf_rule_at_elaboration)
{
    tltest::ChildResult r = tltest::runCase("leaf_rechecked_at_elaboration");
    CHECK_EQ(r.status, 2);
    CHECK(r.firstErrLine().find("TileLink: CoherenceCheck: link L: its master m is a slave on link Lup") == 0);
}

// ------------------------------------------------------------------ chaining
TEST(chained_with_monitor_and_valuecheck)
{
    // L1 carries a Monitor (report mode), a ValueCheck and the CoherenceCheck, attached in that order
    // (CoherenceCheck.h: after the others). Each sees its own violation on the same link:
    //   ValueCheck: PutFullData 0x11 to Y, then a Get answered 0x22 (V-READ);
    //   Monitor:    AcquireBlock BtoT from N (M-PERM-1);
    //   Coherence:  that Acquire granted toT while M2 is a Tip (C-GRANT, C-SWMR).
    const uint64_t Y = 0x2000;
    Sys s(true, DataMode::Full);
    MonitorOptions mo; mo.mode = MonitorOptions::Report; mo.print = false; mo.max_size = 6;
    std::unique_ptr<Monitor> mon(new Monitor(s.k, *s.L1, mo));
    ValueCheck vc;
    std::vector<ValueViolation> vv;
    vc.onViolation = [&](const ValueViolation &x) { vv.push_back(x); };
    vc.attach(*s.L1);
    CoherenceCheck cc;                                  // a second checker attached last, on L1 and L2
    std::vector<CoherenceViolation> cv;
    cc.onViolation = [&](const CoherenceViolation &x) { cv.push_back(x); };
    cc.attach(*s.L1); cc.attach(*s.L2);

    s.own(s.M2, s.S2, 100, Grow::NtoT, Cap::toT);
    MsgPtr put = makeMsg(s.k, Channel::A, unsigned(OpA::PutFullData), 3, 64, Y, 0, DataMode::Full);
    setData(*put, 0, 0x11);
    s.M1->at(100, Channel::A, put);
    s.S1->at(101, Channel::D, makeMsg(s.k, Channel::D, unsigned(OpD::AccessAck), 3, 64, 0, 0, DataMode::Full));
    s.M1->at(102, Channel::A, makeMsg(s.k, Channel::A, unsigned(OpA::Get), 3, 64, Y, 0, DataMode::Full));
    MsgPtr dat = makeMsg(s.k, Channel::D, unsigned(OpD::AccessAckData), 3, 64, Y, 0, DataMode::Full);
    setData(*dat, 0, 0x22);
    s.S1->at(103, Channel::D, dat);
    s.acquire(s.M1, 104, 1, X, Grow::BtoT);
    s.grant(s.S1, 105, 1, 0, Cap::toT, false);
    s.grantAck(s.M1, 106, 0);
    s.run(112);
    mon->finish(s.k.cycle());

    CHECK_EQ(mon->violations(), uint64_t(1));
    CHECK_EQ(mon->count("M-PERM-1"), uint64_t(1));
    CHECK_EQ(mon->perm(X), 'T');
    CHECK_EQ(vv.size(), size_t(1));
    if (!vv.empty()) { CHECK_EQ(vv[0].rule, std::string("V-READ")); CHECK_EQ(vv[0].got, uint64_t(0x22)); }
    CHECK_EQ(vc.counters().writes, uint64_t(1));
    CHECK_EQ(vc.counters().reads, uint64_t(1));
    // both coherence checkers (the Sys one, attached first, and this one) saw the conflict
    CHECK_EQ(s.fired(), std::string("C-GRANT@105 C-SWMR@105"));
    CHECK_EQ(cv.size(), size_t(2));
    CHECK_EQ(cc.permission("L1", X), 'T');
    mon.reset();
}

TL_TEST_MAIN_WITH_CHILDREN("TL-COH")
