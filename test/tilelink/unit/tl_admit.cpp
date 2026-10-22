// TL-ADMIT-*: the admission contract's Octopus side (docs/TileLink_Plan.md s5.2 points 1-3) in
// CacheController, with the shipped directory controllers and a protocol that predicts its
// emissions (the directory MSI tables plus a predictEmissions override): slots and identifiers are
// checked and reserved per channel on the right port, the message waits while they are short,
// the reservation is released when the transition has run, and a request parked as an outstanding
// miss keeps its inner D reservation until the fill answers it. The existing protocols (no
// override) never touch the per-channel interface.
#include "tl_common.h"
#include "CacheControllerDirectory.h"
#include "MSIDirectory.h"
#include "LLCMSIDirectory.h"
#include <deque>
using namespace octopus;

namespace
{
    using MsgClass = CoherenceProtocolHandler::MsgClass;

    // an interface the test feeds and reads, counting the per-channel admission calls
    struct StubTL : CommunicationInterface
    {
        std::deque<Message> rx[3]; std::vector<Message> tx; int peeked = -1;
        int free_slots[6], free_ids[6], reserved[6] = {}, ids_reserved[6] = {};
        int ch_calls = 0;
        StubTL(int id) : CommunicationInterface(id) { for (int c = 0; c < 6; c++) free_slots[c] = free_ids[c] = 8; }
        static int cls(const Message &m)
        {
            if (m.kind == Message::K_FWD || m.kind == Message::K_INV) return 1;
            if (m.data != NULL || m.kind == Message::K_ACK || m.kind == Message::K_RESP || m.kind == Message::K_FILL) return 2;
            return 0;
        }
        void deliver(const Message &m) { rx[cls(m)].push_back(m); }
        bool peekMessage(Message *m) override
        {
            for (int c : {2, 1, 0}) if (!rx[c].empty()) { m->copy(rx[c].front()); peeked = c; return true; }
            peeked = -1; return false;
        }
        void popFrontMessage() override { if (peeked >= 0 && !rx[peeked].empty()) rx[peeked].pop_front(); }
        bool pushMessage(Message &m, uint64_t, MessageType) override { tx.push_back(m); return true; }
        bool pushMessage2RX(Message &m, MessageType) override { deliver(m); return true; }
        int  txFreeSlotsCh(int c) override { ch_calls++; return free_slots[c] - reserved[c]; }
        void txReserveCh(int c, int n) override { ch_calls++; reserved[c] += n; }
        void txReleaseCh(int c, int n) override { ch_calls++; reserved[c] -= n; }
        int  txFreeIdsCh(int c) override { ch_calls++; return free_ids[c] - ids_reserved[c]; }
        void txReserveIdsCh(int c, int n) override { ch_calls++; ids_reserved[c] += n; }
        void txReleaseIdsCh(int c, int n) override { ch_calls++; ids_reserved[c] -= n; }
        int count(uint8_t kind) const { int n = 0; for (auto &m : tx) if (m.kind == kind) n++; return n; }
    };

    MsgClass emission(int kind, int ch, bool memory, int count = 1)
    {
        MsgClass c; c.kind = kind; c.tl_channel = ch; c.memory = memory; c.count = count; return c;
    }

    // the L1: a demand request emits one Acquire on A (a source)
    struct PredictL1 : MSIDirectory
    {
        using MSIDirectory::MSIDirectory;
        int calls = 0;
        bool predictEmissions(const Message &msg, GenericCacheLine &, std::vector<MsgClass> &out) override
        {
            calls++;
            if (msg.isDemandRequest()) out.push_back(emission(Message::K_GETS, 1, false));
            return true;
        }
    };
    // the LLC: a demand request emits its Grant on the inner D (a sink) and a memory read on the
    // outer A (a source); everything else emits nothing
    struct PredictLLC : LLCMSIDirectory
    {
        using LLCMSIDirectory::LLCMSIDirectory;
        int calls = 0;
        bool predictEmissions(const Message &msg, GenericCacheLine &, std::vector<MsgClass> &out) override
        {
            calls++;
            if (msg.isDemandRequest()) { out.push_back(emission(Message::K_RESP, 4, false)); out.push_back(emission(Message::K_MEM_READ, 1, true)); }
            return true;
        }
    };

    // a directory controller whose protocol is replaced by P (same table, same data handler)
    template <class P> struct With : CacheControllerDirectory
    {
        P *p;
        With(ParametersMap map, CommunicationInterface *up, CommunicationInterface *low) : CacheControllerDirectory(map, up, low, "test")
        {
            std::string fsm = std::string(FSM_PATH) + std::get<std::string>(parameters.at("fsm_filename").value) + ".csv";
            p = new P(m_data_handler, fsm, m_id, m_shared_memory_id);
            p->setHomeMap(m_home_ids, m_home_shift);
            delete m_protocol;
            m_protocol = p;
            delete m_processing_queue;
            m_processing_queue = new FRFCFS_Buffer<Message, CoherenceProtocolHandler>(&CoherenceProtocolHandler::getRequestState, m_protocol,
                                                                                      m_processing_queue_size, ~(uint64_t)(m_data_handler->getBlockSize() - 1), true);
        }
    };

    struct Cfg : Configurable
    {
        Cfg(const std::string &name) : Configurable(std::vector<std::string>{}, std::string(CONFIGURATION_PATH) + std::string(SYSTEM_CONFIGURATIONS), name) {}
        using Configurable::getSubMap;
    };
    Message msg(uint64_t id, uint64_t addr, uint64_t cv, int owner, int from, int to, uint8_t kind, bool data = false)
    {
        Message m(id, addr, 1, cv, (uint16_t)owner);
        m.from = (uint16_t)from; m.to.clear(); m.to.push_back((uint16_t)to); m.kind = kind;
        if (data) { uint8_t b[64] = {0}; m.copy(b, 64); }
        return m;
    }
    void cycles(BaseController *c, int n) { ClockedObj *o = c; for (int i = 0; i < n; i++) o->cycleProcess(); }
}

TEST("TL-ADMIT-L1")
{
    Cfg cfg("MultiCoreSystem_Grid");
    StubTL net(0), cpu(-1);
    auto *l1 = new With<PredictL1>(cfg.getSubMap("cache_controller", 0), &net, &cpu);
    const uint64_t A = 0x7f0000101000ull;
    net.free_slots[1] = 0;                                        // no A slot: the miss is not popped
    cpu.deliver(msg(1, A, 0, 0, -1, 0, Message::K_DEMAND));
    cycles(l1, 10);
    CHECK(l1->p->calls > 0);
    CHECK_EQ(net.count(Message::K_GETS), 0);
    net.free_slots[1] = 1; net.free_ids[1] = 0;                   // a slot but no source: still waits
    cycles(l1, 10);
    CHECK_EQ(net.count(Message::K_GETS), 0);
    net.free_ids[1] = 1;
    cycles(l1, 10);
    CHECK_EQ(net.count(Message::K_GETS), 1);
    CHECK_EQ(net.reserved[1], 0); CHECK_EQ(net.ids_reserved[1], 0);   // released when the transition ran
    CHECK_EQ(cpu.ch_calls, 0);                                    // the L1's TileLink port is its upper interface
}

TEST("TL-ADMIT-LLC-PARKED")
{
    Cfg cfg("MultiCoreSystem_Grid");
    StubTL mem(100), net(10);
    auto *llc = new With<PredictLLC>(cfg.getSubMap("llc_controller"), &mem, &net);
    llc->setLogRole(Logger::Role::LLC);                          // as the system does: the controller is a home
    const uint64_t C = 0x7f0000110000ull;
    net.deliver(msg(200, C, MSIDirectory::REQUEST_TYPE_GETS, 0, 0, 10, Message::K_GETS));
    cycles(llc, 12);
    CHECK(llc->p->calls > 0);
    CHECK_EQ(mem.count(Message::K_MEM_READ), 1);
    // parked as an outstanding miss: the outer A is released, the inner D (slot and sink) is kept
    CHECK_EQ(mem.reserved[1], 0); CHECK_EQ(mem.ids_reserved[1], 0);
    CHECK_EQ(net.reserved[4], 1); CHECK_EQ(net.ids_reserved[4], 1);
    // the fill answers it: the D goes out and the reservation is released (txAnswered)
    for (auto &m : mem.tx)
        if (m.kind == Message::K_MEM_READ) { mem.deliver(msg(m.msg_id, m.addr, 0, m.owner, 100, 10, Message::K_FILL, true)); break; }
    cycles(llc, 30);
    int data_out = 0; for (auto &m : net.tx) if (m.data != NULL) data_out++;
    CHECK_EQ(data_out, 1);
    CHECK_EQ(net.reserved[4], 0); CHECK_EQ(net.ids_reserved[4], 0);
}

TEST("TL-ADMIT-LEGACY")
{
    // the shipped directory protocol does not predict: the three-queue admission runs and the
    // per-channel interface is never consulted
    Cfg cfg("MultiCoreSystem_Grid");
    StubTL net(0), cpu(-1);
    auto *l1 = new CacheControllerDirectory(cfg.getSubMap("cache_controller", 0), &net, &cpu, "test");
    cpu.deliver(msg(1, 0x7f0000121000ull, 0, 0, -1, 0, Message::K_DEMAND));
    cycles(l1, 10);
    CHECK_EQ(net.count(Message::K_GETS), 1);
    CHECK_EQ(net.ch_calls, 0); CHECK_EQ(cpu.ch_calls, 0);
}
