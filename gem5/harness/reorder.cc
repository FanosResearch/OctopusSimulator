// Same-line ordering under a busy L1 array: core A queues hits to Y1..Y4 then X
// (all resident, S); while X's hit waits for the array, core B stores to X.
// Hardware: A's load to X was ordered first and hits; the snoop waits, then
// invalidates. Reorder: the snoop invalidates first and A's load becomes a miss.
#include "CacheSim.h"
#include "ExternalCPU.h"
#include <cstdio>
#include <map>
using namespace octopus;
static uint64_t completions = 0, invs = 0, now_cycle = 0;
struct Sink {
    std::map<uint64_t, uint64_t> done_at, inv_at;   // addr -> cycle (Octopus cycles), per core
    void done(uint64_t, uint64_t addr, uint64_t cyc, RequestType, uint8_t*) { completions++; done_at[addr] = cyc; }
    void commit(uint64_t, uint64_t) {}
    void inv(uint64_t addr) { invs++; inv_at[addr] = now_cycle; }
};
static ExternalCPU *attach(int id, Sink &s) { ExternalCPU *c = ExternalCPU::getExtCPUs()->at(id);
    c->registerCPUCallback(new CPUCallback<Sink, uint64_t, uint64_t, uint64_t, RequestType, uint8_t*>(&s, &Sink::done));
    c->registerCommitCallback(new CPUCallback<Sink, uint64_t, uint64_t>(&s, &Sink::commit));
    c->registerInvalidateCallback(new CPUCallback<Sink, uint64_t>(&s, &Sink::inv)); return c; }
static bool drain(CacheSim &sim, uint64_t want) { int steps = 0; while (completions < want && steps < 400000) { sim.step(); now_cycle = sim.now() / 100; steps++; } return completions >= want; }
int main(int argc, char **argv) {
    std::vector<std::string> cl = { std::string("workload_path(s)=") + argv[1] }; for (int i = 2; i < argc; i++) cl.push_back(argv[i]);
    CacheSim sim("MultiCoreSystem", cl, false, "MultiCoreSystem_gem5"); Sink sa, sb; ExternalCPU *a = attach(4, sa), *b = attach(5, sb);
    for (int i = 0; i < 4; i++) sim.step();
    const uint64_t X = 0x300000, Y = 0x310000;
    // warm-up: A and B both read X (S/S); A reads Y1..Y4 (S)
    a->addRequest(X, READ, NULL, 8); drain(sim, completions + 1);
    b->addRequest(X, READ, NULL, 8); drain(sim, completions + 1);
    for (int i = 0; i < 4; i++) { a->addRequest(Y + i * 64, READ, NULL, 8); drain(sim, completions + 1); }
    // the race: A's load to Y closes the array; A's load to X arrives next
    // cycle and is deferred behind it; three cycles later, with X's load
    // already inside the L1, B's store to X reaches the bus.
    uint64_t t0 = sim.now() / 100;
    a->addRequest(Y, READ, NULL, 8);
    sim.step(); now_cycle = sim.now() / 100;
    a->addRequest(X, READ, NULL, 8);
    for (int i = 0; i < 3; i++) { sim.step(); now_cycle = sim.now() / 100; }
    b->addRequest(X, WRITE, NULL, 8);
    bool ok = drain(sim, completions + 3);
    long long lx = (long long)sa.done_at[X] - (long long)t0, ly = (long long)sa.done_at[Y] - (long long)t0;
    long long li = sa.inv_at.count(X) ? (long long)sa.inv_at[X] - (long long)t0 : -1;
    long long lb = (long long)sb.done_at[X] - (long long)t0;
    bool missed = sa.done_at[X] > sb.done_at[X];   // a load that lost its line re-runs as a GetS and completes only after B's store
    printf("%s A: Y hit done +%lld, X load done +%lld, X invalidated +%lld | B: store X done +%lld  -> A's load %s\n",
           ok ? "ok  " : "HUNG", ly, lx, li, lb, missed ? "MISSED: snoop overtook the deferred hit" : "hit, ordered before the snoop");
    return 0; }
