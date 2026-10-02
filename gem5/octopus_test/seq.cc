// Single core: two hits to different lines issued back to back. When does each complete?
#include "CacheSim.h"
#include "ExternalCPU.h"
#include <cstdio>
#include <map>
using namespace octopus;
static uint64_t completions = 0; static std::map<uint64_t, double> done_at;
struct Sink { void done(uint64_t, uint64_t addr, uint64_t cyc, RequestType, uint8_t*) { completions++; done_at[addr] = cyc; } void commit(uint64_t, uint64_t) {} void inv(uint64_t) {} };
int main(int argc, char **argv) {
    std::vector<std::string> cl = { std::string("workload_path(s)=") + argv[1] }; for (int i = 2; i < argc; i++) cl.push_back(argv[i]);
    CacheSim sim("MultiCoreSystem", cl, false, "MultiCoreSystem_gem5"); Sink s; ExternalCPU *a = ExternalCPU::getExtCPUs()->at(4);
    a->registerCPUCallback(new CPUCallback<Sink, uint64_t, uint64_t, uint64_t, RequestType, uint8_t*>(&s, &Sink::done));
    a->registerCommitCallback(new CPUCallback<Sink, uint64_t, uint64_t>(&s, &Sink::commit));
    a->registerInvalidateCallback(new CPUCallback<Sink, uint64_t>(&s, &Sink::inv));
    for (int i = 0; i < 4; i++) sim.step();
    const uint64_t X = 0x300000, Y = 0x310000;
    a->addRequest(X, READ, NULL, 8); while (completions < 1) sim.step();
    a->addRequest(Y, READ, NULL, 8); while (completions < 2) sim.step();
    printf("step granularity %llu ns, cycle %llu ns\n", (unsigned long long)sim.stepGranularity(), 100ULL);
    for (int gap = 0; gap <= 4; gap++) {
        double t0 = sim.now() / 100.0; uint64_t want = completions + 2;
        a->addRequest(Y, READ, NULL, 8);
        for (int i = 0; i < gap; i++) sim.step();
        double tx = sim.now() / 100.0;
        a->addRequest(X, READ, NULL, 8);
        while (completions < want) sim.step();
        printf("gap %d step(s): Y issued +0.0 done +%.1f;  X issued +%.1f done +%.1f  (X round trip %.1f cycles)\n", gap, done_at[Y]-t0, tx-t0, done_at[X]-t0, done_at[X]-tx);
        for (int i = 0; i < 40; i++) sim.step();
    }
    return 0; }
