/*
 * File  :      test_tlcsys.cpp
 * TL-SYS-C (plan §4): the example TL-C systems as tlsim builds them (Kernel::build from the CSV, so
 * GraphCheck, the watchdog at its default bound and CoherenceCheck come from the kernel, all fatal),
 * a Monitor on every link in report mode: 0 violations, every CacheMaster done, the run reaches idle.
 * System 4 replays the four EEMBC a2time01 traces (skipped, with a message, when BMs/ is absent).
 */
#include "TestMain.h"
#include "tilelink/CoherenceCheck.h"
#include "tilelink/Monitor.h"
#include "tilelink/agents/CacheMaster.h"
#include "tilelink/agents/CoherentManager.h"
#include "tilelink/agents/RamSlave.h"
#include "tilelink/agents/Registry.h"
#include "tilelink/agents/TraceMaster.h"
#include "tilelink/ValueCheck.h"
#include <fstream>

using namespace octopus::tl;

namespace
{
    uint64_t runSystem(const std::string &file, const std::vector<std::string> &overrides, uint64_t &accesses)
    {
        Config cfg;
        std::string err;
        CHECK(cfg.load(std::string(TL_CONFIG_DIR) + "/" + file, &err));
        for (auto &o : overrides) cfg.set(o);
        Kernel k;
        k.build(cfg, withAllAgents());
        MonitorOptions mo; mo.mode = MonitorOptions::Report;
        auto mons = Monitor::attachAll(k, mo);
        uint64_t n = k.runUntilIdle(uint64_t(cfg.get<int>("max_cycles", 5000000)));
        Monitor::finishAll(mons, k.cycle());
        uint64_t v = Monitor::totalViolations(mons);
        CHECK_EQ(v, uint64_t(0));
        if (v) for (auto &m : mons) if (m->violations()) std::printf("  %s\n", m->summary().c_str());
        CHECK(n < uint64_t(cfg.get<int>("max_cycles", 5000000)));
        accesses = 0;
        for (auto &a : k.agents())
            if (auto *c = dynamic_cast<CacheMaster *>(a.get()))
            {
                CHECK(c->cpuDone());
                accesses += c->counters().loads + c->counters().stores;
            }
        auto *cc = CoherenceCheck::of(k);
        CHECK(cc != nullptr);
        if (cc) { CHECK_EQ(cc->counters().violations, uint64_t(0)); CHECK(cc->attached().size() == 4); }
        mons.clear();
        return n;
    }
}

TEST(system3_mesi_and_msi)
{
    for (const char *proto : {"MESI", "MSI"})
        for (int seed = 1; seed <= 3; seed++)
        {
            uint64_t acc = 0;
            uint64_t n = runSystem("system3_tlc.csv", {"seed(i)=" + std::to_string(seed), std::string("agent[5].protocol(s)=") + proto}, acc);
            CHECK_EQ(acc, uint64_t(4 * 5000));
            std::printf("  system 3 %s seed %d: %llu accesses, %llu cycles\n", proto, seed, (unsigned long long)acc, (unsigned long long)n);
        }
}

TEST(system4_eembc_a2time01)
{
    std::string t0 = std::string(TL_TRACE_DIR) + "/trace_C0.trc.shared";
    if (!std::ifstream(t0)) { std::printf("  SKIPPED: %s not found (run get_benchmarks.sh)\n", t0.c_str()); return; }
    std::vector<std::string> o;
    for (int k = 0; k < 4; k++) o.push_back("agent[" + std::to_string(k) + "].trace(s)=" + std::string(TL_TRACE_DIR) + "/trace_C" + std::to_string(k) + ".trc.shared");
    uint64_t acc = 0;
    uint64_t n = runSystem("system4_trace.csv", o, acc);
    CHECK_EQ(acc, uint64_t(4 * 68826));   // every line of the four traces
    std::printf("  system 4 (a2time01, 4 cores): %llu accesses, %llu cycles\n", (unsigned long long)acc, (unsigned long long)n);
}

TEST(tracemaster_uncached_and_parser)
{
    // the Octopus format (src/CPU.cpp: "%llx %*d %c %lld"; any type other than R is a write)
    uint64_t a, c; bool st;
    CHECK(TraceReader::parseLine("7ffd3412b9c8 1 W 9", a, st, c));
    CHECK(a == 0x7ffd3412b9c8ull && st && c == 9);
    CHECK(TraceReader::parseLine("4002a8 1 R 120", a, st, c));
    CHECK(a == 0x4002a8 && !st && c == 120);
    CHECK(!TraceReader::parseLine("", a, st, c));
    std::string t0 = std::string(TL_TRACE_DIR) + "/trace_C0.trc.shared";
    if (!std::ifstream(t0)) { std::printf("  SKIPPED: %s not found\n", t0.c_str()); return; }
    // mode uncached (system 2 style): Get / PutFullData of 8 bytes on a TL-UH link, in trace timing
    Kernel k(KernelOptions{100, 1, DataMode::Full});
    Config tc;
    tc.set("trace(s)=" + t0); tc.set("max_ops(i)=5000"); tc.set("outstanding(i)=4"); tc.set("timing(s)=trace"); tc.set("max_gap(i)=20");
    auto *tm = static_cast<TraceMaster *>(&k.add(std::unique_ptr<Agent>(new TraceMaster("tm", tc))));
    Config rc; rc.set("latency(i)=3");
    k.add(std::unique_ptr<Agent>(new RamSlave("mem", rc)));
    LinkParams p; p.level = Level::UH; p.w = 8; p.a = 48; p.o = 2;
    Link &l = k.connect("L", p, *tm, *k.agent("mem"));
    MonitorOptions mo; mo.mode = MonitorOptions::Report;
    auto mons = Monitor::attachAll(k, mo);
    ValueCheck vc;
    vc.attach(l);
    uint64_t n = k.runUntilIdle(10000000);
    Monitor::finishAll(mons, k.cycle());
    CHECK_EQ(tm->issued(), uint64_t(5000));
    CHECK_EQ(tm->answered(), uint64_t(5000));
    CHECK_EQ(Monitor::totalViolations(mons), uint64_t(0));
    CHECK_EQ(vc.counters().violations, uint64_t(0));
    CHECK(vc.counters().reads > 0 && vc.counters().writes > 0);
    std::printf("  TraceMaster uncached: 5000 accesses of a2time01 core 0, %llu cycles, %llu reads / %llu writes value-checked\n",
                (unsigned long long)n, (unsigned long long)vc.counters().reads, (unsigned long long)vc.counters().writes);
    // mode cpu is the CacheMaster's trace(s): the agent refuses it with that pointer
    Config bad; bad.set("trace(s)=" + t0); bad.set("mode(s)=cpu");
    bool threw = false;
    try { TraceMaster x("x", bad); } catch (ConfigError &e) { threw = std::string(e.what()).find("CacheMaster") != std::string::npos; }
    CHECK(threw);
    mons.clear();
}

TL_TEST_MAIN("TL-SYS-C")
