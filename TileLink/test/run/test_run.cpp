// TL-RUN (docs/TileLink_Review_Action_Plan.md problem 6): tlsim's exit status and outputs. A run until drained that
// reaches max_cycles with work left is incomplete (exit 4, stop_reason=cycle_limit), with or without the monitor;
// a drained run exits 0; report-mode violations stay exit 3 (the primary verdict); a fixed window (cycles > 0)
// reports stop_reason=fixed_cycles; negative limits are refused (exit 2). The outputs are written in every case. A
// run that drains on its last allowed cycle succeeds; one cycle less is incomplete.
#include "TestMain.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>

namespace
{
    struct Out { int status = -1; std::string text; bool stats = false; };
    Out tlsim(const std::string &name, const std::string &args)
    {
        std::string dir = std::string(TL_RUN_OUT) + "/" + name;
        std::string cmd = std::string(TLSIM_PATH) + " -c " + TL_CONFIGS + "/system1_ul.csv -o " + dir + " " + args + " > " + dir + ".out 2>&1";
        if (std::system(("mkdir -p " + dir).c_str()) != 0) return Out();
        int st = std::system(cmd.c_str());
        Out o;
        o.status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        std::ifstream f(dir + ".out"); std::stringstream ss; ss << f.rdbuf(); o.text = ss.str();
        std::ifstream s(dir + "/TLStats.csv"); o.stats = s.good();
        return o;
    }
    bool has(const Out &o, const std::string &s) { return o.text.find(s) != std::string::npos; }
}

TEST(budget_exhausted_is_incomplete)
{
    Out o = tlsim("limit", "-p 'max_cycles(i)=100'");                 // the review's reproduction: reset only, no traffic
    CHECK_EQ(o.status, 4);
    CHECK(has(o, "stop_reason=cycle_limit, complete=false, cycles=100, limit=100"));
    CHECK(has(o, "incomplete") && has(o, "agent m0"));
    CHECK(o.stats);                                                    // the outputs are still written
    Out q = tlsim("limit_nomon", "-p 'max_cycles(i)=300' -p 'monitor(i)=0'");   // without the monitor, too
    CHECK_EQ(q.status, 4);
}

TEST(drained_run_succeeds)
{
    Out o = tlsim("full", "");
    CHECK_EQ(o.status, 0);
    CHECK(has(o, "stop_reason=idle, complete=true"));
    CHECK(o.stats);
}

TEST(fixed_window_is_its_own_mode)
{
    Out o = tlsim("fixed", "-p 'cycles(i)=500'");                     // an observation window: not a failure
    CHECK_EQ(o.status, 0);
    CHECK(has(o, "stop_reason=fixed_cycles"));
}

TEST(violations_stay_the_primary_verdict)
{
    // a report-mode violation in a run that also hits its limit: exit 3, both facts printed. The limit stops it
    // mid-transaction, so the monitor's end-of-run rule reports the unanswered requests (M-RESP-7)
    Out o = tlsim("both", "-p 'max_cycles(i)=150' -p 'monitor_mode(s)=report'");
    CHECK_EQ(o.status, 3);
    CHECK(has(o, "stop_reason=cycle_limit, complete=false, cycles=150"));
    CHECK(has(o, "M-RESP-7") && has(o, "violations"));
    CHECK(o.stats);
}

TEST(drained_on_the_last_allowed_cycle)
{
    // the drained run's cycle count N as the limit: drained after the last allowed step, a success; N - 1: incomplete
    Out o = tlsim("count", "");
    size_t at = o.text.find("stop_reason=idle, complete=true, cycles=");
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    unsigned long n = std::strtoul(o.text.c_str() + at + std::string("stop_reason=idle, complete=true, cycles=").size(), nullptr, 10);
    CHECK(n > 100);
    Out exact = tlsim("exact", "-p 'max_cycles(i)=" + std::to_string(n) + "'");
    CHECK_EQ(exact.status, 0);
    CHECK(has(exact, "stop_reason=idle, complete=true, cycles=" + std::to_string(n) + ", limit=" + std::to_string(n)));
    Out shy = tlsim("short", "-p 'max_cycles(i)=" + std::to_string(n - 1) + "'");
    CHECK_EQ(shy.status, 4);
    CHECK(has(shy, "stop_reason=cycle_limit"));
}

TEST(negative_limits_refused)
{
    CHECK_EQ(tlsim("neg_max", "-p 'max_cycles(i)=-1'").status, 2);
    CHECK_EQ(tlsim("neg_cycles", "-p 'cycles(i)=-5'").status, 2);
}

TL_TEST_MAIN("TL-RUN")
