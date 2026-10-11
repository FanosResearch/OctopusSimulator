// P2-ADMIT-RF (docs/NoC_Architecture_Spec.md s14.2, docs/NoC_Test_Plan.md s12): a controller whose
// request queue is full still consumes a forward and a response from its interfaces. The property
// the network's deadlock freedom rests on, checked on the shipped controllers with stub interfaces:
// the directory L1 and LLC (the grid preset) and the snoop L1 (the bus preset).
#include "testing.h"
#include "CacheControllerDirectory.h"
#include "CacheController.h"
#include "Configurable.h"
#include "CommunicationInterface.h"
#include "Logger.h"
#include <deque>
#include <vector>
using namespace octopus;

namespace
{
    // an interface the test feeds and reads: rx is what the controller peeks, tx what it pushed
    // one receive queue per class served response, forward, request, as the network interface does
    // (a request waiting outside a full processing queue must not hide the response behind it)
    struct Stub : CommunicationInterface
    {
        std::deque<Message> rx[3]; std::vector<Message> tx; int peeked = -1;
        Stub(int id) : CommunicationInterface(id) {}
        static int cls(const Message &m)
        {
            if (m.kind == Message::K_FWD || m.kind == Message::K_INV) return 1;
            if (m.data != NULL || m.kind == Message::K_ACK || m.kind == Message::K_SUPPLY || m.kind == Message::K_RESP || m.kind == Message::K_FILL) return 2;
            return 0;
        }
        void deliver(const Message &m) { rx[cls(m)].push_back(m); }
        size_t pending() const { return rx[0].size() + rx[1].size() + rx[2].size(); }
        bool peekMessage(Message *m) override
        {
            for (int c : {2, 1, 0}) if (!rx[c].empty()) { m->copy(rx[c].front()); peeked = c; return true; }
            peeked = -1; return false;
        }
        void popFrontMessage() override { if (peeked >= 0 && !rx[peeked].empty()) rx[peeked].pop_front(); }
        bool pushMessage(Message &m, uint64_t, MessageType) override { tx.push_back(m); return true; }
        bool pushMessage2RX(Message &m, MessageType) override { deliver(m); return true; }
        int count(uint8_t kind) const { int n = 0; for (auto &m : tx) if (m.kind == kind) n++; return n; }
        const Message *last(uint8_t kind) const { for (auto it = tx.rbegin(); it != tx.rend(); ++it) if (it->kind == kind) return &*it; return nullptr; }
    };
    struct Cfg : Configurable
    {
        Cfg(const std::string &name) : Configurable(std::vector<std::string>{}, std::string(CONFIGURATION_PATH) + std::string(SYSTEM_CONFIGURATIONS), name) {}
        using Configurable::getSubMap;
    };
    Configurable::ParametersMap withQueue(Configurable::ParametersMap m, int depth)
    {
        m.erase("processing_queue_size");
        m.insert({"processing_queue_size", Parameter("processing_queue_size", Parameter::Integer, depth)});
        return m;
    }
    Message msg(uint64_t id, uint64_t addr, uint64_t cv, int owner, int from, int to, uint8_t kind, bool data = false)
    {
        Message m(id, addr, 1, cv, (uint16_t)owner);
        m.from = (uint16_t)from; m.to.clear(); m.to.push_back((uint16_t)to); m.kind = kind;
        if (data) { uint8_t b[64] = {0}; m.copy(b, 64); }
        return m;
    }
    void cycles(BaseController *c, int n) { ClockedObj *o = c; for (int i = 0; i < n; i++) o->cycleProcess(); }   // the clock's entry point
    void prepare() { static bool once = false; if (!once) { Logger::getLogger()->registerReportPath("test/network/out"); once = true; } }
}

TEST("P2-ADMIT-RF-L1")
{
    prepare();
    Cfg cfg("MultiCoreSystem_Grid");
    Stub net(0), cpu(-1);                                         // upper = the network, lower = the CPU side
    CacheControllerDirectory *l1 = new CacheControllerDirectory(withQueue(cfg.getSubMap("cache_controller", 0), 2), &net, &cpu, "test");
    const uint64_t A = 0x7f0000001000ull;
    // four loads to one line: the first misses (GetS to the home), the rest stall and fill the
    // two-deep processing queue; the fourth is a demand and is not admitted
    for (uint64_t i = 1; i <= 4; i++) cpu.deliver(msg(i, A + 8 * i, 0, 0, -1, 0, Message::K_DEMAND));
    cycles(l1, 8);
    CHECK_EQ(net.count(Message::K_GETS), 1);
    CHECK_EQ((int)cpu.pending(), 1);                             // the queue is full: a demand waits outside
    // a forward (an invalidation from the home) is consumed while the queue is full, and answered
    net.deliver(msg(50, A, MSIDirectory::REQUEST_TYPE_INV, 0, 10, 0, Message::K_INV));
    cycles(l1, 3);
    CHECK_EQ((int)net.pending(), 0);
    CHECK_EQ((int)cpu.pending(), 1);
    CHECK(net.count(Message::K_ACK) >= 1);
    // a response (the home's data) is consumed while the queue is full
    net.deliver(msg(1, A + 8, 0, 0, 10, 0, Message::K_RESP, true));
    cycles(l1, 3);
    CHECK_EQ((int)net.pending(), 0);
}

TEST("P2-ADMIT-RF-LLC")
{
    prepare();
    Cfg cfg("MultiCoreSystem_Grid");
    Stub mem(100), net(10);                                       // upper = the memory bus, lower = the network
    CacheControllerDirectory *llc = new CacheControllerDirectory(withQueue(cfg.getSubMap("llc_controller"), 2), &mem, &net, "test");
    // the memory stub answers every read with a fill
    auto serveMemory = [&]() {
        for (auto &m : mem.tx) if (m.kind == Message::K_MEM_READ && !m.to.empty() && m.to[0] == 100)
        { Message f = msg(m.msg_id, m.addr, 0, m.owner, 100, 10, Message::K_FILL, true); mem.deliver(f); m.to.clear(); }
    };
    // three lines of one set (32 KiB, 2 ways, 64 B lines: 256 sets, 16 KiB apart) read by core 0:
    // the third fill evicts the first and invalidates its sharer, so the LLC waits for an InvAck
    const uint64_t C1 = 0x7f0000010000ull, C2 = C1 + 0x4000, C3 = C1 + 0x8000, D = 0x7f0000020000ull;
    int id = 100;
    for (uint64_t line : {C1, C2, C3})
    {
        net.deliver(msg(id++, line, MSIDirectory::REQUEST_TYPE_GETS, 0, 0, 10, Message::K_GETS));
        for (int k = 0; k < 6; k++) { cycles(llc, 4); serveMemory(); }
    }
    cycles(llc, 20);
    CHECK_EQ(net.count(Message::K_INV), 1);                      // the eviction's invalidation went out
    const Message *inv = net.last(Message::K_INV);
    // four GetS to a fourth line fill the two-deep queue (the first fetches, the next two stall,
    // the fourth waits outside); the memory does not answer, so the queue stays full
    for (int c = 0; c < 4; c++) net.deliver(msg(id++, D, MSIDirectory::REQUEST_TYPE_GETS, c, c, 10, Message::K_GETS));
    cycles(llc, 8);
    CHECK_EQ((int)net.pending(), 1);
    // the InvAck (response class) is consumed while the queue is full, and completes the eviction
    net.deliver(msg(inv->msg_id, inv->addr, MSIDirectory::REQUEST_TYPE_INV_ACK, 0, 0, 10, Message::K_ACK));
    cycles(llc, 3);
    CHECK_EQ((int)net.pending(), 1);                             // only the un-admitted GetS is left
    // the fill from memory is consumed while the queue is full
    serveMemory();
    cycles(llc, 3);
    CHECK_EQ((int)mem.pending(), 0);
}

TEST("P2-ADMIT-RF-SNOOP-L1")
{
    prepare();
    Cfg cfg("MultiCoreSystem");
    Stub bus(0), cpu(-1);                                         // upper = the bus, lower = the CPU side
    CacheController *l1 = new CacheController(withQueue(cfg.getSubMap("cache_controller", 0), 2), &bus, &cpu, "test");
    const uint64_t A = 0x7f0000004000ull;
    for (uint64_t i = 1; i <= 4; i++) cpu.deliver(msg(i, A + 8 * i, 0, 0, -1, 0, Message::K_DEMAND));
    cycles(l1, 8);
    CHECK(bus.count(Message::K_GETS) >= 1);
    CHECK_EQ((int)cpu.pending(), 1);
    // a snooped request from another core is consumed while the queue is full
    Message snoop = msg(60, 0x7f0000005000ull, 1, 1, 1, 0, Message::K_GETM); snoop.to.push_back(1);
    bus.deliver(snoop);
    cycles(l1, 3);
    CHECK_EQ((int)bus.pending(), 0);
    CHECK_EQ((int)cpu.pending(), 1);
    // the data response for the miss is consumed while the queue is full
    bus.deliver(msg(1, A + 8, 0, 0, 10, 0, Message::K_RESP, true));
    cycles(l1, 3);
    CHECK_EQ((int)bus.pending(), 0);
}
