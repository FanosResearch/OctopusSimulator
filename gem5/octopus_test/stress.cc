// Multi-core contention stress: N cores issue random loads/stores over a few
// shared lines (spinlock-like), each core keeping up to K requests in flight.
// Reports a hang (no completion for a long time) or a protocol exit.
#include "CacheSim.h"
#include "ExternalCPU.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace octopus;
static uint64_t completions = 0;
struct Sink { uint64_t done_n = 0; void done(uint64_t, uint64_t, uint64_t, RequestType, uint8_t*) { completions++; done_n++; } void commit(uint64_t, uint64_t) {} void inv(uint64_t) {} };
int main(int argc, char **argv) {
    std::vector<std::string> cl = { std::string("workload_path(s)=") + argv[1] };
    int ncores = 4, nlines = 3, inflight = 4, total = 20000, seed = 1;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a.rfind("--cores=",0)==0) ncores = atoi(a.c_str()+8); else if (a.rfind("--lines=",0)==0) nlines = atoi(a.c_str()+8);
        else if (a.rfind("--inflight=",0)==0) inflight = atoi(a.c_str()+11); else if (a.rfind("--total=",0)==0) total = atoi(a.c_str()+8);
        else if (a.rfind("--seed=",0)==0) seed = atoi(a.c_str()+7); else cl.push_back(a);
    }
    srand(seed);
    CacheSim sim("MultiCoreSystem", cl, false, "MultiCoreSystem_gem5");
    std::vector<Sink> sinks(ncores); std::vector<ExternalCPU*> cpus; std::vector<uint64_t> issued(ncores, 0);
    for (int c = 0; c < ncores; c++) { ExternalCPU *e = ExternalCPU::getExtCPUs()->at(4 + c);
        e->registerCPUCallback(new CPUCallback<Sink, uint64_t, uint64_t, uint64_t, RequestType, uint8_t*>(&sinks[c], &Sink::done));
        e->registerCommitCallback(new CPUCallback<Sink, uint64_t, uint64_t>(&sinks[c], &Sink::commit));
        e->registerInvalidateCallback(new CPUCallback<Sink, uint64_t>(&sinks[c], &Sink::inv)); cpus.push_back(e); }
    for (int i = 0; i < 4; i++) sim.step();
    uint64_t last_completions = 0, idle_steps = 0, steps = 0;
    while (completions < (uint64_t)total) {
        for (int c = 0; c < ncores; c++) {
            uint64_t outstanding = issued[c] - sinks[c].done_n;
            if (outstanding < (uint64_t)inflight && issued[c] < (uint64_t)(total / ncores) + 1 && (rand() % 3) == 0) {
                uint64_t line = 0x400000 + (rand() % nlines) * 64 + (rand() % 8) * 8;
                cpus[c]->addRequest(line, (rand() % 2) ? WRITE : READ, NULL, 8); issued[c]++;
            }
        }
        sim.step(); steps++;
        if (completions == last_completions) { if (++idle_steps > 200000) { printf("HANG: %llu completions, no progress for 200000 steps (cycle %llu)\n", (unsigned long long)completions, (unsigned long long)(sim.now()/100)); return 2; } }
        else { idle_steps = 0; last_completions = completions; }
        uint64_t all_issued = 0; for (int c = 0; c < ncores; c++) all_issued += issued[c];
        if (all_issued >= (uint64_t)total && completions >= all_issued) break;
    }
    printf("PASS: %llu completions in %llu cycles\n", (unsigned long long)completions, (unsigned long long)(sim.now()/100)); return 0; }
