/*
 * File  :      test_fuzzc.cpp
 * TL-FUZZ-C (plan §6): random loads, stores and evictions from 2, 4 and 8 CacheMasters on 16 blocks
 * (high contention), small L1s (2 sets x 2 ways) and a small inclusive manager (2 sets x 4 ways) so
 * that both levels evict with holders inside, Throttles withholding ready and valid on every link,
 * a Monitor on every link (report mode), CoherenceCheck (SWMR, C-GRANT, C-VALUE),
 * ValueCheck on the outer link, GraphCheck and the watchdog at its default bound. 10 seeds each,
 * MESI and MSI managers, direct links (one link per master) and through an Xbar.
 * Pass: 0 monitor violations, 0 coherence and value violations, no watchdog trip, every access done,
 * the run reaches idle. MSI: no L1 ever enters E.
 */
#include "TestMain.h"
#include "../fixtures/TlcSystem.h"

using namespace octopus::tl;

namespace
{
    void fuzz(unsigned masters, const std::string &proto, bool xbar)
    {
        uint64_t cycles = 0, ops = 0, mv = 0, cv = 0, vv = 0, trips = 0, evictions = 0, probes = 0, e_states = 0, checked = 0;
        for (uint64_t seed = 1; seed <= 10; seed++)
        {
            tlc::Options o;
            o.masters = masters; o.protocol = proto; o.seed = seed; o.xbar = xbar;
            o.ops = 1500;
            tlc::System s;
            s.build(o);
            uint64_t n = s.run(o.max_cycles);
            CHECK(n < o.max_cycles);
            CHECK(s.allDone());
            CHECK_EQ(s.opsDone(), uint64_t(masters) * o.ops);
            uint64_t v = s.monitorViolations();
            CHECK_EQ(v, uint64_t(0));
            if (v) std::printf("  seed %llu:\n%s", (unsigned long long)seed, s.monitorSummary().c_str());
            CHECK_EQ(s.cviol.size(), size_t(0));
            CHECK_EQ(s.vviol.size(), size_t(0));
            CHECK_EQ(s.wd->trips().size(), size_t(0));
            if (!s.wd->trips().empty()) std::printf("  seed %llu: %s\n", (unsigned long long)seed, s.wd->trips()[0].line().c_str());
            const int E = s.cms[0]->policy().table().stateId("E");
            for (auto *c : s.cms) e_states += c->counters().states_entered[E];
            cycles += n; ops += s.opsDone(); mv += v; cv += s.cviol.size(); vv += s.vviol.size(); trips += s.wd->trips().size();
            CHECK(s.cc->counters().bytes_checked > 0);
            evictions += s.mgr->counters().evictions; probes += s.mgr->counters().probes; checked += s.cc->counters().bytes_checked;
        }
        if (proto == "MSI") CHECK_EQ(e_states, uint64_t(0));   // the MSI manager never grants toT on NtoB
        else CHECK(e_states > 0);
        std::printf("  %u masters %s %s: 10 seeds, %llu ops, %llu cycles, %llu LLC evictions, %llu probes, %llu E entries, %llu bytes value-checked; "
                    "violations: monitor %llu, coherence %llu, value %llu, watchdog trips %llu\n",
                    masters, proto.c_str(), xbar ? "xbar" : "direct", (unsigned long long)ops, (unsigned long long)cycles,
                    (unsigned long long)evictions, (unsigned long long)probes, (unsigned long long)e_states, (unsigned long long)checked,
                    (unsigned long long)mv, (unsigned long long)cv, (unsigned long long)vv, (unsigned long long)trips);
    }
}

TEST(mesi_2_direct) { fuzz(2, "MESI", false); }
TEST(mesi_4_direct) { fuzz(4, "MESI", false); }
TEST(mesi_8_direct) { fuzz(8, "MESI", false); }
TEST(msi_2_direct)  { fuzz(2, "MSI", false); }
TEST(msi_4_direct)  { fuzz(4, "MSI", false); }
TEST(msi_8_direct)  { fuzz(8, "MSI", false); }
TEST(mesi_4_xbar)   { fuzz(4, "MESI", true); }
TEST(msi_8_xbar)    { fuzz(8, "MSI", true); }

TL_TEST_MAIN("TL-FUZZ-C")
