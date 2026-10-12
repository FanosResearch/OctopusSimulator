// Reproduction: LLC evictions with the pipelined data array.
// Tiny LLC (8 KiB) so a read stream over 64 KiB evicts continuously; every
// eviction goes through the PWB and an LLC WriteBack (WRITE_BACK then state N).
#include "CacheSim.h"
#include "ExternalCPU.h"
#include <cstdio>
using namespace octopus;
static uint64_t completions = 0;
struct Sink { void done(uint64_t, uint64_t, uint64_t, RequestType, uint8_t*) { completions++; } void commit(uint64_t, uint64_t) {} void inv(uint64_t) {} };
static ExternalCPU *attach(int id, Sink &s) { ExternalCPU *c = ExternalCPU::getExtCPUs()->at(id);
    c->registerCPUCallback(new CPUCallback<Sink, uint64_t, uint64_t, uint64_t, RequestType, uint8_t*>(&s, &Sink::done));
    c->registerCommitCallback(new CPUCallback<Sink, uint64_t, uint64_t>(&s, &Sink::commit));
    c->registerInvalidateCallback(new CPUCallback<Sink, uint64_t>(&s, &Sink::inv)); return c; }
static bool drain(CacheSim &sim, uint64_t want) { int steps = 0; while (completions < want && steps < 4000000) { sim.step(); steps++; } return completions >= want; }
int main(int argc, char **argv) {
    std::vector<std::string> cl = { std::string("workload_path(s)=") + argv[1] }; for (int i = 2; i < argc; i++) cl.push_back(argv[i]);
    CacheSim sim("MultiCoreSystem", cl, false, "MultiCoreSystem_gem5"); Sink sink; ExternalCPU *a = attach(4, sink);
    for (int i = 0; i < 4; i++) sim.step();
    const int N = 1024; const uint64_t base = 0x400000; uint64_t issued = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < N; i++) { a->addRequest(base + (uint64_t)i * 64, READ, NULL, 8); issued++;
            if (issued % 16 == 0 && !drain(sim, issued)) { printf("HANG after %lu completions\n", completions); return 2; } }
    if (!drain(sim, issued)) { printf("HANG after %lu completions\n", completions); return 2; }
    printf("PASS: %lu reads completed, %.0f cycles\n", completions, sim.now() / 100.0); return 0; }
