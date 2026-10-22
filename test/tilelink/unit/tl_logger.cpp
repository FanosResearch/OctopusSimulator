// TL-LOGGER-*: the LatencyReport under TileLink (docs/TileLink_Plan.md s5.3 I-A, "what the stages
// mean"): Req-Bus is the A message and Resp-Bus the D message although the B/C probe legs and the E
// GrantAck carry the request's id; the Probe column is first B EXIT -> last C EXIT; the Completion
// column is the Grant accepted -> its GrantAck accepted, written after the row's other columns (the
// GrantAck comes after the CPU has its data) without reordering the rows of a core.
#include "tl_common.h"
#include <fstream>
#include <sstream>
using namespace octopus;

namespace
{
    std::vector<std::string> split(const std::string &s)
    {
        std::vector<std::string> v; std::stringstream ss(s); std::string x;
        while (std::getline(ss, x, ',')) v.push_back(x);
        return v;
    }
}

TEST("TL-LOGGER-COLUMNS")
{
    Logger *lg = Logger::getLogger();
    lg->enableTileLinkColumns();
    const uint64_t core = 7;
    auto at = [&](uint64_t cyc, uint64_t id, Logger::Role r, uint32_t comp, Logger::Phase p) { lg->setClkCount(core, cyc); lg->event(id, r, comp, p); };
    using R = Logger::Role; using P = Logger::Phase;
    const uint32_t A = (6u << 8) | 1, B = (6u << 8) | 2, C = (4u << 8) | 3, GD = (5u << 8) | 4, E = 5;
    // request 1: a miss with a probe round trip; its GrantAck arrives after the next request's row
    Message r1(77001, 0x9000, 5); lg->setClkCount(core, 10); lg->addRequest(core, r1);
    at(10, 77001, R::CPU, 0, P::ENTER); at(11, 77001, R::L1, 0, P::ENTER);
    at(13, 77001, R::REQ_BUS, A, P::EXIT); at(14, 77001, R::LLC, 10, P::ENTER);
    at(15, 77001, R::REQ_BUS, B, P::EXIT); at(16, 77001, R::REQ_BUS, B, P::EXIT);
    at(18, 77001, R::RESP_BUS, C, P::EXIT); at(19, 77001, R::RESP_BUS, C, P::EXIT);
    at(20, 77001, R::LLC, 10, P::SERVICE); at(22, 77001, R::LLC, 10, P::EXIT);
    at(25, 77001, R::RESP_BUS, GD, P::EXIT); at(27, 77001, R::CPU, 0, P::EXIT);
    // request 2: an L1 hit, finalized while request 1 waits for its GrantAck
    Message r2(77002, 0x9040, 27); lg->setClkCount(core, 28); lg->addRequest(core, r2);
    at(28, 77002, R::CPU, 0, P::ENTER); at(28, 77002, R::L1, 0, P::ENTER); at(30, 77002, R::CPU, 0, P::EXIT);
    at(31, 77001, R::RESP_BUS, E, P::EXIT);                    // request 1's GrantAck: Completion 31 - 25
    // request 3: a Grant whose GrantAck never comes before the end (-1)
    Message r3(77003, 0x9080, 31); lg->setClkCount(core, 32); lg->addRequest(core, r3);
    at(32, 77003, R::CPU, 0, P::ENTER); at(33, 77003, R::L1, 0, P::ENTER); at(35, 77003, R::REQ_BUS, A, P::EXIT);
    at(36, 77003, R::LLC, 10, P::ENTER); at(37, 77003, R::LLC, 10, P::SERVICE); at(38, 77003, R::LLC, 10, P::EXIT);
    at(40, 77003, R::RESP_BUS, GD, P::EXIT); at(41, 77003, R::CPU, 0, P::EXIT);
    lg->traceEnd(core);

    std::ifstream f(lg->reportPath() + "/LatencyReport_C7.csv");
    CHECK_MSG(f.good(), "no LatencyReport_C7.csv in " + lg->reportPath());
    std::string line; std::getline(f, line);
    auto hdr = split(line);
    CHECK(hdr.size() >= 2);
    CHECK_EQ(hdr[hdr.size() - 2], std::string("Probe Latency")); CHECK_EQ(hdr.back(), std::string("Completion Latency"));
    auto col = [&](const std::string &name) { for (size_t i = 0; i < hdr.size(); i++) if (hdr[i] == name) return (int)i; return -1; };
    std::vector<std::vector<std::string>> rows;
    while (std::getline(f, line) && !line.empty()) rows.push_back(split(line));
    CHECK_EQ(rows.size(), (size_t)3);
    CHECK_EQ(rows[0][0], std::string("77001")); CHECK_EQ(rows[1][0], std::string("77002")); CHECK_EQ(rows[2][0], std::string("77003"));
    int rq = col("Requst Bus Latency"), rs = col("Response Bus Latency"), tot = col("Total Latency");
    CHECK(rq >= 0 && rs >= 0 && tot >= 0);
    CHECK_EQ(rows[0][rq], std::string("2"));                  // A: 13 - 11 (not a B)
    CHECK_EQ(rows[0][rs], std::string("3"));                  // D: 25 - 22 (not the C or the E)
    CHECK_EQ(rows[0][tot], std::string("17"));
    CHECK_EQ(rows[0][hdr.size() - 2], std::string("4"));     // Probe: first B 15 -> last C 19
    CHECK_EQ(rows[0].back(), std::string("6"));               // Completion: 25 -> 31
    CHECK_EQ(rows[1][hdr.size() - 2], std::string("0")); CHECK_EQ(rows[1].back(), std::string("0"));
    CHECK_EQ(rows[2].back(), std::string("-1"));
}
