// Octopus_tl_tests: the TileLink adapter's unit tests (TLPort, TileLinkInterconnect, the admission
// contract, the Logger's TileLink columns; docs/TileLink_Plan.md s5.2, s5.3 I-A).
//   Octopus_tl_tests [prefix]          runs the tests whose id starts with prefix
//   Octopus_tl_tests --fatal-channel0  pushes an untagged message into a TLPort (must exit 2)
#include <cstdlib>
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
    if (argc > 1 && std::string(argv[1]) == "--fatal-corrupt")
    {
        // --fatal-corrupt [fatal|off|report] [beat]: a Get answered with an 8-beat AccessAckData (w = 8) whose
        // beat `beat` a fault marks corrupt on the wire (the crossbar's D toward master 0). TLPort refuses it when
        // the message reaches Octopus (status 2, named), whatever the monitor setting
        const std::string mon = argc > 2 ? argv[2] : "fatal";
        const unsigned beat = argc > 3 ? unsigned(std::atoi(argv[3])) : 0;
        tltest::Sys s({{"w", 8}}, {0}, mon == "off" ? 0 : 1, "C", mon == "report" ? "report" : "fatal");
        std::vector<uint8_t> b = tltest::pattern(1);
        tltest::send(s.port(0), tltest::get(1, 0x40, 0));
        s.run(10);
        Message req; s.port(10)->peekMessage(&req); s.port(10)->popFrontMessage();
        octopus::tl::Link *l = s.ic->kernel().link("L0");
        auto prev = l->chan(octopus::tl::Channel::D).onOffer;
        l->chan(octopus::tl::Channel::D).onOffer = [prev, beat](const octopus::tl::ChannelState &c, const octopus::tl::Beat &bt, uint64_t t) {
            if (prev) prev(c, bt, t);
            if (bt.index == beat && bt.msg->corrupt.size() > beat) bt.msg->corrupt[beat] = 1;
        };
        tltest::send(s.port(10), tltest::answer(req, Message::K_FILL, b));
        s.run(60);
        Message got;
        std::printf("the corrupt beat was delivered%s\n", s.port(0)->peekMessage(&got) ? " to the L1" : "");
        return 0;
    }
    // the trace is opened at the first record: name it before any component logs
    if (!std::getenv("OCTOPUS_TRACE")) setenv("OCTOPUS_TRACE", (dir + "/tl_unit_trace.bin").c_str(), 1);
    octopus::Logger::getLogger()->registerReportPath(dir);
    return octest::run(argc, argv);
}
