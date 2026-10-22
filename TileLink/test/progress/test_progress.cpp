/*
 * File  :      test_progress.cpp
 * TL-PROGRESS (plan §6): the four §5.2 examples against the watchdog of plan §3.6, with the
 * scenarios and bounds of docs/TileLink_Negative_Tests.md §6. The periodic forwarder trips
 * rule 3ii on L1; the master withholding beats trips rule 2 on L1; the very slow arbiter and
 * the refresh-waiting RAM complete without a trip at their bounds and trip 3i at a bound that
 * is too small (the bound is a declaration, not a verdict). Plus report mode, the default
 * formula, the config keys and determinism. All links w=8 a=32 z=3 o=2 i=0 TL-UH,
 * reset_cycles 100 (t0 = 100), data mode none.
 */
#include "../graph/Subprocess.h"
#include "agents.h"
#include "tilelink/Watchdog.h"
#include <sstream>

using namespace octopus::tl;
using namespace tlp;

namespace
{
    const uint64_t T0 = 100;

    LinkParams params()
    {
        LinkParams p; p.w = 8; p.a = 32; p.z = 3; p.o = 2; p.i = 0; p.level = Level::UH;
        return p;
    }

    struct Scenario
    {
        Kernel k;
        std::unique_ptr<Watchdog> wd;
        std::ostringstream log, info;
        Scenario() : k(KernelOptions{T0, 1, DataMode::None}) {}
        template <typename T> T &add(const std::string &name) { return static_cast<T &>(k.add(std::unique_ptr<Agent>(new T(name)))); }
        Link &link(const std::string &n, Agent &m, Agent &s) { return k.connect(n, params(), m, s); }
        // The watchdog with an explicit bound; fatal mode exits 2 on a trip (the child cases),
        // report mode logs into `log` and counts.
        void watchdog(uint64_t bound, WatchdogMode mode)
        {
            WatchdogOptions o;
            o.bound = bound; o.mode = mode; o.log = &log; o.info = &info;
            wd.reset(new Watchdog(o));
            k.addObserver(wd.get());
        }
    };

    // §6.1: M -> L1 -> F (periodic forwarder) -> L2 -> S (ready on odd cycles).
    struct PeriodicForwarder : Scenario
    {
        ScriptedMaster &m; Forwarder &f; PeriodicReadySlave &s; Link &l1, &l2;
        explicit PeriodicForwarder(bool mutant)
            : m(add<ScriptedMaster>("M")), f(add<Forwarder>("F")), s(add<PeriodicReadySlave>("S")),
              l1(link("L1", m, f)), l2(link("L2", f, s))
        {
            f.periodic = mutant;
            m.script = {{T0, newRequest(m, l1, OpA::Get, 3, 0x100, 0)}};
        }
    };

    // §6.2: X (two master links) -> L1, L2 -> Y (slow arbiter) -> L3 -> Z (latency 2).
    struct Withholding : Scenario
    {
        WithholdingMaster &x; SlowArbiter &y; RamSlaveLite &z; Link &l1, &l2, &l3;
        explicit Withholding(bool conforming)
            : x(add<WithholdingMaster>("X")), y(add<SlowArbiter>("Y")), z(add<RamSlaveLite>("Z")),
              l1(link("L1", x, y)), l2(link("L2", x, y)), l3(link("L3", y, z))
        {
            x.conforming = conforming;
            z.latency = 2;
        }
    };

    // §6.3: M1, M2 (fuzz, 4 requests, 1 outstanding) -> L1, L2 -> Y -> L3 -> Z (latency 4).
    struct Arbiter : Scenario
    {
        FuzzLite &m1, &m2; SlowArbiter &y; RamSlaveLite &z; Link &l1, &l2, &l3;
        Arbiter()
            : m1(add<FuzzLite>("M1")), m2(add<FuzzLite>("M2")), y(add<SlowArbiter>("Y")), z(add<RamSlaveLite>("Z")),
              l1(link("L1", m1, y)), l2(link("L2", m2, y)), l3(link("L3", y, z))
        {
            m1.n = m2.n = 4; m1.max_outstanding = m2.max_outstanding = 1;
            m1.first_size = 5;   // the first transaction of M1 is the longest: M2's first wait is the long one
            z.latency = 4;
        }
    };

    // §6.4: M (fuzz, 8 requests, 2 outstanding) -> L1 -> Z (latency 4, refresh 20 of 50).
    struct Refresh : Scenario
    {
        FuzzLite &m; RamSlaveLite &z; Link &l1;
        Refresh()
            : m(add<FuzzLite>("M")), z(add<RamSlaveLite>("Z")), l1(link("L1", m, z))
        {
            m.n = 8; m.max_outstanding = 2;
            z.latency = 4; z.refresh_period = 50; z.refresh_cycles = 20;
        }
    };

    const char *RULE2_LINE = "TileLink: WATCHDOG rule 2 link=L1 channel=A agent=X cycle=132 bound=32 msg=PutFullData src=0 addr=0x100 size=5 "
                             "since=100 timer=32 1 of 4 beats accepted, no further beat presented since 100 (rule 2, §5: present all beats of a received message)";
    const char *RULE3II_LINE = "TileLink: WATCHDOG rule 3ii link=L1 channel=A agent=F cycle=116 bound=16 msg=Get src=0 addr=0x100 size=3 "
                               "since=100 timer=16 request received at 100 and not answered (rule 3ii, §5: answer a received request)";
}

// ---- child cases (fatal mode: exit 2 after the dump)

CHILD_CASE(periodic_forwarder)
{
    PeriodicForwarder s(true);
    s.watchdog(16, WatchdogMode::Fatal);
    s.k.run(T0 + 40);
}

CHILD_CASE(withholding)
{
    Withholding s(false);
    s.watchdog(32, WatchdogMode::Fatal);
    s.k.run(T0 + 60);
}

CHILD_CASE(arbiter_bound_8)
{
    Arbiter s;
    s.watchdog(8, WatchdogMode::Fatal);
    s.k.runUntilIdle(T0 + 2000);
}

CHILD_CASE(refresh_bound_16)
{
    Refresh s;
    s.watchdog(16, WatchdogMode::Fatal);
    s.k.runUntilIdle(T0 + 2000);
}

// ---- the suite

TEST(periodic_forwarder_rule_3ii)
{
    // §5.2 example 1: F raises valid on L2 only on even cycles, S is ready only on odd ones;
    // the forwarded beat is never presented (retracted every other cycle, §5.1), so no 3i
    // timer runs on L2, and the Get received on L1 at +0 is never answered: rule 3ii at +16.
    PeriodicForwarder s(true);
    s.watchdog(16, WatchdogMode::Report);
    s.k.run(T0 + 40);
    CHECK(s.info.str().find("progress_bound=16 (explicit progress_bound)") != std::string::npos);
    CHECK_EQ(s.wd->trips().size(), size_t(1));
    if (!s.wd->trips().empty())
    {
        const Trip &t = s.wd->trips()[0];
        CHECK_EQ(int(t.rule), int(ProgressRule::Rule3ii));
        CHECK(t.link == &s.l1);
        CHECK_EQ(int(t.channel), int(Channel::A));
        CHECK(t.agent == &s.f);
        CHECK_EQ(t.cycle, T0 + 16);
        CHECK_EQ(t.since, T0);
        CHECK_EQ(t.timer, uint64_t(16));
        CHECK_EQ(t.bound, uint64_t(16));
        CHECK_EQ("TileLink: " + t.line(), std::string(RULE3II_LINE));
    }
    CHECK_EQ(s.log.str(), std::string(RULE3II_LINE) + "\n");
    CHECK_EQ(s.wd->unanswered(), size_t(1));      // still unanswered at the end
    CHECK_EQ(s.wd->presented(), size_t(0));        // L2's beat is never presented
    CHECK_EQ(s.l2.chan(Channel::A).stats().beats, uint64_t(0));
    CHECK(s.l2.chan(Channel::A).stats().stall > 10);
    CHECK(s.m.answered.empty());
    // fatal mode: the run exits 2 after the dump, whose first line is the verdict
    tltest::ChildResult r = tltest::runCase("periodic_forwarder");
    CHECK_EQ(r.status, 2);
    CHECK_EQ(r.firstErrLine(), std::string(RULE3II_LINE));
    CHECK(r.err.find("bound: progress_bound=16 (explicit progress_bound)") != std::string::npos);
    CHECK(r.err.find("unanswered requests at cycle 116 (1):") != std::string::npos);
    CHECK(r.err.find("link=L1 channel=A agent=F msg=Get src=0 addr=0x100 size=3 received=100 age=16") != std::string::npos);
    CHECK(r.err.find("offered beats (1):") != std::string::npos);                  // F's beat on L2 at 116 (even)
    CHECK(r.err.find("link=L2 channel=A sender=F msg=Get src=0 addr=0x100 size=3 beat=0/1 ready=0 presented_since=116 age=0") != std::string::npos);
    CHECK(r.err.find("bursts in progress (0):") != std::string::npos);
}

TEST(periodic_forwarder_control)
{
    // F conforming (valid held HIGH): accepted on L2 at the first odd cycle (+1), answered
    // at +2, forwarded, accepted by M at +3. Fatal mode: any trip would exit 2.
    PeriodicForwarder s(false);
    s.watchdog(16, WatchdogMode::Fatal);
    uint64_t n = s.k.runUntilIdle(T0 + 40);
    CHECK(n < T0 + 40);
    CHECK_EQ(s.m.answered.size(), size_t(1));
    if (!s.m.answered.empty()) CHECK_EQ(s.m.answered[0].first, T0 + 3);
    CHECK_EQ(s.wd->unanswered(), size_t(0));
    CHECK_EQ(s.wd->presented(), size_t(0));
    CHECK_EQ(s.l2.chan(Channel::A).stats().beats, uint64_t(1));
}

TEST(withholding_rule_2)
{
    // §5.2 example 3: A's first beat accepted on L1 at +0 (received, in flight); X then
    // starts B on L2 at +1 and holds valid LOW on L1 until B is accepted. Y keeps L2 rejected
    // while busy with L1 (rule 3 exemption: its forwarding link has an unanswered message),
    // so L1 never resumes: rule 2 on L1 at +32. L1's 3ii (Y cannot answer) ties at +32 and
    // loses to rule 2; L2's 3i (presented at +1) follows at +33, with L3's rule 2 and 3ii.
    Withholding s(false);
    s.watchdog(32, WatchdogMode::Report);
    s.k.run(T0 + 40);
    const auto &tr = s.wd->trips();
    CHECK_EQ(tr.size(), size_t(5));
    if (tr.size() == 5)
    {
        CHECK_EQ("TileLink: " + tr[0].line(), std::string(RULE2_LINE));
        CHECK(tr[0].link == &s.l1); CHECK(tr[0].agent == &s.x); CHECK_EQ(tr[0].beat, 1u);
        CHECK_EQ(int(tr[1].rule), int(ProgressRule::Rule3ii)); CHECK(tr[1].link == &s.l1); CHECK_EQ(tr[1].cycle, T0 + 32); CHECK(tr[1].agent == &s.y);
        CHECK_EQ(int(tr[2].rule), int(ProgressRule::Rule2));   CHECK(tr[2].link == &s.l3); CHECK_EQ(tr[2].cycle, T0 + 33); CHECK(tr[2].agent == &s.y);
        CHECK_EQ(int(tr[3].rule), int(ProgressRule::Rule3i));  CHECK(tr[3].link == &s.l2); CHECK_EQ(tr[3].cycle, T0 + 33); CHECK(tr[3].agent == &s.y);
        CHECK_EQ(tr[3].since, T0 + 1); CHECK_EQ(tr[3].beat, 0u);
        CHECK_EQ(int(tr[4].rule), int(ProgressRule::Rule3ii)); CHECK(tr[4].link == &s.l3); CHECK_EQ(tr[4].cycle, T0 + 33); CHECK(tr[4].agent == &s.z);
    }
    CHECK(s.l1.chan(Channel::A).inProgress());
    CHECK_EQ(s.l1.chan(Channel::A).stats().beats, uint64_t(1));
    CHECK_EQ(s.l2.chan(Channel::A).stats().beats, uint64_t(0));
    // fatal mode: the verdict is the earliest trip, ties broken 2, 3i, 3ii; the 3ii of L1 is
    // "also past the bound"; L2's presented beat is listed with its age (31, one short)
    tltest::ChildResult r = tltest::runCase("withholding");
    CHECK_EQ(r.status, 2);
    CHECK_EQ(r.firstErrLine(), std::string(RULE2_LINE));
    CHECK(r.err.find("also past the bound: WATCHDOG rule 3ii link=L1 channel=A agent=Y cycle=132") != std::string::npos);
    CHECK(r.err.find("link=L2 channel=A sender=X msg=PutFullData src=0 addr=0x200 size=5 beat=0/4 ready=0 presented_since=101 age=31") != std::string::npos);
    CHECK(r.err.find("bursts in progress (2):") != std::string::npos);
    CHECK(r.err.find("link=L1 channel=A sender=X msg=PutFullData src=0 addr=0x100 size=5 accepted=1/4 last_progress=100 age=32") != std::string::npos);
    CHECK(r.err.find("link=L3 channel=A sender=Y msg=PutFullData src=0 addr=0x100 size=5 accepted=1/4 last_progress=101 age=31") != std::string::npos);
    CHECK(r.err.find("unanswered requests at cycle 132 (2):") != std::string::npos);
}

TEST(withholding_control)
{
    // X conforming: A completes on L1 first, then B on L2. Y rejects L2 while serving A (burst
    // 4 + latency 2 + response 1 + turnaround), within the bound. Both Puts acknowledged.
    Withholding s(true);
    s.watchdog(32, WatchdogMode::Fatal);
    uint64_t n = s.k.runUntilIdle(T0 + 200);
    CHECK(n < T0 + 200);
    CHECK_EQ(s.x.answered, 2u);
    CHECK_EQ(s.l1.chan(Channel::A).stats().beats, uint64_t(4));
    CHECK_EQ(s.l2.chan(Channel::A).stats().beats, uint64_t(4));
    CHECK_EQ(s.l3.chan(Channel::A).stats().beats, uint64_t(8));
    CHECK(s.l2.chan(Channel::A).stats().stall >= 7);
    CHECK(s.l2.chan(Channel::A).stats().stall < 32);
    CHECK_EQ(s.wd->unanswered(), size_t(0));
}

TEST(slow_arbiter_conforming)
{
    // §5.2 example 4 at bound 32: no trip, all 8 requests answered, Y serves L1 and L2 in
    // turn. Fatal mode, so a trip would end the process.
    Arbiter s;
    s.watchdog(32, WatchdogMode::Fatal);
    uint64_t n = s.k.runUntilIdle(T0 + 2000);
    CHECK(n < T0 + 2000);
    CHECK_EQ(s.m1.answered, 4u);
    CHECK_EQ(s.m2.answered, 4u);
    CHECK_EQ(s.wd->unanswered(), size_t(0));
    CHECK_EQ(s.wd->presented(), size_t(0));
    CHECK_EQ(s.m1.accept_cycles.size(), size_t(4));
    CHECK_EQ(s.m2.accept_cycles.size(), size_t(4));
    if (s.m1.accept_cycles.size() == 4 && s.m2.accept_cycles.size() == 4)
    {
        CHECK_EQ(s.m1.accept_cycles[0], T0);                     // ties to L1
        CHECK(s.m2.accept_cycles[0] > T0);                       // M2 waited for M1's whole transaction
        CHECK(s.m2.accept_cycles[0] - T0 >= 8 && s.m2.accept_cycles[0] - T0 <= 14);
        CHECK(s.m1.accept_cycles[1] > s.m2.accept_cycles[0]);    // then L1 again: alternation
    }
    // the bound is a declaration: at 8 the first long wait trips 3i on L2 although every
    // agent conforms, and the dump prints the bound so the reader sees "bound too small"
    Arbiter s8;
    s8.watchdog(8, WatchdogMode::Report);
    s8.k.runUntilIdle(T0 + 2000);
    CHECK(!s8.wd->trips().empty());
    if (!s8.wd->trips().empty())
    {
        const Trip &t = s8.wd->trips()[0];
        CHECK_EQ(int(t.rule), int(ProgressRule::Rule3i));
        CHECK(t.link == &s8.l2);
        CHECK(t.agent == &s8.y);
        CHECK_EQ(t.cycle, T0 + 8);
        CHECK_EQ(t.since, T0);
        CHECK_EQ(t.bound, uint64_t(8));
    }
    CHECK_EQ(s8.m1.answered, 4u);   // report mode continues; everything still completes
    CHECK_EQ(s8.m2.answered, 4u);
    tltest::ChildResult r = tltest::runCase("arbiter_bound_8");
    CHECK_EQ(r.status, 2);
    CHECK(r.firstErrLine().find("TileLink: WATCHDOG rule 3i link=L2 channel=A agent=Y cycle=108 bound=8 msg=") == 0);
    CHECK(r.err.find("bound: progress_bound=8 (explicit progress_bound); a trip means longer than the declared bound, not non-conformance") != std::string::npos);
}

TEST(refresh_conforming)
{
    // §5.2 example 2 at bound 40: Z lowers a_ready for 20 of every 50 cycles; the longest
    // legal wait is refresh 20 + latency 4 + a burst ahead; no trip, all 8 answered, and the
    // A stall counter shows at least one full refresh window rejected.
    Refresh s;
    s.watchdog(40, WatchdogMode::Fatal);
    uint64_t n = s.k.runUntilIdle(T0 + 2000);
    CHECK(n < T0 + 2000);
    CHECK_EQ(s.m.answered, 8u);
    CHECK_EQ(s.wd->unanswered(), size_t(0));
    CHECK(s.l1.chan(Channel::A).stats().stall >= 20);
    CHECK(!s.m.accept_cycles.empty());
    if (!s.m.accept_cycles.empty()) CHECK_EQ(s.m.accept_cycles[0], T0 + 20);   // the first beat waits out the refresh at 100..119
    // bound 16: 3i on L1 in the first refresh window, agent Z, with the bound printed
    Refresh s16;
    s16.watchdog(16, WatchdogMode::Report);
    s16.k.runUntilIdle(T0 + 2000);
    CHECK(!s16.wd->trips().empty());
    if (!s16.wd->trips().empty())
    {
        const Trip &t = s16.wd->trips()[0];
        CHECK_EQ(int(t.rule), int(ProgressRule::Rule3i));
        CHECK(t.link == &s16.l1);
        CHECK(t.agent == &s16.z);
        CHECK_EQ(t.cycle, T0 + 16);
        CHECK_EQ(t.since, T0);
    }
    CHECK_EQ(s16.m.answered, 8u);
    tltest::ChildResult r = tltest::runCase("refresh_bound_16");
    CHECK_EQ(r.status, 2);
    CHECK(r.firstErrLine().find("TileLink: WATCHDOG rule 3i link=L1 channel=A agent=Z cycle=116 bound=16 msg=") == 0);
}

TEST(retract_resets_presentation)
{
    // §5.1: a beat retracted was never presented. The periodic forwarder's L2 beat is offered
    // on even cycles and withdrawn on odd ones: the 3i timer restarts at every offer and
    // never reaches even a bound of 2; the unanswered request still trips 3ii.
    PeriodicForwarder s(true);
    s.watchdog(2, WatchdogMode::Report);
    s.k.run(T0 + 10);
    size_t rule3i = 0, rule3ii = 0;
    for (auto &t : s.wd->trips()) (t.rule == ProgressRule::Rule3i ? rule3i : rule3ii)++;
    CHECK_EQ(rule3i, size_t(0));
    CHECK_EQ(rule3ii, size_t(1));
}

TEST(report_mode_counts_and_callback)
{
    // Report mode counts every timer once and hands the trip and the dump to the callback.
    Withholding s(false);
    s.watchdog(32, WatchdogMode::Report);
    int calls = 0;
    std::string first_dump;
    s.wd->onTrip = [&](const Trip &t, const std::string &dump) { if (calls++ == 0) first_dump = dump; CHECK(t.bound == 32); };
    s.k.run(T0 + 100);
    CHECK_EQ(calls, 5);
    CHECK_EQ(s.wd->trips().size(), size_t(5));
    CHECK(first_dump.find(RULE2_LINE + 10) == 0);   // the dump's first line is the verdict, without the "TileLink: " prefix
    CHECK(first_dump.find("also past the bound: WATCHDOG rule 3ii link=L1") != std::string::npos);
    // one log line per trip, each prefixed like fatal's output
    std::string log = s.log.str();
    size_t lines = 0;
    for (char c : log) if (c == '\n') lines++;
    CHECK_EQ(lines, size_t(5));
    CHECK(log.find("TileLink: WATCHDOG rule 2 link=L1") == 0);
}

TEST(default_bound_formula)
{
    // Plan §3.6: (slowest latency + deepest queue + largest burst) x masters, from hints().
    // Arbiter: Z latency 4, Y queue 1, burst 2^7/8 = 16 beats, 2 masters: (4 + 1 + 16) x 2 = 42.
    Arbiter s;
    std::string formula;
    CHECK_EQ(Watchdog::defaultBound(s.k, &formula), uint64_t(42));
    CHECK_EQ(formula, std::string("default formula (plan §3.6): (latency 4 + queue 1 + burst 16 beats) x 2 masters = 42"));
    WatchdogOptions o;
    o.info = &s.info;
    Watchdog wd(o);
    s.k.addObserver(&wd);
    s.k.elaborate();
    CHECK_EQ(wd.bound(), uint64_t(42));
    CHECK(s.info.str().find("TileLink watchdog: progress_bound=42 (default formula") == 0);
    // Refresh: Z latency 4 and queue = refresh 20, burst 16, 1 master: 40, the scenario's bound
    Refresh r;
    CHECK_EQ(Watchdog::defaultBound(r.k), uint64_t(40));
    // the default hints: an originator is a master on a link and a slave on none
    PeriodicForwarder p(false);
    CHECK(p.m.hints().is_master);
    CHECK(!p.f.hints().is_master);
    CHECK(!p.s.hints().is_master);
    CHECK_EQ(Watchdog::defaultBound(p.k), uint64_t(18));   // (1 + 1 + 16) x 1
}

TEST(config_keys)
{
    // Kernel::build: watchdog(i) default 1 with the default formula; progress_bound(i)
    // explicit; 0 turns it off; watchdog(i)=0 too; watchdog_mode(s) fatal|report.
    auto base = []()
    {
        Config c;
        c.set("reset_cycles(i),0");
        c.set("agent[0].type(s),NullMaster"); c.set("agent[0].name(s),m");
        c.set("agent[1].type(s),NullSlave");  c.set("agent[1].name(s),s");
        c.set("link[0].master(s),m"); c.set("link[0].slave(s),s"); c.set("link[0].level(s),UH");
        return c;
    };
    {
        Kernel k;
        k.build(base(), AgentRegistry::withBuiltins());
        Watchdog *w = dynamic_cast<Watchdog *>(k.observer("Watchdog"));
        CHECK(w != nullptr);
        k.elaborate();
        if (w) { CHECK_EQ(w->bound(), uint64_t(18)); CHECK(w->formula().find("default formula") == 0); }
        k.run(50);   // nothing is offered: no timer runs, no trip
    }
    {
        Config c = base(); c.set("progress_bound(i),7"); c.set("watchdog_mode(s),report");
        Kernel k;
        k.build(c, AgentRegistry::withBuiltins());
        Watchdog *w = dynamic_cast<Watchdog *>(k.observer("Watchdog"));
        CHECK(w != nullptr);
        k.elaborate();
        if (w) { CHECK_EQ(w->bound(), uint64_t(7)); CHECK_EQ(w->formula(), std::string("explicit progress_bound")); }
    }
    {
        Config c = base(); c.set("progress_bound(i),0");
        Kernel k;
        k.build(c, AgentRegistry::withBuiltins());
        CHECK(k.observer("Watchdog") == nullptr);
    }
    {
        Config c = base(); c.set("watchdog(i),0");
        Kernel k;
        k.build(c, AgentRegistry::withBuiltins());
        CHECK(k.observer("Watchdog") == nullptr);
        CHECK(k.observer("GraphCheck") != nullptr);
    }
}

TEST(determinism)
{
    // The same scenario twice: identical trips, log and transcript (plan §3.2).
    auto run = [](std::string &log, std::string &transcript) -> size_t
    {
        Arbiter s;
        s.watchdog(8, WatchdogMode::Report);
        std::ostringstream tr;
        s.k.enableTranscript(tr);
        s.k.runUntilIdle(T0 + 2000);
        log = s.log.str();
        transcript = tr.str();
        return s.wd->trips().size();
    };
    std::string l1, t1, l2, t2;
    size_t n1 = run(l1, t1), n2 = run(l2, t2);
    CHECK_EQ(n1, n2);
    CHECK(n1 >= 1);
    CHECK_EQ(l1, l2);
    CHECK_EQ(t1, t2);
    CHECK(t1.find("PutFullData") != std::string::npos);
}

TL_TEST_MAIN_WITH_CHILDREN("test_progress")
