// TileLink on the network (docs/TileLink_Plan.md s5.3 I-C, s6 TL-NET-7): the 5- and 7-class
// classifier keyed on the channel, the class priority, the per-channel admission of the network
// interface, and the protocol-and-network check over TileLink annotations (a synthetic handler).
#include "testing.h"
#include "Network.h"
#include "CoherenceProtocolHandler.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
using namespace octopus;
using namespace octopus::net;

static Parameter Pi(const std::string &n, int v) { return Parameter(n, Parameter::Integer, v); }
static Parameter Ps(const std::string &n, const std::string &v) { return Parameter(n, Parameter::String, v); }
static Parameter Pvi(const std::string &n, std::vector<int> v) { return Parameter(n, Parameter::Vector_Integer, v); }
static std::string errOf(std::function<void()> f)
{
    try { f(); } catch (ConfigError &e) { return e.what(); }
    return "";
}
// a TileLink message on channel `ch` from `from`
static Message tl(int ch, int from, bool data = false)
{
    Message m(1, 0x1000, 0, 0, (uint16_t)from); m.from = (uint16_t)from; m.tl.channel = (uint8_t)ch;
    if (data) { uint8_t b[64] = {0}; m.copy(b, 64); }
    return m;
}
// a star: L1s 0 and 1, the LLC 2 (home), the LLC's memory port 110 and the memory controller 100
static Configurable::ParametersMap starMap(int classes, bool memory)
{
    Configurable::ParametersMap m;
    m.insert({"topology", Ps("topology", "star")});
    m.insert({"component_ids", Pvi("component_ids", {0, 1, 2, 100, 110})});
    m.insert({"home_ids", Pvi("home_ids", {2})});
    m.insert({"num_classes", Pi("num_classes", classes)});
    m.insert({"inject_queue_depth", Pi("inject_queue_depth", 4)});
    if (memory) m.insert({"memory_ids", Pvi("memory_ids", {100, 110})});
    return m;
}

// ---------------------------------------------------------------- classifier
TEST("TL-CLS-5")
{
    Classifier c(5, {2});
    for (int ch = 1; ch <= 5; ch++) { CHECK_EQ(c.classify(tl(ch, 0), 2), ch - 1); CHECK_EQ(c.classify(tl(ch, 2, true), 0), ch - 1); }
    CHECK_EQ(c.className(0), std::string("A")); CHECK_EQ(c.className(4), std::string("E"));
    // an Octopus message on a TileLink network: no class, no fallback
    Message g(1, 0, 0, 0, 0); g.kind = Message::K_GETS;
    CHECK_EQ(c.classify(g, 2), -1);
    CHECK(c.why(g, 2).find("without a TileLink channel") != std::string::npos);
    CHECK_EQ((int)c.fallbackCount(), 0);
    // channel out of range
    CHECK_EQ(c.classify(tl(6, 0), 2), -1);
    // 5 classes have no outer link
    std::string w; CHECK_EQ(c.tlClass(1, true, &w), -1); CHECK(w.find("num_classes 7") != std::string::npos);
}
TEST("TL-CLS-7")
{
    Classifier c(7, {2});
    c.setMemory({100, 110});
    // inner link: A_in=0, B=1, C=2, D_in=5, E=6
    const int inner[] = {0, 1, 2, 5, 6};
    for (int ch = 1; ch <= 5; ch++) CHECK_EQ(c.classify(tl(ch, 0), 2), inner[ch - 1]);
    CHECK_EQ(c.classify(tl(2, 2), 0), 1);       // a probe, LLC -> L1
    CHECK_EQ(c.classify(tl(4, 2, true), 1), 5); // GrantData, LLC -> L1
    // outer link (both ends memory-side): A_out=3, D_out=4
    CHECK_EQ(c.classify(tl(1, 110), 100), 3);
    CHECK_EQ(c.classify(tl(1, 110, true), 100), 3);   // PutFullData (write-back)
    CHECK_EQ(c.classify(tl(4, 100, true), 110), 4);   // AccessAckData
    // B, C, E have no outer class
    CHECK_EQ(c.classify(tl(2, 110), 100), -1); CHECK(c.why(tl(2, 110), 100).find("A and D only") != std::string::npos);
    CHECK_EQ(c.classify(tl(5, 100), 110), -1);
    // between a memory-side endpoint and an inner one: refused
    CHECK_EQ(c.classify(tl(1, 0), 100), -1); CHECK(c.why(tl(1, 0), 100).find("memory-side endpoint and an inner one") != std::string::npos);
    CHECK_EQ(c.classify(tl(4, 100), 2), -1);
    const char *names[] = {"A_in", "B", "C", "A_out", "D_out", "D_in", "E"};
    for (int k = 0; k < 7; k++) CHECK_EQ(c.className(k), std::string(names[k]));
}
TEST("TL-CLS-3-REFUSED")
{
    // a TileLink message on a 3- or 2-class network: no class (fatal through classifyOrFatal)
    for (int n : {2, 3})
    {
        Classifier c(n, {2});
        CHECK_EQ(c.classify(tl(1, 0), 2), -1);
        CHECK(c.why(tl(1, 0), 2).find("TileLink message on a " + std::to_string(n) + "-class network") != std::string::npos);
        CHECK(Classifier::describe(tl(3, 0), 2).find("tl channel C") != std::string::npos);
    }
    // the Octopus kinds keep their classes (the description of a non-TileLink message is unchanged)
    Classifier c3(3, {2});
    Message g(1, 0x40, 0, 0, 0); g.kind = Message::K_GETS;
    CHECK_EQ(c3.classify(g, 2), CLASS_REQUEST);
    CHECK(Classifier::describe(g, 2).find("tl") == std::string::npos);
    CHECK_EQ(c3.why(Message(), 1), std::string("unclassifiable message"));
}
TEST("TL-NUM-CLASSES")
{
    for (int n : {2, 3, 5, 7}) CHECK_EQ(errOf([n] { Classifier c(n, {}); }), std::string(""));
    for (int n : {0, 1, 4, 6, 8}) CHECK(!errOf([n] { Classifier c(n, {}); }).empty());
    CHECK(errOf([] { Classifier c(4, {}); }).find("2, 3, 5 or 7") != std::string::npos);
}
TEST("TL-PRIORITY")
{
    NetConfig c;
    c.num_classes = 2; CHECK(c.classPriority() == std::vector<int>({1, 0}));
    c.num_classes = 3; CHECK(c.classPriority() == std::vector<int>({2, 1, 0}));
    c.num_classes = 5; CHECK(c.classPriority() == std::vector<int>({4, 3, 2, 1, 0}));
    c.num_classes = 7; CHECK(c.classPriority() == std::vector<int>({6, 5, 4, 3, 2, 1, 0}));
}

// ---------------------------------------------------------------- network interface
TEST("TL-NI-CHANNEL")
{
    Network net(starMap(7, true), "test");
    CHECK(net.isMemoryEndpoint(100)); CHECK(net.isMemoryEndpoint(110)); CHECK(!net.isMemoryEndpoint(2));
    NetworkInterface *l1 = static_cast<NetworkInterface *>(net.getInterfaceFor(0));
    NetworkInterface *port = static_cast<NetworkInterface *>(net.getInterfaceFor(110));
    NetworkInterface *mc = static_cast<NetworkInterface *>(net.getInterfaceFor(100));
    // the class of a channel on each side
    CHECK_EQ(l1->txClassCh(1), 0); CHECK_EQ(l1->txClassCh(3), 2); CHECK_EQ(l1->txClassCh(5), 6);
    CHECK_EQ(port->txClassCh(1), 3); CHECK_EQ(mc->txClassCh(4), 4);
    // reservations per class: taken from that class only, balanced on release
    CHECK_EQ(l1->txFreeSlotsCh(1), 4);
    l1->txReserveCh(1, 3);
    CHECK_EQ(l1->txFreeSlotsCh(1), 1); CHECK_EQ(l1->txFreeSlotsCh(3), 4); CHECK_EQ(l1->txReserved(0), 3);
    port->txReserveCh(1, 2);
    CHECK_EQ(port->txReserved(3), 2); CHECK_EQ(port->txReserved(0), 0); CHECK_EQ(port->txFreeSlotsCh(1), 2);
    l1->txReleaseCh(1, 3); port->txReleaseCh(1, 2);
    CHECK_EQ(l1->txReserved(0), 0); CHECK_EQ(port->txReserved(3), 0);
    CHECK_EQ((int)l1->txReservedTotal(0), 3); CHECK_EQ((int)l1->txReleasedTotal(0), 3);
    // injection: the class follows the channel; the inject queue of that class fills
    Message a = tl(1, 0); a.to.push_back(2);
    CHECK(l1->pushMessage(a)); CHECK_EQ((int)l1->injectQueue(0).size(), 1);
    Message w = tl(1, 110, true); w.to.push_back(100);
    CHECK(port->pushMessage(w)); CHECK_EQ((int)port->injectQueue(3).size(), 1);
    // an RX push takes the class of the message's channel on this endpoint's side
    Message d = tl(4, 2, true); CHECK(l1->pushMessage2RX(d)); CHECK_EQ(l1->rxSize(5), 1);
    Message dd = tl(4, 100, true); CHECK(port->pushMessage2RX(dd)); CHECK_EQ(port->rxSize(4), 1);
}
TEST("TL-NI-RX-ORDER")
{
    // priority serving on 7 classes: the highest class first (E, D_in, D_out, A_out, C, B, A_in)
    Network net(starMap(7, true), "test");
    NetworkInterface *ni = static_cast<NetworkInterface *>(net.getInterfaceFor(0));
    for (int c = 0; c < 7; c++) { ni->reserve(c); Message m = tl(1, 2); m.msg_id = 10 + c; ni->deliver(c, m); }
    std::vector<int> order; Message m;
    while (ni->peekMessage(&m)) { order.push_back((int)m.msg_id - 10); ni->popFrontMessage(); }
    CHECK(order == std::vector<int>({6, 5, 4, 3, 2, 1, 0}));
    for (int c = 0; c < 7; c++) CHECK_EQ(ni->reserved(c), 0);
}
TEST("TL-MEMORY-IDS")
{
    // memory_ids only on 7 classes, and only endpoints
    Network n7(starMap(7, false), "test");
    CHECK(!n7.isMemoryEndpoint(100));
    n7.setMemoryEndpoints({100, 110});
    CHECK(n7.isMemoryEndpoint(100));
    // the 5-class network classifies every TileLink message on the inner classes
    Network n5(starMap(5, false), "test");
    NetworkInterface *ni = static_cast<NetworkInterface *>(n5.getInterfaceFor(110));
    CHECK_EQ(ni->txClassCh(1), 0); CHECK_EQ(ni->txClassCh(4), 3);
}

// ---------------------------------------------------------------- protocol-and-network check
namespace
{
    // A synthetic TileLink manager: AcquireBlock (A) probes (B); ProbeAck (C) grants (D); a
    // Replacement (the controller itself) writes back on the outer A; the outer AccessAckData (D)
    // grants; GrantAck (E) ends. mode: 0 = correct, 1 = class order broken (the grant emitted on A),
    // 2 = Octopus kinds instead of channels.
    std::string fsmFile()
    {
        auto p = std::filesystem::temp_directory_path() / "octopus_tl_check_fsm.csv";
        std::ofstream f(p);
        f << "EventNum,Event\n0,AcquireBlock\n1,ProbeAck\n2,Replacement\n3,AccessAckData\n4,GrantAck\n,\n"
          << "ActionNum,Action\n0,Stall\n1,SendProbe\n2,SendGrant\n3,WriteBack\n4,OuterGet\n,\n"
          << "StateNum,State\n0,N\n1,B_p\n,\n"
          << "State,stable,isDataValid,AcquireBlock,ProbeAck,Replacement,AccessAckData,GrantAck\n"
          << "N,1,1,OuterGet/N,Stall/,WriteBack/N,SendGrant/N,Stall/\n"
          << "B_p,0,1,SendProbe/,SendGrant/N,Stall/,Stall/,Stall/\n";
        return p.string();
    }
    struct TLManager : CoherenceProtocolHandler
    {
        int mode;
        TLManager(int mode) : CoherenceProtocolHandler(nullptr, fsmFile(), 2, 100), mode(mode) {}
        std::vector<ControllerAction> processRequest(Message &, DebugPrint *) override { return {}; }
        FRFCFS_State getRequestState(const Message &, FRFCFS_State s) override { return s; }
        bool hasClassAnnotation() const override { return true; }
        static MsgClass ch(int c, bool data, bool home, bool memory) { MsgClass m; m.tl_channel = c; m.data = data; m.from_home = home; m.memory = memory; return m; }
        bool consumedBy(const std::string &e, MsgClass &o) const override
        {
            o = MsgClass();
            if (e == "Replacement") return true;
            if (mode == 2) { o.kind = Message::K_GETS; return true; }
            if (e == "AcquireBlock") { o = ch(1, false, false, false); return true; }
            if (e == "ProbeAck") { o = ch(3, true, false, false); return true; }
            if (e == "AccessAckData") { o = ch(4, true, false, true); return true; }
            if (e == "GrantAck") { o = ch(5, false, false, false); return true; }
            return false;
        }
        bool emittedBy(const std::string &a, std::vector<MsgClass> &o) const override
        {
            o.clear();
            if (a == "Stall") return true;
            if (mode == 2) { MsgClass m; m.kind = Message::K_RESP; m.data = true; m.from_home = true; o.push_back(m); return true; }
            if (a == "SendProbe") { o.push_back(ch(2, false, true, false)); o.back().count = -1; return true; }
            if (a == "SendGrant") { o.push_back(ch(mode == 1 ? 1 : 4, true, true, false)); return true; }
            if (a == "WriteBack") { o.push_back(ch(1, true, true, true)); return true; }
            if (a == "OuterGet") { o.push_back(ch(1, false, true, true)); return true; }
            return false;
        }
    };
}
TEST("TL-CHECK-7")
{
    Network net(starMap(7, true), "test");
    TLManager good(0);
    Network::ProtocolVerdict v = net.analyzeProtocols({{true, 2, &good}}, true);
    CHECK_MSG(v.violations.empty(), v.line);
    CHECK_MSG(v.line.find("consumption order holds") != std::string::npos, v.line);
    // A_in -> A_out (the outer Get), C -> D_in, D_out -> D_in, and the memory controller A_out -> D_out
    CHECK(v.edges.count({0, 3})); CHECK(v.edges.count({2, 5})); CHECK(v.edges.count({4, 5})); CHECK(v.edges.count({3, 4}));
    CHECK_MSG(v.line.find("A_in->A_out") != std::string::npos && v.line.find("A_out->D_out") != std::string::npos, v.line);
    // the Replacement cell's write-back on A_out is listed
    CHECK_EQ((int)v.self_emitters.size(), 1);
    CHECK_EQ(v.self_line, std::string("self-consumed emitters: LLC (N, Replacement) emits A_out"));
}
TEST("TL-CHECK-ORDER-BROKEN")
{
    // the grant emitted on A after consuming C (C -> A_in goes down): refused, the cell named
    Network net(starMap(7, true), "test");
    TLManager bad(1);
    Network::ProtocolVerdict v = net.analyzeProtocols({{true, 2, &bad}}, true);
    CHECK_MSG(!v.violations.empty(), v.line);
    CHECK_EQ((int)v.violations.size(), 2);
    CHECK_MSG(v.violations[0] == "LLC cell (N, AccessAckData) consumes D_out and emits SendGrant in A_in", v.violations[0]);
    CHECK_MSG(v.violations[1] == "LLC cell (B_p, ProbeAck) consumes C and emits SendGrant in A_in", v.violations[1]);
    CHECK_MSG(v.line.find("violation(s)") != std::string::npos, v.line);
    // the same on 5 classes, memory not on the network (the outer cells are skipped)
    Network n5(starMap(5, false), "test");
    v = n5.analyzeProtocols({{true, 2, &bad}}, false);
    CHECK_MSG(!v.violations.empty() && v.violations[0].find("consumes C and emits SendGrant in A") != std::string::npos, v.line);
    TLManager good(0);
    v = n5.analyzeProtocols({{true, 2, &good}}, false);
    CHECK_MSG(v.violations.empty() && v.self_line.empty(), v.line + " | " + v.self_line);     // the write-back is memory traffic, off the network
}
TEST("TL-CHECK-MISMATCH")
{
    TLManager good(0), octo(2);
    // TileLink annotations on a 3-class network, Octopus ones on a TileLink network: refused
    Network n3(starMap(3, false), "test");
    CHECK(errOf([&] { n3.analyzeProtocols({{true, 2, &good}}, false); }).find("TileLink needs num_classes 5 or 7") != std::string::npos);
    Network n5(starMap(5, false), "test");
    CHECK(errOf([&] { n5.analyzeProtocols({{true, 2, &octo}}, false); }).find("annotates Octopus message kinds") != std::string::npos);
    // memory on the network needs 7 classes, and the memory-side endpoints
    CHECK(errOf([&] { n5.analyzeProtocols({{true, 2, &good}}, true); }).find("needs num_classes 7") != std::string::npos);
    Network n7(starMap(7, false), "test");
    CHECK(errOf([&] { n7.analyzeProtocols({{true, 2, &good}}, true); }).find("memory-side endpoints are not known") != std::string::npos);
}
