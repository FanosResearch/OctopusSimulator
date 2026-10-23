/*
 * File  :      test_neg.cpp
 * TL-NEG for TL-UL and TL-UH (plan §6; docs/TileLink_Negative_Tests.md §2 and §3, "the doc"): one
 * mutant per monitor rule. Harness (doc §1.1): link L0 w=8 a=32 z=3 o=2 i=1, slave max_size 6,
 * address map [0x0, 0x10000), reset_cycles 100, t0 = 100; ScriptedMaster M and ScriptedSlave S. No
 * watchdog is attached (the kernel is built directly), which is the doc's `progress_bound=0`.
 *
 * Report mode: the expected rule fires, its first report is at the documented cycle, and every
 * other rule that fires is in the mutant's `also` list. `also` holds the doc's "would also hold"
 * entries plus the ones found here that the doc does not list, each marked DISCREPANCY below.
 * Fatal mode: the test binary re-runs itself with `--fatal <mutant>`; the child exits 2 and its
 * first stderr line names `fatal_first` (the first rule in the catalogue's evaluation order, §0.2).
 *
 * Harness choice for the offer-time mutants (static field rules, checked at offer whether or not
 * the beat is accepted, catalogue §0.2): S rejects the bad beat (a_ready LOW in that cycle) and M
 * offers it once, so that nothing downstream of an illegal request (an unanswered request, a
 * response to an illegal opcode) adds reports of its own. Where the doc's scenario needs the
 * request accepted (response, ID and burst rules), S accepts and answers by the doc's harness rule.
 */
#include "TestMain.h"
#include "ScriptedSlave.h"
#include "tilelink/Monitor.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/wait.h>

using namespace octopus::tl;
using namespace octopus::tl::fixture;

namespace
{
    const char *g_argv0 = nullptr;

    struct Neg
    {
        Kernel k;
        ScriptedMaster *M;
        ScriptedSlave *S;
        Link *l;
        std::unique_ptr<Monitor> mon;
        uint64_t t0 = 100;
        Neg(Level level, unsigned z, int max_size, AddrRegion region, MonitorOptions::Mode mode)
            : k(KernelOptions{100, 1, DataMode::Full})
        {
            M = static_cast<ScriptedMaster *>(&k.add(std::unique_ptr<Agent>(new ScriptedMaster("M"))));
            S = static_cast<ScriptedSlave *>(&k.add(std::unique_ptr<Agent>(new ScriptedSlave("S"))));
            LinkParams p; p.w = 8; p.a = 32; p.z = z; p.o = 2; p.i = 1; p.level = level;
            l = &k.connect("L0", p, *M, *S);
            MonitorOptions mo;
            mo.mode = mode;
            mo.print = mode == MonitorOptions::Report;   // fatal: tl::fatal prints the line itself
            mo.max_size = max_size;
            mo.regions = {region};
            mon.reset(new Monitor(k, *l, mo));
            Monitor *mp = mon.get();
            M->missingChannel = [mp](Channel ch, const Msg &m, unsigned beat, uint64_t t) { mp->offerOnMissingChannel(ch, m, beat, t); };
        }
        ~Neg() { mon.reset(); }
        MsgPtr A(OpA op, unsigned size, uint64_t addr, uint64_t src, unsigned param = 0)
        { return makeMsg(k, Channel::A, unsigned(op), size, 8, addr, src, DataMode::Full, param); }
        MsgPtr D(OpD op, unsigned size, uint64_t src) { return makeMsg(k, Channel::D, unsigned(op), size, 8, 0, src, DataMode::Full); }
        void at(int rel, const MsgPtr &m, unsigned beat = 0, std::function<void(Msg &)> before = nullptr)
        { M->at(t0 + uint64_t(rel), m->channel, m, beat, before); }
        // An offer-time mutant: offered once at +rel and rejected (harness choice above).
        void rejected(int rel, const MsgPtr &m) { at(rel, m); S->notReady(Channel::A, t0 + uint64_t(rel)); }
        void respond() { S->autoRespond = true; S->delay = 1; }
    };

    struct Mutant
    {
        const char *name;          // test id
        const char *rule;          // the rule the doc says fires
        Level level;
        int fire;                  // cycle relative to t0 (absolute if fire_abs)
        bool fire_abs;
        int run;                   // cycles after t0 to run
        std::vector<std::string> also;
        const char *fatal_first;   // first report in fatal mode
        std::function<void(Neg &)> build;
        unsigned z = 3;
        int max_size = 6;
        AddrRegion region{0x0, 0x10000};
    };

    std::vector<Mutant> mutants()
    {
        std::vector<Mutant> v;
        auto add = [&](Mutant m) { v.push_back(m); };
        // ------------------------------------------------------------ doc §2, TL-UL
        add({"M-RESET-1", "M-RESET-1", Level::UL, 50, true, 20, {}, "M-RESET-1",
             [](Neg &n) { n.M->at(50, Channel::A, n.A(OpA::Get, 3, 0x100, 0)); }});
        add({"M-LEVEL-1", "M-LEVEL-1", Level::UL, 0, false, 20, {}, "M-LEVEL-1",
             [](Neg &n) { MsgPtr a = n.A(OpA::ArithmeticData, 3, 0x100, 0, unsigned(Arith::ADD)); setData(*a, 0, 1); n.rejected(0, a); }});
        add({"M-LEVEL-2", "M-LEVEL-2", Level::UL, 1, false, 20, {"M-RESP-1"}, "M-LEVEL-2",   // doc: M-RESP-1 would also hold
             [](Neg &n) { n.at(0, n.A(OpA::Get, 3, 0x100, 0)); n.respond();
                          n.S->respond = [&n](const Msg &r) { return n.D(OpD::HintAck, r.size, r.source); }; }});
        add({"M-LEVEL-4", "M-LEVEL-4", Level::UL, 0, false, 20, {}, "M-LEVEL-4",
             [](Neg &n) { n.at(0, makeMsg(n.k, Channel::C, unsigned(OpC::Release), 6, 8, 0x100, 0, DataMode::Full, unsigned(Prune::BtoN))); }});
        add({"M-LEVEL-5", "M-LEVEL-5", Level::UL, 0, false, 20, {}, "M-LEVEL-5",
             [](Neg &n) { n.rejected(0, n.A(OpA::PutFullData, 4, 0x100, 0)); }});
        add({"M-PARAM-1", "M-PARAM-1", Level::UL, 0, false, 20, {}, "M-PARAM-1",
             [](Neg &n) { n.rejected(0, n.A(OpA::Get, 3, 0x100, 0, 1)); }});
        add({"M-PARAM-5", "M-PARAM-5", Level::UL, 1, false, 20, {}, "M-PARAM-5",
             [](Neg &n) { n.at(0, n.A(OpA::PutFullData, 3, 0x100, 0)); n.respond(); n.S->tweak = [](Msg &r, const Msg &) { r.param = 1; }; }});
        add({"M-ALIGN-1", "M-ALIGN-1", Level::UL, 0, false, 20, {}, "M-ALIGN-1",
             [](Neg &n) { n.rejected(0, n.A(OpA::Get, 3, 0x104, 0)); }});
        // DISCREPANCY (doc §2 M-ALIGN-2): on the TL-UL link the doc specifies, 2^8 > w fires M-LEVEL-5 first
        // (evaluation order §0.2), and size 8 > max_size 6 always fires M-ALIGN-3 as well; the doc lists neither.
        add({"M-ALIGN-2", "M-ALIGN-2", Level::UL, 0, false, 20, {"M-LEVEL-5", "M-ALIGN-3"}, "M-LEVEL-5",
             [](Neg &n) { n.rejected(0, n.A(OpA::Get, 8, 0x100, 0)); }});
        add({"M-ALIGN-3", "M-ALIGN-3", Level::UH, 0, false, 20, {}, "M-ALIGN-3",
             [](Neg &n) { n.rejected(0, n.A(OpA::Get, 7, 0x0, 0)); }});
        // DISCREPANCY (doc §2 M-ALIGN-4): the address 0x1_0000_0000 is also outside the harness's address map
        // [0x0, 0x10000), so M-ALIGN-5 holds too (it is later in the order).
        add({"M-ALIGN-4", "M-ALIGN-4", Level::UL, 0, false, 20, {"M-ALIGN-5"}, "M-ALIGN-4",
             [](Neg &n) { n.rejected(0, n.A(OpA::Get, 3, 0x100000000ull, 0)); }});
        { Mutant m{"M-ALIGN-5", "M-ALIGN-5", Level::UH, 0, false, 20, {}, "M-ALIGN-5",
                   [](Neg &n) { n.rejected(0, n.A(OpA::Get, 12, 0x1000, 0)); }};
          m.z = 4; m.max_size = 12; m.region = AddrRegion{0x1000, 0x800}; add(m); }
        add({"M-ALIGN-6", "M-ALIGN-6", Level::UL, 0, false, 20, {}, "M-ALIGN-6",
             [](Neg &n) { n.rejected(0, n.A(OpA::Get, 3, 0x100, 4)); }});
        add({"M-MASK-1", "M-MASK-1", Level::UL, 0, false, 20, {}, "M-MASK-1",
             [](Neg &n) { MsgPtr a = n.A(OpA::PutFullData, 2, 0x104, 0); setMask(*a, 0, 0xff); n.rejected(0, a); }});
        add({"M-MASK-2", "M-MASK-2", Level::UL, 0, false, 20, {}, "M-MASK-2",
             [](Neg &n) { MsgPtr a = n.A(OpA::PutFullData, 3, 0x100, 0); setMask(*a, 0, 0x0f); n.rejected(0, a); }});
        add({"M-MASK-2b", "M-MASK-2", Level::UH, 0, false, 20, {}, "M-MASK-2",
             [](Neg &n) { MsgPtr a = n.A(OpA::Get, 5, 0x100, 0); setMask(*a, 0, 0x0f); n.rejected(0, a); }});
        add({"M-MASK-3", "M-MASK-3", Level::UL, 0, false, 20, {}, "M-MASK-3",
             [](Neg &n) { MsgPtr a = n.A(OpA::PutPartialData, 2, 0x104, 0); setMask(*a, 0, 0x11); n.rejected(0, a); }});
        add({"M-RSV-1", "M-RSV-1", Level::UL, 0, false, 20, {}, "M-RSV-1",
             [](Neg &n) { MsgPtr a = n.A(OpA::Get, 3, 0x100, 0); a->corrupt[0] = 1; n.rejected(0, a); }});
        add({"M-ERR-1", "M-ERR-1", Level::UL, 0, false, 20, {}, "M-ERR-1",
             [](Neg &n) { MsgPtr a = n.A(OpA::Get, 3, 0x100, 0); a->denied = true; n.rejected(0, a); }});
        add({"M-ERR-2", "M-ERR-2", Level::UH, 3, false, 20, {}, "M-ERR-2",
             [](Neg &n) { n.at(0, n.A(OpA::Get, 5, 0x100, 0)); n.respond();
                          n.S->tweak = [](Msg &r, const Msg &) { r.denied = true; r.corrupt = {1, 1, 0, 1}; }; }});
        add({"M-BURST-5", "M-BURST-5", Level::UL, 50, false, 50, {}, "M-BURST-5",
             [](Neg &n) { n.at(0, n.A(OpA::Get, 3, 0x100, 0)); n.S->at(n.t0 + 1, Channel::D, n.D(OpD::AccessAckData, 3, 0));
                          n.M->notReady(Channel::D, n.t0 + 1); }});
        add({"M-RESP-1", "M-RESP-1", Level::UL, 1, false, 20, {}, "M-RESP-1",
             [](Neg &n) { n.at(0, n.A(OpA::Get, 3, 0x100, 0)); n.respond();
                          n.S->respond = [&n](const Msg &r) { return n.D(OpD::AccessAck, r.size, r.source); }; }});
        add({"M-RESP-2", "M-RESP-2", Level::UL, 0, false, 20, {}, "M-RESP-2",
             [](Neg &n) { n.S->at(n.t0, Channel::D, n.D(OpD::AccessAck, 3, 1)); }});
        // DISCREPANCY (doc §2 M-RESP-2 note): in the "response one cycle before its request" variant the Get
        // of +1 is never answered afterwards, so M-RESP-7 also holds at the end of the run.
        add({"M-RESP-2-early", "M-RESP-2", Level::UL, 0, false, 20, {"M-RESP-7"}, "M-RESP-2",
             [](Neg &n) { n.S->at(n.t0, Channel::D, n.D(OpD::AccessAck, 3, 1)); n.at(1, n.A(OpA::Get, 3, 0x100, 1)); }});
        add({"M-RESP-6", "M-RESP-6", Level::UL, 1, false, 20, {}, "M-RESP-6",
             [](Neg &n) { n.at(0, n.A(OpA::Get, 3, 0x100, 0)); n.respond();
                          n.S->respond = [&n](const Msg &r) { return n.D(OpD::AccessAckData, 2, r.source); }; }});
        add({"M-RESP-7", "M-RESP-7", Level::UL, 50, false, 50, {}, "M-RESP-7",
             [](Neg &n) { n.at(0, n.A(OpA::Get, 3, 0x100, 0)); }});
        add({"M-ID-1", "M-ID-1", Level::UL, 1, false, 20, {}, "M-ID-1",
             [](Neg &n) { n.at(0, n.A(OpA::Get, 3, 0x100, 2)); n.at(1, n.A(OpA::Get, 3, 0x108, 2)); n.respond(); }});
        // ------------------------------------------------------------ doc §3, TL-UH
        add({"M-PARAM-2", "M-PARAM-2", Level::UH, 0, false, 20, {}, "M-PARAM-2",
             [](Neg &n) { n.rejected(0, n.A(OpA::ArithmeticData, 3, 0x100, 0, 5)); }});
        add({"M-PARAM-3", "M-PARAM-3", Level::UH, 0, false, 20, {}, "M-PARAM-3",
             [](Neg &n) { n.rejected(0, n.A(OpA::LogicalData, 3, 0x100, 0, 4)); }});
        add({"M-PARAM-4", "M-PARAM-4", Level::UH, 0, false, 20, {}, "M-PARAM-4",
             [](Neg &n) { n.rejected(0, n.A(OpA::Intent, 3, 0x100, 0, 2)); }});
        add({"M-BURST-1", "M-BURST-1", Level::UH, 2, false, 20, {}, "M-BURST-1",
             [](Neg &n) { MsgPtr p = n.A(OpA::PutFullData, 5, 0x100, 0);
                          n.at(0, p, 0); n.at(1, p, 1); n.at(2, p, 1); n.at(3, p, 2); n.at(4, p, 3); n.respond(); }});
        // DISCREPANCY (doc §3 M-BURST-2): 0x108 is not aligned to size 5, so M-ALIGN-1 holds on the same beat
        // and is first in the evaluation order (§0.2); fatal mode therefore reports M-ALIGN-1.
        add({"M-BURST-2", "M-BURST-2", Level::UH, 1, false, 20, {"M-ALIGN-1"}, "M-ALIGN-1",
             [](Neg &n) { MsgPtr p = n.A(OpA::PutFullData, 5, 0x100, 0);
                          n.at(0, p, 0); n.at(1, p, 1, [](Msg &m) { m.address = 0x108; });
                          n.at(2, p, 2, [](Msg &m) { m.address = 0x100; }); n.at(3, p, 3); n.respond(); }});
        add({"M-BURST-3", "M-BURST-3", Level::UH, 2, false, 20, {}, "M-BURST-3",
             [](Neg &n) { MsgPtr p = n.A(OpA::PutFullData, 5, 0x100, 0);
                          n.at(0, p, 0); n.at(1, p, 1); n.at(2, n.A(OpA::Get, 3, 0x200, 1)); n.at(3, p, 2); n.at(4, p, 3); n.respond(); }});
        add({"M-BURST-4", "M-BURST-4", Level::UH, 50, false, 50, {}, "M-BURST-4",
             [](Neg &n) { n.at(0, n.A(OpA::PutFullData, 5, 0x100, 0), 0); n.respond(); }});
        return v;
    }

    const Mutant *find(const std::string &name)
    {
        static std::vector<Mutant> all = mutants();
        for (auto &m : all) if (name == m.name) return &m;
        return nullptr;
    }

    // Runs a mutant; in report mode returns its monitor's log.
    std::vector<Violation> run(const Mutant &mu, MonitorOptions::Mode mode)
    {
        Neg n(mu.level, mu.z, mu.max_size, mu.region, mode);
        mu.build(n);
        n.k.run(n.t0 + uint64_t(mu.run));
        n.mon->finish(n.k.cycle());
        return n.mon->log();
    }

    int g_fired = 0, g_fatal_ok = 0;

    void checkMutant(const Mutant &mu)
    {
        std::vector<Violation> log = run(mu, MonitorOptions::Report);
        uint64_t expect_cycle = mu.fire_abs ? uint64_t(mu.fire) : 100 + uint64_t(mu.fire);
        const Violation *first = nullptr;
        std::set<std::string> fired;
        for (auto &v : log) { fired.insert(v.rule); if (!first && v.rule == mu.rule) first = &v; }
        bool ok = first && first->cycle == expect_cycle;
        for (auto &r : fired)
            if (r != mu.rule && std::find(mu.also.begin(), mu.also.end(), r) == mu.also.end()) ok = false;
        CHECK(first != nullptr);
        if (first) CHECK_EQ(first->cycle, expect_cycle);
        if (!ok)
        {
            std::printf("  %s: expected %s at cycle %llu, fired:", mu.name, mu.rule, (unsigned long long)expect_cycle);
            for (auto &v : log) std::printf(" %s@%llu", v.rule.c_str(), (unsigned long long)v.cycle);
            std::printf("\n");
            CHECK(false);
        }
        else g_fired++;
        // fatal mode: the child exits 2 with the first rule in the evaluation order
        std::string err = std::string("neg_") + mu.name + ".stderr";
        std::string cmd = std::string("\"") + g_argv0 + "\" --fatal " + mu.name + " >/dev/null 2>" + err;
        int st = std::system(cmd.c_str());
        int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        std::ifstream in(err);
        std::string line;
        std::getline(in, line);
        std::string want = std::string("TileLink: ") + mu.fatal_first + " ";
        CHECK_EQ(code, 2);
        CHECK(line.compare(0, want.size(), want) == 0);
        if (code == 2 && line.compare(0, want.size(), want) == 0) g_fatal_ok++;
        else std::printf("  %s fatal: exit %d, first line: %s\n", mu.name, code, line.c_str());
    }
}

TEST(tl_ul_uh_mutants)
{
    for (auto &mu : mutants()) checkMutant(mu);
    std::printf("  %d/%zu mutants fired as expected in report mode, %d/%zu exited 2 in fatal mode with the expected id\n",
                g_fired, mutants().size(), g_fatal_ok, mutants().size());
}

namespace
{
    int runTlsim(const std::string &cfg_text, const std::string &tag, std::string *out_text)
    {
        std::string cfg = "neg_" + tag + ".csv";
        { std::ofstream f(cfg); f << cfg_text; }
        std::string out = "neg_" + tag + ".out";
        std::string cmd = std::string("\"") + TLSIM_PATH + "\" -c " + cfg + " -o neg_" + tag + "_dir > " + out + " 2>&1";
        int st = std::system(cmd.c_str());
        std::ifstream in(out);
        std::stringstream ss; ss << in.rdbuf();
        if (out_text) *out_text = ss.str();
        return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    }
    const char *SYS =
        "agent[0].type(s),FuzzMaster\nagent[0].name(s),m\nagent[0].ops(i),50\n"
        "agent[1].type(s),RamSlave\nagent[1].name(s),s\nagent[1].latency(i),40\n"
        "link[0].master(s),m\nlink[0].slave(s),s\nlink[0].level(s),UL\nlink[0].o(i),2\nwatchdog(i),0\n";
}

TEST(m_reset_2_tlsim_refuses_short_reset)
{
    // doc §2 M-RESET-2: tlsim with reset_cycles=10 standalone: exit 2 at elaboration, before cycle 0.
    std::string out;
    int code = runTlsim(std::string(SYS) + "reset_cycles(i),10\ncycles(i),300\n", "reset2", &out);
    CHECK_EQ(code, 2);
    CHECK(out.find("TileLink: M-RESET-2") != std::string::npos);
    CHECK(out.find("tlsim:") == std::string::npos);   // never ran
    if (code == 2) g_fired++;
}

TEST(tlsim_exit_status)
{
    // Exit 3: report mode counted violations (requests still unanswered when a run until drained stops at
    // max_cycles(i): M-RESP-7; the violations are the primary verdict over the incomplete run, exit 4);
    // exit 2: the same run in fatal mode; exit 0: a clean run. (A fixed cycles(i) window is an observation and
    // applies no end-of-run rule unless it drained: docs/TileLink_Review_Action_Plan.md problem 6, TL-RUN.)
    std::string out;
    std::string base = std::string(SYS) + "reset_cycles(i),100\n";
    CHECK_EQ(runTlsim(base + "cycles(i),0\nmax_cycles(i),130\nmonitor_mode(s),report\n", "exit3", &out), 3);
    CHECK(out.find("M-RESP-7") != std::string::npos);
    CHECK(out.find("monitor link0: ") != std::string::npos);
    CHECK(out.find("stop_reason=cycle_limit") != std::string::npos);
    CHECK_EQ(runTlsim(base + "cycles(i),0\nmax_cycles(i),130\nmonitor_mode(s),fatal\n", "exit2", &out), 2);
    CHECK(out.find("TileLink: M-RESP-7") != std::string::npos);
    CHECK_EQ(runTlsim(base + "cycles(i),0\n", "exit0", &out), 0);
    CHECK(out.find("monitor link0: 0 violations") != std::string::npos);
}

int main(int argc, char **argv)
{
    g_argv0 = argv[0];
    if (argc >= 3 && std::string(argv[1]) == "--fatal")
    {
        const Mutant *mu = find(argv[2]);
        if (!mu) { std::fprintf(stderr, "unknown mutant %s\n", argv[2]); return 1; }
        run(*mu, MonitorOptions::Fatal);
        return 0;   // no violation: the parent sees 0, not 2
    }
    int failed_cases = 0;
    for (auto &c : tltest::cases())
    {
        int before = tltest::failures();
        std::printf("[TL-NEG] %s\n", c.name);
        c.body();
        if (tltest::failures() != before) failed_cases++;
    }
    std::printf("TL-NEG: %zu cases, %d checks, %d failed\n", tltest::cases().size(), tltest::checks(), failed_cases);
    return failed_cases ? 1 : 0;
}
