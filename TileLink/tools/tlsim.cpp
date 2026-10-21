/*
 * File  :      tlsim.cpp
 * The standalone driver (plan §4). Same flags as Octopus_Simulator:
 *   tlsim -c <config.csv> [-o <output-dir>] [--trace] [-p name(type)=value]...
 * Config keys: cycles(i) (default 1000; 0 = run until idle, bounded by max_cycles(i)),
 * reset_cycles(i), seed(i), data_mode(s), trace(i), agent[k].type(s)/name(s), link[k].master(s)/
 * slave(s)/w(i)/a(i)/z(i)/o(i)/i(i)/level(s)/name(s). Writes TLStats.csv and, with trace=1 or
 * --trace, transcript.txt into the output directory (default ".").
 * The agent registry holds only NullMaster/NullSlave until T2 adds real agents.
 */
#include "tilelink/Kernel.h"
#include <cstring>
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
    if (cfg.has("trace") && cfg.get<int>("trace") != 0) trace = true;

    mkdir(out_dir.c_str(), 0755);   // exists already: fine, the open below reports a real failure

    Kernel kernel;
    kernel.build(cfg, AgentRegistry::withBuiltins());
    if (kernel.agents().empty()) fatal("the configuration declares no agent (agent[k].type)");
    if (trace) kernel.enableTranscript(out_dir + "/transcript.txt");

    uint64_t cycles = uint64_t(cfg.get<int>("cycles", 1000));
    uint64_t ran;
    if (cycles == 0) ran = kernel.runUntilIdle(uint64_t(cfg.get<int>("max_cycles", 1000000)));
    else { kernel.run(cycles); ran = cycles; }
    kernel.writeStats(out_dir + "/TLStats.csv");
    std::cout << "tlsim: " << kernel.agents().size() << " agents, " << kernel.links().size() << " links, "
              << ran << " cycles, outputs in " << out_dir << std::endl;
    return 0;
}
