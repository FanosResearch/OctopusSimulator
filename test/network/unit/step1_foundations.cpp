// Step 1 tests: packets/flits, the classifier, the topology (docs/NoC_Test_Plan.md §4).
#include "testing.h"
#include "Classifier.h"
#include "Topology.h"
#include <map>
#include <set>
using namespace octopus;
using namespace octopus::net;

static Message mk(uint8_t kind, bool data, int from = 0, int owner = 0)
{
    Message m(1, 0x1000, 0, 0, owner); m.kind = kind; m.from = from;
    if (data) { uint8_t buf[64] = {0}; m.copy(buf, 64); }
    return m;
}
static std::string errOf(std::function<void()> f)
{
    try { f(); } catch (ConfigError &e) { return e.what(); }
    return "";
}

// ---------------------------------------------------------------- classifier
TEST("S1-CLS-3")
{
    Classifier c(3, {10});
    struct Row { uint8_t kind; bool data; int from; int owner; int dst; int want; };
    std::vector<Row> rows = {
        {Message::K_GETS, false, 0, 0, 10, CLASS_REQUEST}, {Message::K_GETM, false, 1, 1, 10, CLASS_REQUEST},
        {Message::K_PUTS, false, 2, 2, 10, CLASS_REQUEST}, {Message::K_PUTM, true, 3, 3, 10, CLASS_REQUEST},
        {Message::K_DEMAND, false, 0, 0, 10, CLASS_REQUEST},
        {Message::K_FWD, false, 10, 0, 2, CLASS_FORWARD}, {Message::K_INV, false, 10, 10, 1, CLASS_FORWARD},
        {Message::K_RESP, true, 10, 0, 0, CLASS_RESPONSE}, {Message::K_SUPPLY, true, 1, 0, 0, CLASS_RESPONSE},
        {Message::K_SUPPLY_DEFERRED, true, 1, 0, 0, CLASS_RESPONSE}, {Message::K_FILL, true, 100, 0, 10, CLASS_RESPONSE},
        {Message::K_FILL_ROLLBACK, true, 100, 0, 10, CLASS_RESPONSE}, {Message::K_ACK, false, 2, 0, 0, CLASS_RESPONSE},
        {Message::K_WB_DATA, true, 1, 1, 10, CLASS_RESPONSE}, {Message::K_MEM_READ, false, 10, 0, 100, CLASS_RESPONSE},
        {Message::K_MEM_WRITE, true, 10, 10, 100, CLASS_RESPONSE}, {Message::K_EVICT, true, 10, 10, 100, CLASS_RESPONSE},
        // fallbacks: untagged
        {Message::K_UNKNOWN, true, 1, 0, 0, CLASS_RESPONSE},        // data -> response
        {Message::K_UNKNOWN, false, 0, 0, 10, CLASS_REQUEST},       // to a home -> request
        {Message::K_UNKNOWN, false, 10, 10, 1, CLASS_FORWARD},      // from a home -> forward
    };
    CHECK_EQ((int)rows.size(), 20);
    for (size_t i = 0; i < rows.size(); i++)
    {
        Message m = mk(rows[i].kind, rows[i].data, rows[i].from, rows[i].owner);
        CHECK_MSG(c.classify(m, rows[i].dst) == rows[i].want, "row " + octest::str(i) + " class " + octest::str(c.classify(m, rows[i].dst)) + " expected " + octest::str(rows[i].want));
    }
    CHECK_EQ((int)c.fallbackCount(), 3);
}
TEST("S1-CLS-2")
{
    Classifier c(2, {10});
    std::vector<uint8_t> kinds = {Message::K_GETS, Message::K_PUTM, Message::K_FWD, Message::K_INV, Message::K_RESP, Message::K_ACK, Message::K_FILL, Message::K_UNKNOWN};
    for (uint8_t k : kinds)
    {
        CHECK_EQ(c.classify(mk(k, false, 10, 0), 0), 0);
        CHECK_EQ(c.classify(mk(k, true, 10, 0), 0), 1);
    }
    CHECK_EQ((int)c.fallbackCount(), 0);
}
TEST("S1-CLS-FATAL")
{
    // classify() returns -1 for the unclassifiable case; classifyOrFatal() exits with status 2
    // on it (the exit path is exercised by the shell test S4-FATAL-CLASS).
    Classifier c(3, {10});
    Message m = mk(Message::K_UNKNOWN, false, 1, 1);
    CHECK_EQ(c.classify(m, 2), -1);
    CHECK_EQ((int)c.fallbackCount(), 0);
    CHECK(Classifier::describe(m, 2).find("kind 0") != std::string::npos);
    CHECK_EQ(errOf([] { Classifier bad(4, {10}); }).empty(), false);
}
TEST("S1-CLS-LOG")
{
    Classifier c(3, {10});
    for (int i = 0; i < 10; i++) c.classify(mk(Message::K_UNKNOWN, false, 0, 0), 10);
    CHECK_EQ((int)c.fallbackCount(), 10);
}

// ---------------------------------------------------------------- flits
TEST("S1-FLITS")
{
    Message req = mk(Message::K_GETS, false), d64 = mk(Message::K_RESP, true);
    Message d72 = mk(Message::K_RESP, true); { uint8_t b[72] = {0}; d72.copy(b, 72); }
    CHECK_EQ(flitCount(req, 16, 8), 1); CHECK_EQ(flitCount(d64, 16, 8), 5); CHECK_EQ(flitCount(d72, 16, 8), 5);
    CHECK_EQ(flitCount(req, 8, 8), 1);  CHECK_EQ(flitCount(d64, 8, 8), 9);  CHECK_EQ(flitCount(d72, 8, 8), 10);
    CHECK_EQ(flitCount(req, 0, 8), 1);  CHECK_EQ(flitCount(d64, 0, 8), 1);  CHECK_EQ(flitCount(d72, 0, 8), 1);
}

// ---------------------------------------------------------------- topology
static std::map<int, int> nm(std::initializer_list<std::pair<const int, int>> l) { return std::map<int, int>(l); }

TEST("S1-GRID-3x3")
{
    Topology t = Topology::grid2d(3, 3, nm({{0, 0}, {1, 4}, {2, 8}}), 0);
    CHECK_EQ(t.routers(), 9);
    // centre: east(5) west(3) north(1) south(7) local
    CHECK_EQ(t.linkPorts(4), 4); CHECK_EQ(t.ports(4), 5);
    CHECK_EQ(t.link(4, 0).router, 5); CHECK_EQ(t.link(4, 1).router, 3); CHECK_EQ(t.link(4, 2).router, 1); CHECK_EQ(t.link(4, 3).router, 7);
    CHECK_EQ(t.portName(4, 0), std::string("east")); CHECK_EQ(t.portName(4, 3), std::string("south")); CHECK_EQ(t.portName(4, 4), std::string("local0"));
    // corner 0: east(1) south(3) local ; corner 8: west(7) north(5) local
    CHECK_EQ(t.linkPorts(0), 2); CHECK_EQ(t.link(0, 0).router, 1); CHECK_EQ(t.link(0, 1).router, 3); CHECK_EQ(t.ports(0), 3);
    CHECK_EQ(t.linkPorts(8), 2); CHECK_EQ(t.link(8, 0).router, 7); CHECK_EQ(t.link(8, 1).router, 5);
    // edge 1: east(2) west(0) south(4), no local port (no endpoint)
    CHECK_EQ(t.linkPorts(1), 3); CHECK_EQ(t.ports(1), 3);
    CHECK_EQ(t.link(1, 2).router, 4);
    // far-end ports point back
    for (int r = 0; r < 9; r++) for (int p = 0; p < t.linkPorts(r); p++)
    {
        auto f = t.link(r, p); CHECK_EQ(t.link(f.router, f.port).router, r); CHECK_EQ(t.link(f.router, f.port).port, p);
    }
}
TEST("S1-GRID-COORD")
{
    Topology t = Topology::grid2d(4, 4, {}, 0);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++)
    { CHECK_EQ(t.coord(y * 4 + x).first, x); CHECK_EQ(t.coord(y * 4 + x).second, y); }
    CHECK_EQ(t.dimX(), 4); CHECK_EQ(t.dimY(), 4);
}
TEST("S1-TORUS")
{
    Topology t = Topology::grid2d(3, 3, {}, 0, true);
    for (int r = 0; r < 9; r++) CHECK_EQ(t.linkPorts(r), 4);
    // router 0: east 1, west(wrap) 2, north(wrap) 6, south 3; both directions of a wrap exist
    CHECK_EQ(t.link(0, 1).router, 2); CHECK_EQ(t.portName(0, 1), std::string("west(wrap)"));
    CHECK_EQ(t.link(2, 0).router, 0); CHECK_EQ(t.portName(2, 0), std::string("east(wrap)"));
    CHECK_EQ(t.link(0, 2).router, 6); CHECK_EQ(t.link(6, 3).router, 0);
    CHECK_EQ(t.kind() == Topology::Kind::Torus2D, true);
    CHECK_EQ(errOf([] { Topology::grid2d(2, 2, {}, 0, true); }).empty(), false);
}
TEST("S1-RING")
{
    Topology t = Topology::ring(5, nm({{0, 0}, {1, 2}}), 0);
    CHECK_EQ(t.routers(), 5);
    CHECK_EQ(t.link(0, 0).router, 1); CHECK_EQ(t.portName(0, 0), std::string("clockwise"));
    CHECK_EQ(t.link(0, 1).router, 4); CHECK_EQ(t.portName(0, 1), std::string("counter-clockwise"));
    CHECK_EQ(t.link(4, 0).router, 0); CHECK_EQ(t.link(0, 1).port, 0); CHECK_EQ(t.link(0, 0).port, 1);
    CHECK_EQ(t.ports(0), 3); CHECK_EQ(t.ports(1), 2);
}
TEST("S1-STAR")
{
    Topology t = Topology::star({0, 1, 2, 3, 10}, 0);
    CHECK_EQ(t.routers(), 1); CHECK_EQ(t.linkPorts(0), 0); CHECK_EQ(t.ports(0), 5);
    CHECK_EQ(t.routerOf(10), 0); CHECK_EQ(t.localPort(10), 4); CHECK_EQ(t.localPort(0), 0);
    CHECK_EQ((int)t.endpointsAtPort(0, 2).size(), 1); CHECK_EQ(t.endpointsAtPort(0, 2)[0], 2);
}
TEST("S1-CUSTOM-MESH")
{
    // the shipped Mesh.csv: complete graph on 0,1,2,3,10; every node an endpoint -> an implicit
    // router each, four link ports each, the matrix adjacency reproduced in row order
    std::vector<std::vector<int>> m = {{0, 1, 2, 3, 10}, {1, 0, 2, 3, 10}, {2, 0, 1, 3, 10}, {3, 0, 1, 2, 10}, {10, 0, 1, 2, 3}};
    Topology t = Topology::custom(m, {0, 1, 2, 3, 10}, {}, 0);
    CHECK_EQ(t.routers(), 5);
    for (int r = 0; r < 5; r++) { CHECK_EQ(t.linkPorts(r), 4); CHECK_EQ(t.ports(r), 5); CHECK_EQ((int)t.endpointsAt(r).size(), 1); }
    int r10 = t.routerIndex(10); CHECK_EQ(r10, 4); CHECK_EQ(t.routerOf(10), 4); CHECK_EQ(t.localPort(10), 4);
    for (int p = 0; p < 4; p++) CHECK_EQ(t.routerId(t.link(r10, p).router), p);          // 10's row: 0,1,2,3 in order
    CHECK_EQ(t.routerId(t.link(0, 3).router), 10);                                       // 0's row: 1,2,3,10
    for (int r = 0; r < 5; r++) for (int p = 0; p < t.linkPorts(r); p++)
    { auto f = t.link(r, p); CHECK_EQ(t.link(f.router, f.port).router, r); }
}
TEST("S1-CUSTOM-NOC")
{
    // the shipped NoC.csv: endpoints 0..3 link to switch 200 and to 10; 10 links to 0..3
    std::vector<std::vector<int>> m = {{0, 200, 10}, {1, 200, 10}, {2, 200, 10}, {3, 200, 10}, {10, 0, 1, 2, 3}, {200, 0, 1, 2, 3}};
    Topology t = Topology::custom(m, {0, 1, 2, 3, 10}, {}, 0);
    CHECK_EQ(t.routers(), 6);
    int r200 = t.routerIndex(200); CHECK(r200 >= 0);
    CHECK_EQ(t.linkPorts(r200), 4); CHECK_EQ(t.ports(r200), 4);                 // a router, no endpoint
    CHECK_EQ((int)t.endpointsAt(r200).size(), 0); CHECK_EQ(t.isEndpoint(200), false);
    CHECK_EQ(t.linkPorts(t.routerIndex(0)), 2); CHECK_EQ(t.ports(t.routerIndex(0)), 3);
    CHECK_EQ(t.routerId(t.link(t.routerIndex(0), 0).router), 200); CHECK_EQ(t.routerId(t.link(t.routerIndex(0), 1).router), 10);
    CHECK_EQ(t.linkPorts(t.routerIndex(10)), 4);
    // router_ids restricting: naming 200 is fine; naming nothing but having 200 is fine too
    Topology t2 = Topology::custom(m, {0, 1, 2, 3, 10}, {200}, 0); CHECK_EQ(t2.routers(), 6);
}
TEST("S1-CUSTOM-ASYM")
{
    std::vector<std::vector<int>> m = {{0, 1}, {1}};
    std::string e = errOf([&] { Topology::custom(m, {0, 1}, {}, 0); });
    CHECK(e.find("not symmetric") != std::string::npos);
    CHECK(e.find("0 lists 1") != std::string::npos);
}
TEST("S1-ATTACH")
{
    Topology t = Topology::grid2d(4, 4, nm({{0, 0}, {1, 3}, {2, 12}, {3, 15}, {10, 15}}), 0);
    for (int e : {0, 1, 2, 3, 10}) { CHECK(t.routerOf(e) >= 0); CHECK(t.localPort(e) >= t.linkPorts(t.routerOf(e))); }
    CHECK_EQ(t.routerOf(10), 15); CHECK_EQ((int)t.endpointsAt(15).size(), 2);
    CHECK_EQ(t.localPorts(15), 2); CHECK_EQ(t.localPort(3) != t.localPort(10), true);
    CHECK_EQ(t.endpointsAtPort(15, t.localPort(10))[0], 10);
    CHECK_EQ((int)t.endpoints().size(), 5);
}
TEST("S1-ATTACH-MISSING")
{
    std::string e = errOf([] { Topology::grid2d(2, 2, nm({{0, 0}, {1, 7}}), 0); });
    CHECK(e.find("node_map[1]") != std::string::npos);
}
TEST("S1-ATTACH-SHARED")
{
    Topology t = Topology::grid2d(2, 2, nm({{3, 3}, {10, 3}}), 1);
    CHECK_EQ(t.localPorts(3), 1); CHECK_EQ(t.localPort(3), t.linkPorts(3)); CHECK_EQ(t.localPort(10), t.linkPorts(3));
    auto eps = t.endpointsAtPort(3, t.linkPorts(3)); CHECK_EQ((int)eps.size(), 2); CHECK_EQ(eps[0], 3); CHECK_EQ(eps[1], 10);
}
TEST("S1-KEYS")
{
    // the constraints the topology layer owns (spec §4.1, §4.2); the Network object checks the rest
    CHECK(!errOf([] { Topology::grid2d(0, 3, {}, 0); }).empty());
    CHECK(!errOf([] { Topology::ring(2, {}, 0); }).empty());
    CHECK(!errOf([] { Topology::grid2d(2, 2, nm({{0, 0}}), -1); }).empty());
    CHECK(!errOf([] { Topology::custom({{0, 1}, {1, 0}, {0, 1}}, {0, 1}, {}, 0); }).empty());   // node twice
    CHECK(!errOf([] { Topology::custom({{0, 1}, {1, 0}}, {0, 1, 2}, {}, 0); }).empty());         // endpoint without a row
    CHECK(!errOf([] { Topology::custom({{0, 5}}, {0}, {}, 0); }).empty());                        // neighbour without a row
    CHECK(!errOf([] { Classifier c(1, {}); }).empty());
}
