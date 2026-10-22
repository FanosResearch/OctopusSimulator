// Octopus_tl_tests: the TileLink adapter's unit tests (TLPort, TileLinkInterconnect, the admission
// contract, the Logger's TileLink columns; docs/TileLink_Plan.md s5.2, s5.3 I-A).
//   Octopus_tl_tests [prefix]          runs the tests whose id starts with prefix
//   Octopus_tl_tests --fatal-channel0  pushes an untagged message into a TLPort (must exit 2)
#include "tl_common.h"
#include <sys/stat.h>

int main(int argc, char **argv)
{
    const char *out = std::getenv("TL_TEST_OUT");
    std::string dir = out ? out : ".";
    mkdir(dir.c_str(), 0755);
    if (argc > 1 && std::string(argv[1]) == "--fatal-channel0")
    {
        tltest::Sys s({});
        Message m(1, 0x40);
        m.to = {10};
        s.port(0)->pushMessage(m);
        std::printf("pushMessage returned\n");
        return 0;
    }
    // the trace is opened at the first record: name it before any component logs
    if (!std::getenv("OCTOPUS_TRACE")) setenv("OCTOPUS_TRACE", (dir + "/tl_unit_trace.bin").c_str(), 1);
    octopus::Logger::getLogger()->registerReportPath(dir);
    return octest::run(argc, argv);
}
