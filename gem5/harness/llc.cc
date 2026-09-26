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
static uint64_t drain(CacheSim &sim, uint64_t want) { uint64_t t0 = sim.now(); int steps = 0; while (completions < want && steps < 1000000) { sim.step(); steps++; } return sim.now() - t0; }
int main(int argc, char **argv) {
    std::vector<std::string> cl = { std::string("workload_path(s)=") + argv[1] }; for (int i = 2; i < argc; i++) cl.push_back(argv[i]);
    CacheSim sim("MultiCoreSystem", cl, false, "MultiCoreSystem_gem5"); Sink sink; ExternalCPU *a = attach(4, sink), *b = attach(5, sink);
    for (int i = 0; i < 4; i++) sim.step(); const int N = 20; const uint64_t base = 0x400000;
    for (int i = 0; i < N; i++) a->addRequest(base + i * 64, READ, NULL, 8); drain(sim, N);
    b->addRequest(base, READ, NULL, 8); uint64_t one = drain(sim, N + 1);
    for (int i = 1; i < N; i++) b->addRequest(base + i * 64, READ, NULL, 8); uint64_t burst = drain(sim, 2 * N);
    printf("LLC hit alone: %.0f cycles; %d LLC hits back-to-back: %.0f cycles = %.1f per extra hit\n", one / 100.0, N - 1, burst / 100.0, (burst / 100.0 - one / 100.0) / (N - 2)); return 0; }
