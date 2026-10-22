/*
 * File  :      test_deny.cpp
 * TL-DENY, TL-UL/UH part (plan §6): a FuzzMaster against an ErrorSlave (everything denied) and
 * against a RamSlave with a denied range. §4.4: a denied operation has no effect and a denied data
 * response marks every beat corrupt; every request still gets its response with all its beats.
 * Checked: 0 monitor violations (M-ERR-2 on every beat, M-RESP-7 at the end), every request
 * answered, every denied data beat corrupt, and the denied memory never written.
 */
#include "TestMain.h"
#include "tilelink/Monitor.h"
#include "tilelink/agents/ErrorSlave.h"
#include "tilelink/agents/FuzzMaster.h"
#include "tilelink/agents/RamSlave.h"

using namespace octopus::tl;

namespace
{
    Config params(std::initializer_list<const char *> lines)
    {
        Config c;
        for (const char *l : lines) c.set(l);
        return c;
    }

    struct Run
    {
        FuzzMaster *fm = nullptr;
        RamSlave *rs = nullptr;
        uint64_t violations = 0, cycles = 0;
        std::string summary;
    };

    Run run(Level level, bool error_slave, const Config &slave_params, uint64_t seed, unsigned ops)
    {
        static std::vector<std::unique_ptr<Kernel>> keep;   // the agents are inspected after the run
        keep.emplace_back(new Kernel(KernelOptions{100, seed, DataMode::Full}));
        Kernel &k = *keep.back();
        Config mp = params({"ops(i)=0", "rate(i)=80", "retract_pct(i)=5", "d_ready_pct(i)=85", "fence_every(i)=50"});
        mp.set("ops(i)=" + std::to_string(ops));
        Run r;
        r.fm = static_cast<FuzzMaster *>(&k.add(std::unique_ptr<Agent>(new FuzzMaster("m", mp))));
        if (error_slave) r.rs = static_cast<RamSlave *>(&k.add(std::unique_ptr<Agent>(new ErrorSlave("s", slave_params))));
        else r.rs = static_cast<RamSlave *>(&k.add(std::unique_ptr<Agent>(new RamSlave("s", slave_params))));
        LinkParams p; p.w = 8; p.z = 3; p.o = 3; p.level = level;
        k.connect("L0", p, *r.fm, *r.rs);
        MonitorOptions mo; mo.mode = MonitorOptions::Report;
        auto mons = Monitor::attachAll(k, mo);
        r.cycles = k.runUntilIdle(5000000);
        Monitor::finishAll(mons, k.cycle());
        r.violations = Monitor::totalViolations(mons);
        r.summary = mons[0]->summary();
        return r;
    }

    void common(const Run &r, unsigned ops)
    {
        CHECK_EQ(r.violations, uint64_t(0));
        if (r.violations) std::printf("  %s\n", r.summary.c_str());
        CHECK_EQ(r.fm->issued(), uint64_t(ops));
        CHECK_EQ(r.fm->answered(), uint64_t(ops));          // every request answered
        CHECK_EQ(r.rs->responses(), uint64_t(ops));
        CHECK_EQ(r.fm->deniedCorruptBeats(), r.fm->deniedDataBeats());   // §4.4: all beats corrupt
    }
}

TEST(error_slave_ul)
{
    const unsigned ops = 3000;
    Run r = run(Level::UL, true, params({"level(s)=UL", "latency(i)=2", "latency_jitter(i)=3", "addr_size(i)=4096"}), 1, ops);
    common(r, ops);
    CHECK_EQ(r.fm->deniedResponses(), uint64_t(ops));       // everything denied
    CHECK(r.fm->deniedDataBeats() > 0);
    for (uint64_t a = 0; a < 4096; a++) if (r.rs->readByte(a) != 0) { CHECK(false); break; }   // no effect
    std::printf("  UL: %u ops, %llu denied data beats all corrupt, %llu cycles\n", ops,
                (unsigned long long)r.fm->deniedDataBeats(), (unsigned long long)r.cycles);
}

TEST(error_slave_uh_bursts)
{
    const unsigned ops = 3000;
    Run r = run(Level::UH, true, params({"latency(i)=3", "latency_jitter(i)=4", "queue_depth(i)=4", "addr_size(i)=4096", "max_size(i)=6"}), 2, ops);
    common(r, ops);
    CHECK_EQ(r.fm->deniedResponses(), uint64_t(ops));
    CHECK(r.fm->deniedDataBeats() > ops);                  // bursts: more beats than responses
    CHECK(r.fm->opsIssued(unsigned(OpA::ArithmeticData)) > 0);
    CHECK(r.fm->opsIssued(unsigned(OpA::Intent)) > 0);
    for (uint64_t a = 0; a < 4096; a++) if (r.rs->readByte(a) != 0) { CHECK(false); break; }
    std::printf("  UH: %u ops, %llu denied data beats all corrupt, %llu cycles\n", ops,
                (unsigned long long)r.fm->deniedDataBeats(), (unsigned long long)r.cycles);
}

TEST(ram_slave_denied_range)
{
    // [0x800, 0xc00) denied, the rest served: denied responses there, normal ones elsewhere.
    const unsigned ops = 4000;
    Run r = run(Level::UH, false, params({"latency(i)=2", "latency_jitter(i)=2", "addr_size(i)=4096", "max_size(i)=6", "deny(vi)=2048,1024"}), 3, ops);
    common(r, ops);
    CHECK(r.fm->deniedResponses() > 0);
    CHECK(r.fm->deniedResponses() < ops);
    CHECK_EQ(r.rs->deniedCount(), r.fm->deniedResponses());
    CHECK(r.fm->deniedDataBeats() > 0);
    bool written_outside = false, written_inside = false;
    for (uint64_t a = 0; a < 4096; a++)
    {
        bool in = a >= 0x800 && a < 0xc00;
        if (r.rs->readByte(a) != 0) (in ? written_inside : written_outside) = true;
    }
    CHECK(written_outside);
    CHECK(!written_inside);                                  // §4.4: a denied write has no effect
    std::printf("  RamSlave deny [0x800,0xc00): %llu of %u denied, %llu denied data beats all corrupt\n",
                (unsigned long long)r.fm->deniedResponses(), ops, (unsigned long long)r.fm->deniedDataBeats());
}

TL_TEST_MAIN("TL-DENY")
