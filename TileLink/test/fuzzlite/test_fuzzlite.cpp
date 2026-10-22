/*
 * File  :      test_fuzzlite.cpp
 * TL-FUZZ-U-lite (T2): configs/system1_ul.csv and configs/system2_uh.csv, 50k requests each,
 * seeds 1..10, built through Kernel::build (so GraphCheck and the watchdog with its default bound
 * are attached, fatal) with a Monitor per link in report mode: 0 monitor violations, every request
 * answered, the run reaches idle.
 */
#include "TestMain.h"
#include "tilelink/Monitor.h"
#include "tilelink/agents/FuzzMaster.h"
#include "tilelink/agents/Registry.h"

using namespace octopus::tl;

namespace
{
    void fuzz(const std::string &sys)
    {
        const unsigned ops = 50000;
        uint64_t total_cycles = 0, total_viol = 0;
        for (int seed = 1; seed <= 10; seed++)
        {
            Config cfg;
            std::string err;
            CHECK(cfg.load(std::string(TL_CONFIG_DIR) + "/" + sys + ".csv", &err));
            cfg.set("seed(i)=" + std::to_string(seed));
            cfg.set("agent[0].ops(i)=" + std::to_string(ops));
            Kernel k;
            k.build(cfg, withAllAgents());
            MonitorOptions mo; mo.mode = MonitorOptions::Report;
            auto mons = Monitor::attachAll(k, mo);
            uint64_t n = k.runUntilIdle(50000000);
            Monitor::finishAll(mons, k.cycle());
            uint64_t v = Monitor::totalViolations(mons);
            const FuzzMaster *fm = dynamic_cast<const FuzzMaster *>(k.agent("m0"));
            CHECK(fm != nullptr);
            CHECK_EQ(v, uint64_t(0));
            if (v) std::printf("  %s seed %d: %s\n", sys.c_str(), seed, mons[0]->summary().c_str());
            CHECK(n < 50000000);
            if (fm) { CHECK_EQ(fm->issued(), uint64_t(ops)); CHECK_EQ(fm->answered(), uint64_t(ops)); }
            total_cycles += n; total_viol += v;
            mons.clear();
        }
        std::printf("  %s: 10 seeds x %u requests, %llu cycles, %llu violations\n", sys.c_str(), ops,
                    (unsigned long long)total_cycles, (unsigned long long)total_viol);
    }
}

TEST(system1_ul_50k_x10) { fuzz("system1_ul"); }
TEST(system2_uh_50k_x10) { fuzz("system2_uh"); }

TL_TEST_MAIN("TL-FUZZ-U-lite")
