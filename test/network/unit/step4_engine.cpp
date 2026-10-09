// Step 4 unit tests on a Network built from a parameter map: RX serving order and reservations.
#include "testing.h"
#include "Network.h"
using namespace octopus;
using namespace octopus::net;

static Parameter Pi(const std::string &n, int v) { return Parameter(n, Parameter::Integer, v); }
static Parameter Ps(const std::string &n, const std::string &v) { return Parameter(n, Parameter::String, v); }
static Parameter Pvi(const std::string &n, std::vector<int> v) { return Parameter(n, Parameter::Vector_Integer, v); }
static Configurable::ParametersMap starMap(const std::string &rx, int eject_depth)
{
    Configurable::ParametersMap m;
    m.insert({"topology", Ps("topology", "star")});
    m.insert({"component_ids", Pvi("component_ids", {0, 1, 2})});
    m.insert({"rx_serving", Ps("rx_serving", rx)});
    m.insert({"eject_queue_depth", Pi("eject_queue_depth", eject_depth)});
    m.insert({"home_ids", Pvi("home_ids", {2})});
    return m;
}
static Message mk(uint64_t id, bool data) { Message m(id, 0x40 * id, 0, 0, 1); m.from = 1; if (data) { uint8_t b[64] = {0}; m.copy(b, 64); } return m; }

TEST("S4-RX-ORDER")
{
    // priority: response, forward, request
    Network net(starMap("priority", 8), "test");
    NetworkInterface *ni = static_cast<NetworkInterface *>(net.getInterfaceFor(0));
    for (int c = 0; c < 3; c++) for (int k = 0; k < 2; k++) { ni->reserve(c); ni->deliver(c, mk(10 * c + k, c == 2)); }
    std::vector<int> order; Message m;
    while (ni->peekMessage(&m)) { order.push_back((int)(m.msg_id / 10)); ni->popFrontMessage(); }
    std::vector<int> want = {2, 2, 1, 1, 0, 0};
    CHECK(order == want);
    // alternate: cyclic over the non-empty classes, starting at class 0
    Network net2(starMap("alternate", 8), "test");
    NetworkInterface *n2 = static_cast<NetworkInterface *>(net2.getInterfaceFor(0));
    for (int c = 0; c < 3; c++) for (int k = 0; k < 2; k++) { n2->reserve(c); n2->deliver(c, mk(10 * c + k, c == 2)); }
    order.clear();
    while (n2->peekMessage(&m)) { order.push_back((int)(m.msg_id / 10)); n2->popFrontMessage(); }
    want = {0, 1, 2, 0, 1, 2};
    CHECK(order == want);
    // alternate with only one class non-empty serves it every time
    n2->reserve(1); n2->deliver(1, mk(11, false)); n2->reserve(1); n2->deliver(1, mk(12, false));
    order.clear(); while (n2->peekMessage(&m)) { order.push_back((int)(m.msg_id / 10)); n2->popFrontMessage(); }
    want = {1, 1}; CHECK(order == want);
}
TEST("S4-RESERVE")
{
    Network net(starMap("priority", 2), "test");
    NetworkInterface *ni = static_cast<NetworkInterface *>(net.getInterfaceFor(0));
    CHECK_EQ(ni->rxFree(2), 2); CHECK(ni->canReserve(2)); CHECK(ni->canAcceptRX(MessageType::DATA_RESPONSE));
    ni->reserve(2); ni->reserve(2);
    CHECK_EQ(ni->rxFree(2), 0); CHECK(!ni->canReserve(2)); CHECK(!ni->canAcceptRX(MessageType::DATA_RESPONSE));
    CHECK(ni->canAcceptRX(MessageType::REQUEST));          // another class is unaffected
    ni->deliver(2, mk(1, true));
    CHECK_EQ(ni->rxFree(2), 0); CHECK_EQ(ni->rxSize(2), 1); CHECK_EQ(ni->reserved(2), 1);
    Message m; CHECK(ni->peekMessage(&m)); ni->popFrontMessage();
    CHECK_EQ(ni->rxFree(2), 1); CHECK(ni->canReserve(2));
    ni->deliver(2, mk(2, true));
    CHECK_EQ(ni->reserved(2), 0); CHECK_EQ(ni->rxSize(2), 1);
}
