// Regression: transposed summaries must not retain cores from a prior report window.
#include "CacheSim.h"
#include "ExternalCPU.h"
#include "Logger.h"
#include <cassert>
#include <fstream>
#include <iostream>

using namespace octopus;
struct Sink {
    unsigned completions = 0;
    void done(uint64_t, uint64_t, uint64_t, RequestType, uint8_t*) { ++completions; }
    void commit(uint64_t, uint64_t) {}
    void invalidate(uint64_t) {}
};

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string path = argv[1];
    // The four-argument constructor must select the gem5 preset, not an output directory.
    CacheSim sim("MultiCoreSystem", {"workload_path(s)=" + path, "cpu[*].log_requests(i)=1"}, false, "MultiCoreSystem_gem5");
    Sink sink;
    for (int id : {4, 5}) {
        ExternalCPU* cpu = ExternalCPU::getExtCPUs()->at(id);
        cpu->registerCPUCallback(new CPUCallback<Sink, uint64_t, uint64_t, uint64_t, RequestType, uint8_t*>(
            &sink, &Sink::done));
        cpu->registerCommitCallback(new CPUCallback<Sink, uint64_t, uint64_t>(&sink, &Sink::commit));
        cpu->registerInvalidateCallback(new CPUCallback<Sink, uint64_t>(&sink, &Sink::invalidate));
    }
    for (int i = 0; i < 4; ++i) sim.step();
    for (int id : {4, 5}) {
        Logger::getLogger()->resetReports();
        ExternalCPU::setLoggerEnable(true);
        const unsigned target = sink.completions + 1;
        ExternalCPU::getExtCPUs()->at(id)->addRequest(0x400000 + id * 64, READ, nullptr, 8);
        for (int i = 0; i < 100000 && sink.completions < target; ++i) sim.step();
        assert(sink.completions == target);
        ExternalCPU::writeLogReports();
        std::ifstream file(path + "/newLogger/Summary_transposed.csv");
        std::string header;
        std::getline(file, header);
        assert(header == "Metric,Core " + std::to_string(id));
    }
    std::cout << "PASS: gem5 constructor selects the preset; summary cores reset between report windows\n";
}
