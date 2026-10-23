/*
 * File  :      tlsim.cpp
 * The standalone driver (plan §4). Same flags as Octopus_Simulator:
 *   tlsim -c <config.csv> [-o <output-dir>] [--trace] [-p name(type)=value]...
 * Config keys: cycles(i) (default 1000; 0 = run until idle, bounded by max_cycles(i)),
 * reset_cycles(i), seed(i), data_mode(s), trace(i), agent[k].type(s)/name(s) and the agent's own
 * keys, link[k].master(s)/slave(s)/w(i)/a(i)/z(i)/o(i)/i(i)/level(s)/name(s); the checkers:
 *   monitor(i) 1                one conformance Monitor per link (plan §3.6)
 *   monitor_mode(s) fatal       fatal: the first violation exits 2; report: count and go on
 *   block_bytes(i) 64, allow_acquire_under_acquire(i) 0, c_ordered(i) 1   monitor parameters (TL-C)
 *   standalone(i) 1             M-RESET-2: reset_cycles >= 100 standalone
 *   graph_check(i), watchdog(i), progress_bound(i), watchdog_mode(s)   Kernel::build (T4)
 *   value_check(vs)             links to watch with ONE shared ValueCheck (plan §3.6), e.g. the
 *                               masters' links for an end-to-end check; needs data_mode full|stamp
 * Outputs in the output directory (default "."): TLStats.csv, transcript.txt (trace=1 or --trace),
 * monitor.txt (the per-link monitor summaries, also printed).
 * Exit status: 0 clean; 1 usage; 2 a fatal error or fatal violation (monitor, watchdog, a refused
 * configuration); 3 the monitor in report mode counted violations; 4 a run until drained (cycles(i) 0) reached
 * max_cycles(i) with work left (incomplete; printed as stop_reason=cycle_limit with the busy agents and links).
 */
#include "tilelink/Kernel.h"
#include "tilelink/Monitor.h"
#include "tilelink/ValueCheck.h"
#include "tilelink/agents/Registry.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace octopus::tl;

static void usage(const char *prog)
{
    std::cerr << "Usage: " << prog << " -c <config.csv> [-o <output-directory>] [--trace] [-p name(type)=value]..." << std::endl;
}

int main(int argc, char *argv[])
{
    std::string config_path, out_dir = ".";
    std::vector<std::string> overrides;
    bool trace = false;
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        bool needs_value = (a == "-c" || a == "--config" || a == "-o" || a == "-p");
        if (needs_value && (i + 1 == argc || argv[i + 1][0] == '\0' || (argv[i + 1][0] == '-' && a != "-p")))
        {
            std::cerr << "Missing value for " << a << std::endl;
            return 1;
        }
        if (a == "-c" || a == "--config") config_path = argv[++i];
        else if (a == "-o") out_dir = argv[++i];
        else if (a == "-p") overrides.push_back(argv[++i]);
        else if (a == "--trace") trace = true;
        else { usage(argv[0]); return 1; }
    }
    if (config_path.empty()) { usage(argv[0]); return 1; }

    Config cfg;
    std::string err;
    if (!cfg.load(config_path, &err)) fatal(err);
    try { for (auto &o : overrides) cfg.set(o); }
    catch (ConfigError &e) { fatal(std::string("bad -p override: ") + e.what()); }

    MonitorOptions mo;
    bool monitor = true;
    try
    {
        if (cfg.has("trace") && cfg.get<int>("trace") != 0) trace = true;
        monitor = cfg.get<int>("monitor", 1) != 0;
        std::string mode = cfg.get<std::string>("monitor_mode", "fatal");
        if (!MonitorOptions::parseMode(mode, mo.mode)) throw ConfigError("monitor_mode must be fatal or report, got " + mode);
        mo.block_bytes = unsigned(cfg.get<int>("block_bytes", 64));
        mo.allow_acquire_under_acquire = cfg.get<int>("allow_acquire_under_acquire", 0) != 0;
        mo.c_ordered = cfg.get<int>("c_ordered", 1) != 0;
    }
    catch (ConfigError &e) { fatal(e.what()); }

    mkdir(out_dir.c_str(), 0755);   // exists already: fine, the open below reports a real failure

    Kernel kernel;
    kernel.build(cfg, withAllAgents());
    if (kernel.agents().empty()) fatal("the configuration declares no agent (agent[k].type)");

    // M-RESET-2 (§3.2.2, plan §3.2): standalone, reset is asserted for at least 100 cycles; refused at
    // elaboration (exit 2 before cycle 0). A system of only NullMaster/NullSlave agents can never drive
    // valid and is exempt (T1's smoke configuration test/unit/null_system.csv uses reset_cycles 10).
    if (monitor)
    {
        bool only_null = true;
        for (auto &a : kernel.agents())
            if (!dynamic_cast<NullMaster *>(a.get()) && !dynamic_cast<NullSlave *>(a.get())) only_null = false;
        std::string r = Monitor::checkResetCycles(kernel.options().reset_cycles, cfg.get<int>("standalone", 1) != 0);
        if (!r.empty() && !only_null) fatal(r);
    }

    if (trace) kernel.enableTranscript(out_dir + "/transcript.txt");
    std::vector<std::unique_ptr<Monitor>> monitors;
    if (monitor) monitors = Monitor::attachAll(kernel, mo);
    // after the monitors: ValueCheck chains onto the hooks they installed (both keep firing)
    std::unique_ptr<ValueCheck> vcheck;
    std::vector<std::string> vlinks = cfg.get<std::vector<std::string>>("value_check", {});
    if (!vlinks.empty())
    {
        if (kernel.options().data_mode == DataMode::None) fatal("value_check needs data_mode full or stamp");
        vcheck.reset(new ValueCheck());
        for (auto &n : vlinks)
        {
            Link *l = kernel.link(n);
            if (!l) fatal("value_check: no link named " + n);
            vcheck->attach(*l);
        }
    }

    // docs/TileLink_Review_Action_Plan.md problem 6: cycles(i) 0 = run until drained, bounded by max_cycles(i); a
    // budget exhausted with work left is an incomplete run (exit 4), not a success. cycles(i) > 0 = a fixed
    // observation window, which need not drain.
    const int cycles_i = cfg.get<int>("cycles", 1000), max_i = cfg.get<int>("max_cycles", 1000000);
    if (cycles_i < 0) fatal("cycles(i) must be >= 0, got " + std::to_string(cycles_i));
    if (max_i < 0) fatal("max_cycles(i) must be >= 0, got " + std::to_string(max_i));
    const uint64_t cycles = uint64_t(cycles_i), limit = uint64_t(max_i);
    uint64_t ran;
    bool incomplete = false;
    std::string stop_line;
    if (cycles == 0)
    {
        Kernel::RunResult r = kernel.runToDrain(limit);
        ran = r.cycles;
        incomplete = !r.drained();
        stop_line = std::string("tlsim: stop_reason=") + (r.drained() ? "idle" : "cycle_limit") + ", complete=" + (r.drained() ? "true" : "false") +
                    ", cycles=" + std::to_string(ran) + ", limit=" + std::to_string(limit);
    }
    else
    {
        kernel.run(cycles); ran = cycles;
        stop_line = std::string("tlsim: stop_reason=fixed_cycles, drained=") + (kernel.drained() ? "true" : "false") + ", cycles=" + std::to_string(ran);
    }
    std::vector<std::string> busy = incomplete ? kernel.busy() : std::vector<std::string>();
    const bool fixed_undrained = cycles != 0 && !kernel.drained();
    // the stop reason first: a fatal end-of-run report below must not hide why the run stopped
    std::cout << stop_line << std::endl;
    if (incomplete)
    {
        std::cout << "tlsim: incomplete: the cycle limit " << limit << " was reached with work left:";
        for (size_t i = 0; i < busy.size() && i < 10; i++) std::cout << (i ? ", " : " ") << busy[i];
        if (busy.size() > 10) std::cout << ", ... (" << busy.size() << " in all)";
        std::cout << std::endl;
    }
    kernel.writeStats(out_dir + "/TLStats.csv");
    if (kernel.transcript()) kernel.transcript()->flush();

    // M-BURST-4, M-BURST-5, M-RESP-7 are "eventually" rules: a fixed window that ends mid-transaction is an
    // observation, not a drained run, so they apply only when it drained (or in a run until drained)
    if (fixed_undrained) std::cout << "tlsim: end-of-run rules not applied: the fixed window ended with work in flight" << std::endl;
    else Monitor::finishAll(monitors, kernel.cycle());
    uint64_t violations = Monitor::totalViolations(monitors);
    if (monitor)
    {
        std::ofstream f(out_dir + "/monitor.txt");
        for (auto &m : monitors)
        {
            std::cout << m->summary() << std::endl;
            if (f) f << m->summary() << "\n";
        }
    }
    if (vcheck)
    {
        const ValueCheck::Counters &c = vcheck->counters();
        std::cout << "value check: " << c.reads << " reads, " << c.writes << " writes, " << c.atomics << " atomics, "
                  << c.bytes_checked << " bytes checked, " << c.violations << " violations" << std::endl;
    }
    std::cout << "tlsim: " << kernel.agents().size() << " agents, " << kernel.links().size() << " links, "
              << ran << " cycles, outputs in " << out_dir << std::endl;
    monitors.clear();   // before the kernel that owns the links
    // 3: the monitor counted violations (the primary verdict); 4: the run did not drain within max_cycles
    return violations ? 3 : (incomplete ? 4 : 0);
}
