#include "CacheSim.h"
#include "ExternalCPU.h"
#include <cstdio>
using namespace octopus;
static uint64_t completions = 0, commits = 0, invs = 0;
struct Sink {
    void done(uint64_t, uint64_t, uint64_t, RequestType, uint8_t*) { completions++; }
    void commit(uint64_t, uint64_t) { commits++; }
    void inv(uint64_t) { invs++; }
};
static ExternalCPU *attach(int id, Sink &s) {
    ExternalCPU *c = ExternalCPU::getExtCPUs()->at(id);
    c->registerCPUCallback(new CPUCallback<Sink, uint64_t, uint64_t, uint64_t, RequestType, uint8_t*>(&s, &Sink::done));
    c->registerCommitCallback(new CPUCallback<Sink, uint64_t, uint64_t>(&s, &Sink::commit));
    c->registerInvalidateCallback(new CPUCallback<Sink, uint64_t>(&s, &Sink::inv));
    return c;
}
static bool drain(CacheSim &sim, uint64_t want, int limit) {
    int steps = 0;
    while (completions < want && steps < limit) { sim.step(); steps++; }
    return completions >= want;
}
static void one(CacheSim &sim, ExternalCPU *c, uint64_t addr, RequestType t, const char *label) {
    uint64_t want = completions + 1, t0 = sim.now();
    c->addRequest(addr, t, NULL, 8);
    bool ok = drain(sim, want, 100000);
    printf("  %-26s %s %.1f cycles\n", label, ok ? "ok  " : "HUNG", (sim.now() - t0) / 100.0);
}
int main(int argc, char **argv) {
    std::vector<std::string> cl = { std::string("workload_path(s)=") + argv[1] };
    for (int i = 2; i < argc; i++) cl.push_back(argv[i]);
    CacheSim sim("MultiCoreSystem", cl, false, "MultiCoreSystem_gem5");
    Sink s; ExternalCPU *a = attach(4, s), *b = attach(5, s);
    for (int i = 0; i < 4; i++) sim.step();
    one(sim, a, 0x200000, READ,  "load miss (LLC)");
    one(sim, a, 0x200000, READ,  "load hit (S)");
    one(sim, a, 0x200000, WRITE, "store upgrade (S->M)");
    one(sim, a, 0x200000, WRITE, "store hit (M)");
    // burst: 8 stores to one line, then 8 loads to it while B takes the line
    for (int i = 0; i < 8; i++) a->addRequest(0x500000 + i * 8, WRITE, NULL, 8);
    bool ok = drain(sim, completions + 8, 200000);
    printf("  8 same-line stores burst   %s (commits %llu)\n", ok ? "ok  " : "HUNG", (unsigned long long)commits);
    for (int i = 0; i < 8; i++) a->addRequest(0x500000 + i * 8, READ, NULL, 8);
    b->addRequest(0x500000, WRITE, NULL, 8);
    ok = drain(sim, completions + 9, 200000);
    printf("  8 loads + remote store     %s (completions %llu commits %llu invalidations %llu)\n",
           ok ? "ok  " : "HUNG", (unsigned long long)completions, (unsigned long long)commits, (unsigned long long)invs);
    return 0;
}
