// TL-RETRY (docs/TileLink_Denied_Grant_Retry_Plan.md, docs/TileLink_Review_Action_Plan.md problem 3): a CPU
// access whose Acquire meets a denied Grant stays outstanding and is retried. The real TLC_L1 under its
// CacheControllerDirectory, the TileLink port a stub that counts the per-channel slot and identifier
// reservations; Grants, Probes and CPU accesses are fed deterministically. TL-RETRY-E-BLOCKED's second
// part runs the real TLC_LLC (its N_g state: an Acquire arriving before the GrantAck stalls, the GrantAck
// is consumed). The transports are exercised by test/tilelink/run.sh step retry (OCTOPUS_TL_DENY_FILLS).
#include "testing.h"
#include "CacheControllerDirectory.h"
#include "Configurable.h"
#include "CommunicationInterface.h"
#include "tilelink/Types.h"
#include "Logger.h"
#include <deque>
#include <vector>
using namespace octopus;
namespace ttl = octopus::tl;

namespace
{
    struct Port : CommunicationInterface
    {
        std::deque<Message> rx[3]; std::vector<Message> tx; int peeked = -1;
        int free_slots[6], free_ids[6], reserved[6] = {}, ids_reserved[6] = {};
        Port(int id) : CommunicationInterface(id) { for (int c = 0; c < 6; c++) free_slots[c] = free_ids[c] = 8; }
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
        int  txFreeSlotsCh(int c) override { return free_slots[c] - reserved[c]; }
        void txReserveCh(int c, int n) override { reserved[c] += n; }
        void txReleaseCh(int c, int n) override { reserved[c] -= n; }
        int  txFreeIdsCh(int c) override { return free_ids[c] - ids_reserved[c]; }
        void txReserveIdsCh(int c, int n) override { ids_reserved[c] += n; }
        void txReleaseIdsCh(int c, int n) override { ids_reserved[c] -= n; }
        int  count(uint8_t kind) const { int n = 0; for (auto &m : tx) if (m.kind == kind) n++; return n; }
        int  acquires() const { return count(Message::K_GETS) + count(Message::K_GETM); }
        const Message *last(uint8_t kind) const { for (auto it = tx.rbegin(); it != tx.rend(); ++it) if (it->kind == kind) return &*it; return nullptr; }
        const Message *lastAcquire() const { for (auto it = tx.rbegin(); it != tx.rend(); ++it) if (it->kind == Message::K_GETS || it->kind == Message::K_GETM) return &*it; return nullptr; }
        bool balanced() const { for (int c = 0; c < 6; c++) if (reserved[c] || ids_reserved[c]) return false; return true; }
    };
    struct Cfg : Configurable
    {
        Cfg(const std::string &name) : Configurable(std::vector<std::string>{}, std::string(CONFIGURATION_PATH) + std::string(SYSTEM_CONFIGURATIONS), name) {}
        using Configurable::getSubMap;
    };
    void setInt(Configurable::ParametersMap &m, const std::string &key, int v) { m.erase(key); m.insert({key, Parameter(key, Parameter::Integer, v)}); }

    const uint64_t A = 0x7f0000001000ull, B = 0x7f0000002000ull;
    void cycles(BaseController *c, int n) { ClockedObj *o = c; for (int i = 0; i < n; i++) o->cycleProcess(); }

    // a CPU access (the lower interface): a Load, or a Store with its bytes (the external CPU's form)
    Message load(uint64_t id, uint64_t addr) { Message m(id, addr, 1, 0, 0); m.from = (uint16_t)-1; m.to = {0}; m.kind = Message::K_DEMAND; return m; }
    Message store(uint64_t id, uint64_t addr, uint8_t v)
    {
        Message m = load(id, addr); m.complementary_value = 1;
        std::vector<uint8_t> b(64, v); m.copy(b.data(), 64);
        return m;
    }
    // a D message to the L1 (endpoint 0) from its home (10): Grant / GrantData, cap, denied, with bytes
    Message grant(uint64_t id, uint64_t addr, bool data, unsigned cap, bool denied, uint8_t fill, uint32_t sink)
    {
        Message m(id, addr, 1, 20, 0); m.from = 10; m.to = {0}; m.kind = Message::K_RESP;
        m.tl.channel = 4; m.tl.opcode = uint8_t(data ? ttl::OpD::GrantData : ttl::OpD::Grant); m.tl.param = uint8_t(cap);
        m.tl.size = 6; m.tl.denied = denied; m.tl.sink = sink;
        if (data) { std::vector<uint8_t> b(64, fill); m.copy(b.data(), 64); }
        return m;
    }
    Message probe(uint64_t id, uint64_t addr, unsigned cap)
    {
        Message m(id, addr, 1, 20, 0); m.from = 10; m.to = {0}; m.kind = cap == unsigned(ttl::Cap::toN) ? Message::K_INV : Message::K_FWD;
        m.tl.channel = 2; m.tl.opcode = uint8_t(ttl::OpB::ProbeBlock); m.tl.param = uint8_t(cap); m.tl.size = 6;
        return m;
    }
    const unsigned toT = unsigned(ttl::Cap::toT), toB = unsigned(ttl::Cap::toB), toN = unsigned(ttl::Cap::toN);
    const unsigned NtoB = unsigned(ttl::Grow::NtoB), NtoT = unsigned(ttl::Grow::NtoT), BtoT = unsigned(ttl::Grow::BtoT);

    struct L1
    {
        Port net{0}, cpu{-1};
        CacheControllerDirectory *c;
        explicit L1(int mshr = -2, int latency = -1, bool pipelined = false)
        {
            Cfg cfg("MultiCoreSystem_TileLink");
            Configurable::ParametersMap m = cfg.getSubMap("cache_controller", 0);
            if (mshr != -2) setInt(m, "num_mshr", mshr);
            if (latency >= 0) setInt(m, "m_data_handler.m_data_access_latency", latency);
            if (pipelined) setInt(m, "m_data_handler.m_data_array_pipelined", 1);
            c = new CacheControllerDirectory(m, &net, &cpu, "test");
        }
        void run(int n) { cycles(c, n); }
        int responses(uint64_t id) const { int n = 0; for (auto &m : cpu.tx) if (m.msg_id == id) n++; return n; }
        const Message *response(uint64_t id) const { for (auto &m : cpu.tx) if (m.msg_id == id) return &m; return nullptr; }
        bool idle(uint64_t addr) { return c->tlReplaying() == 0 && c->pendingOn(addr) == 0; }
    };
}

TEST("TL-RETRY-LOAD")
{
    L1 l;
    l.cpu.deliver(load(1, A));
    l.run(10);
    CHECK_EQ(l.net.count(Message::K_GETS), 1);
    l.net.deliver(grant(1, A, false, toB, true, 0, 3));                 // denied
    // cycle by cycle: the GrantAck goes out, and the retried Acquire only on a later cycle
    int ack_at = -1, acq_at = -1;
    for (int k = 0; k < 20; k++)
    {
        l.run(1);
        if (ack_at < 0 && l.net.count(Message::K_GRANT_ACK) == 1) ack_at = k;
        if (acq_at < 0 && l.net.count(Message::K_GETS) == 2) acq_at = k;
    }
    CHECK(ack_at >= 0 && acq_at > ack_at);
    CHECK_EQ(l.net.last(Message::K_GRANT_ACK)->tl.sink, 3u);            // the denied Grant's sink
    CHECK_EQ((int)l.cpu.tx.size(), 0);                                  // no CPU response for the denial
    const Message *a = l.net.lastAcquire();
    CHECK_EQ(a->msg_id, 1u); CHECK_EQ((unsigned)a->tl.param, NtoB); CHECK_EQ((unsigned)a->tl.channel, 1u);
    CHECK_EQ(l.c->pendingOn(A), 1u);                                    // parked once, not twice
    l.net.deliver(grant(1, A, true, toB, false, 0x5a, 4));
    l.run(20);
    CHECK_EQ(l.responses(1), 1);                                        // exactly once
    CHECK(l.response(1) && l.response(1)->data && l.response(1)->data[0] == 0x5a);
    CHECK_EQ(l.net.count(Message::K_GRANT_ACK), 2);
    CHECK_EQ(l.c->tlRetries(), 1u);
    CHECK(l.idle(A)); CHECK(l.net.balanced());
}

TEST("TL-RETRY-GRANTDATA")
{
    L1 l;
    l.cpu.deliver(load(1, A));
    l.run(10);
    l.net.deliver(grant(1, A, true, toB, true, 0xee, 3));               // denied GrantData: its bytes are not data
    l.run(20);
    CHECK_EQ((int)l.cpu.tx.size(), 0);
    CHECK_EQ(l.net.count(Message::K_GRANT_ACK), 1);
    CHECK_EQ(l.net.count(Message::K_GETS), 2);
    l.net.deliver(grant(1, A, true, toB, false, 0x5a, 4));
    l.run(20);
    CHECK_EQ(l.responses(1), 1);
    bool clean = true; for (int i = 0; i < 64; i++) if (l.response(1)->data[i] != 0x5a) clean = false;
    CHECK(clean);                                                       // the valid fill, never 0xee
    // and the line holds it: a later Load hits with the same bytes
    l.cpu.deliver(load(2, A));
    l.run(20);
    CHECK(l.response(2) && l.response(2)->data[0] == 0x5a);
    CHECK(l.idle(A)); CHECK(l.net.balanced());
}

TEST("TL-RETRY-STORE-MISS")
{
    L1 l;
    l.cpu.deliver(store(1, A, 0x77));
    l.run(10);
    CHECK_EQ(l.net.count(Message::K_GETM), 1);
    l.net.deliver(grant(1, A, true, toT, true, 0xee, 3));
    l.run(20);
    CHECK_EQ((int)l.cpu.tx.size(), 0);                                  // not committed
    CHECK_EQ(l.net.count(Message::K_GETM), 2);
    CHECK_EQ((unsigned)l.net.lastAcquire()->tl.param, NtoT);
    l.net.deliver(grant(1, A, true, toT, false, 0x11, 4));
    l.run(20);
    CHECK_EQ(l.responses(1), 1);
    l.cpu.deliver(load(2, A));                                           // reads the Store's bytes
    l.run(20);
    CHECK(l.response(2) && l.response(2)->data[0] == 0x77);
    CHECK(l.idle(A)); CHECK(l.net.balanced());
}

TEST("TL-RETRY-UPGRADE")
{
    L1 l;
    l.cpu.deliver(load(1, A)); l.run(10);
    l.net.deliver(grant(1, A, true, toB, false, 0x5a, 1)); l.run(20);   // S
    CHECK_EQ(l.responses(1), 1);
    l.cpu.deliver(store(2, A, 0x77)); l.run(10);
    CHECK_EQ((unsigned)l.net.lastAcquire()->tl.param, BtoT);
    l.net.deliver(grant(2, A, false, toT, true, 0, 2)); l.run(20);      // denied: SM_a -> S
    CHECK_EQ(l.responses(2), 0);
    CHECK_EQ(l.net.acquires(), 3);
    CHECK_EQ((unsigned)l.net.lastAcquire()->tl.param, BtoT);            // still S: BtoT again
    // the Branch is intact while the Store waits: a Load from another core's view is not needed here;
    // the line's bytes are checked after the Store completes (the Store applied once, over 0x5a)
    l.net.deliver(grant(2, A, false, toT, false, 0, 3)); l.run(20);     // Grant toT, no data: the line's copy
    CHECK_EQ(l.responses(2), 1);
    l.cpu.deliver(load(3, A)); l.run(20);
    CHECK(l.response(3) && l.response(3)->data[0] == 0x77);
    CHECK(l.idle(A)); CHECK(l.net.balanced());
}

TEST("TL-RETRY-PROBE")
{
    L1 l;
    l.cpu.deliver(load(1, A)); l.run(10);
    l.net.deliver(grant(1, A, true, toB, false, 0x5a, 1)); l.run(20);   // S
    l.cpu.deliver(store(2, A, 0x77)); l.run(10);                        // SM_a, BtoT
    l.net.deliver(probe(50, A, toN)); l.run(10);                        // SM_a + ProbeBlock_toN -> ProbeAck BtoN, IM_a
    CHECK_EQ(l.net.count(Message::K_ACK), 1);
    CHECK_EQ((unsigned)l.net.last(Message::K_ACK)->tl.param, unsigned(ttl::Prune::BtoN));
    l.net.deliver(grant(2, A, false, toT, true, 0, 2)); l.run(20);      // denied in IM_a -> I
    CHECK_EQ(l.responses(2), 0);
    CHECK_EQ((unsigned)l.net.lastAcquire()->tl.param, NtoT);            // the Branch is gone: NtoT, not BtoT
    l.net.deliver(grant(2, A, true, toT, false, 0x22, 3)); l.run(20);
    CHECK_EQ(l.responses(2), 1);
    l.cpu.deliver(load(3, A)); l.run(20);
    CHECK(l.response(3) && l.response(3)->data[0] == 0x77);
    CHECK(l.idle(A)); CHECK(l.net.balanced());
}

TEST("TL-RETRY-E-BLOCKED")
{
    // the L1: no E slot, the denied Grant waits (its cell needs one); E opens, GrantAck, then the retry
    L1 l;
    l.cpu.deliver(load(1, A)); l.run(10);
    l.net.free_slots[5] = 0;
    l.net.deliver(grant(1, A, false, toB, true, 0, 3));
    for (int k = 0; k < 30; k++) { l.run(1); CHECK(l.net.balanced()); }
    CHECK_EQ(l.net.count(Message::K_GRANT_ACK), 0);
    CHECK_EQ(l.net.count(Message::K_GETS), 1);                          // no retry before the GrantAck
    CHECK_EQ((int)l.cpu.tx.size(), 0);
    l.net.free_slots[5] = 8;
    l.run(20);
    CHECK_EQ(l.net.count(Message::K_GRANT_ACK), 1);
    CHECK_EQ(l.net.count(Message::K_GETS), 2);
    l.net.deliver(grant(1, A, true, toB, false, 0x5a, 4)); l.run(20);
    CHECK_EQ(l.responses(1), 1);
    CHECK(l.idle(A)); CHECK(l.net.balanced());

    // the manager (TLC_LLC, N_g): the retried Acquire arriving before the old GrantAck stalls, the GrantAck
    // (E) is consumed meanwhile, then the Acquire proceeds (TLC_Tables s3: N_g + AcquireBlock = Stall)
    Cfg cfg("MultiCoreSystem_TileLink");
    Port mem(100), inner(10);
    CacheControllerDirectory *llc = new CacheControllerDirectory(cfg.getSubMap("llc_controller"), &mem, &inner, "test");
    llc->setLogRole(Logger::Role::LLC);                                 // as the system does: the controller is a home
    auto acquire = [](uint64_t id) {
        Message m(id, A, 1, 20, 0); m.from = 0; m.to = {10}; m.kind = Message::K_GETS;
        m.tl.channel = 1; m.tl.opcode = uint8_t(ttl::OpA::AcquireBlock); m.tl.param = uint8_t(ttl::Grow::NtoB); m.tl.size = 6;
        return m;
    };
    inner.deliver(acquire(7));
    cycles(llc, 10);
    CHECK_EQ(mem.count(Message::K_MEM_READ), 1);
    Message fill(7, A, 1, 0, 0); fill.from = 100; fill.to = {10}; fill.kind = Message::K_FILL;
    { std::vector<uint8_t> b(64, 0); fill.copy(b.data(), 64); }
    fill.tl.channel = 4; fill.tl.opcode = uint8_t(ttl::OpD::AccessAckData); fill.tl.size = 6; fill.tl.denied = true;
    fill.tl.source = mem.tx.back().tl.source;
    mem.deliver(fill);
    cycles(llc, 10);
    const Message *g = inner.last(Message::K_RESP);
    CHECK(g && g->tl.denied && g->tl.opcode == uint8_t(ttl::OpD::Grant));   // Grant_denied
    inner.deliver(acquire(8));                                          // the retry, ahead of the GrantAck
    cycles(llc, 10);
    CHECK_EQ(mem.count(Message::K_MEM_READ), 1);                        // stalled in N_g: no new transaction
    Message e(7, A, 1, 20, 0); e.from = 0; e.to = {10}; e.kind = Message::K_GRANT_ACK;
    e.tl.channel = 5; e.tl.sink = g->tl.sink;
    inner.deliver(e);
    cycles(llc, 10);
    CHECK_EQ(mem.count(Message::K_MEM_READ), 2);                        // GrantAck consumed, then the Acquire proceeds
}

TEST("TL-RETRY-A-BLOCKED")
{
    L1 l;
    l.cpu.deliver(load(1, A)); l.run(10);
    l.net.free_ids[1] = 0;                                               // no A source for the retry
    l.net.deliver(grant(1, A, false, toB, true, 0, 3));
    for (int k = 0; k < 30; k++) { l.run(1); CHECK(l.net.balanced()); }
    CHECK_EQ(l.net.count(Message::K_GRANT_ACK), 1);                     // the E went out
    CHECK_EQ(l.net.count(Message::K_GETS), 1);                          // the retry waits for an A source
    CHECK_EQ((int)l.cpu.tx.size(), 0);
    CHECK_EQ(l.c->tlReplaying(), 1u); CHECK_EQ(l.c->pendingOn(A), 1u);  // still owned, once
    CHECK(l.c->queued() <= 1);
    l.net.free_ids[1] = 8;
    l.run(10);
    CHECK_EQ(l.net.count(Message::K_GETS), 2);
    l.net.deliver(grant(1, A, true, toB, false, 0x5a, 4)); l.run(20);
    CHECK_EQ(l.responses(1), 1);
    CHECK(l.idle(A)); CHECK(l.net.balanced());
}

TEST("TL-RETRY-ORDER")
{
    L1 l;
    l.cpu.deliver(load(1, A)); l.run(10);                               // A: IS_a
    l.cpu.deliver(store(2, A, 0x77));                                   // younger, same line: waits behind
    l.cpu.deliver(load(3, B)); l.run(10);                               // another line: its own Acquire
    CHECK_EQ(l.net.count(Message::K_GETS), 2);
    l.net.deliver(grant(1, A, false, toB, true, 0, 3));                 // deny A
    l.net.deliver(probe(60, B, toB)); l.run(20);                        // a Probe of B still progresses
    CHECK_EQ(l.net.count(Message::K_ACK), 1);
    // the retry is the older Load (msg 1, NtoB); the younger Store has not issued an Acquire of its own
    int acq_a = 0; uint64_t first = 0;
    for (auto &m : l.net.tx) if ((m.kind == Message::K_GETS || m.kind == Message::K_GETM) && ((m.addr ^ A) & ~uint64_t(63)) == 0) { if (++acq_a == 2) first = m.msg_id; }
    CHECK_EQ(acq_a, 2); CHECK_EQ(first, 1u);
    CHECK_EQ(l.net.count(Message::K_GETM), 0);
    l.net.deliver(grant(3, B, true, toB, false, 0x33, 5)); l.run(20);   // B completes meanwhile
    CHECK_EQ(l.responses(3), 1);
    l.net.deliver(grant(1, A, true, toB, false, 0x5a, 4)); l.run(20);   // the Load completes, then the Store upgrades
    CHECK_EQ(l.responses(1), 1); CHECK_EQ(l.responses(2), 0);
    CHECK_EQ(l.net.count(Message::K_GETM), 1);
    l.net.deliver(grant(2, A, false, toT, false, 0, 6)); l.run(20);
    CHECK_EQ(l.responses(2), 1);
    // program order on A: the Load's response precedes the Store's
    int i1 = -1, i2 = -1; for (int i = 0; i < (int)l.cpu.tx.size(); i++) { if (l.cpu.tx[i].msg_id == 1) i1 = i; if (l.cpu.tx[i].msg_id == 2) i2 = i; }
    CHECK(i1 >= 0 && i2 > i1);
    CHECK(l.response(1)->data[0] == 0x5a);                              // the Load saw the line before the Store
    CHECK(l.idle(A)); CHECK(l.net.balanced());
}

TEST("TL-RETRY-REPEATED")
{
    L1 l;
    l.cpu.deliver(store(1, A, 0x77)); l.run(10);
    for (uint32_t k = 0; k < 3; k++)
    {
        l.net.deliver(grant(1, A, true, toT, true, 0xee, 10 + k)); l.run(20);
        CHECK_EQ((int)l.cpu.tx.size(), 0);
        CHECK_EQ(l.c->pendingOn(A), 1u); CHECK(l.c->tlReplaying() <= 1);
        CHECK(l.net.balanced());
    }
    CHECK_EQ(l.net.count(Message::K_GRANT_ACK), 3); CHECK_EQ(l.net.count(Message::K_GETM), 4);
    l.net.deliver(grant(1, A, true, toT, false, 0x11, 20)); l.run(20);
    CHECK_EQ(l.responses(1), 1);
    CHECK_EQ(l.c->tlRetries(), 3u);
    l.cpu.deliver(load(2, A)); l.run(20);
    CHECK(l.response(2) && l.response(2)->data[0] == 0x77);             // the Store applied once
    CHECK(l.idle(A)); CHECK(l.net.balanced());
}

TEST("TL-RETRY-PERMANENT")
{
    L1 l;
    l.cpu.deliver(load(1, A)); l.run(10);
    for (uint32_t k = 0; k < 20; k++)
    {
        l.net.deliver(grant(1, A, false, toB, true, 0, 10 + k)); l.run(10);
        CHECK_EQ(l.c->pendingOn(A), 1u); CHECK(l.c->tlReplaying() <= 1); CHECK(l.c->queued() <= 1);
        CHECK(l.net.balanced());
    }
    CHECK_EQ((int)l.cpu.tx.size(), 0);                                  // never a fake success
    CHECK_EQ(l.net.count(Message::K_GRANT_ACK), 20); CHECK_EQ(l.net.count(Message::K_GETS), 21);
    CHECK_EQ(l.c->tlRetries(), 20u);
    CHECK_EQ(l.c->pendingLines(), 1u);                                  // bounded: one parked access, one record
}

TEST("TL-RETRY-MSHR")
{
    L1 l(1);
    l.cpu.deliver(load(1, A)); l.cpu.deliver(load(2, B)); l.run(10);
    CHECK_EQ(l.net.count(Message::K_GETS), 1);                          // B refused: one MSHR
    l.net.deliver(grant(1, A, false, toB, true, 0, 3));
    for (int k = 0; k < 30; k++) { l.run(1); CHECK(l.net.balanced()); }
    CHECK_EQ(l.net.count(Message::K_GETS), 2);                          // the retry of A holds the credit; B still waits
    const Message *a = l.net.lastAcquire(); CHECK_EQ(a->msg_id, 1u);
    l.net.deliver(grant(1, A, true, toB, false, 0x5a, 4)); l.run(20);
    CHECK_EQ(l.responses(1), 1);
    CHECK_EQ(l.net.count(Message::K_GETS), 3);                          // the MSHR freed: B's Acquire
    l.net.deliver(grant(2, B, true, toB, false, 0x33, 5)); l.run(20);
    CHECK_EQ(l.responses(2), 1);
    CHECK(l.idle(A) && l.idle(B)); CHECK(l.net.balanced());
}

// docs/Shared_Controller_Defects_Plan.md R1 (introduced by 3df508a4): with the one MSHR held, a younger Store
// carrying its bytes is refused; the dataless Grant behind it in the queue must still be processed. Case one:
// the Grant is denied (the GrantAck goes out and the Load is retried); case two (TL-RETRY-MSHR-UPGRADE): a
// successful dataless upgrade Grant. Before the fix the refused Store ended every controller cycle at the head
// of the queue and neither Grant was ever processed.
TEST("TL-RETRY-MSHR-STORE")
{
    L1 l(1);
    l.cpu.deliver(load(1, A)); l.run(10);
    l.cpu.deliver(store(2, B, 0x77)); l.run(10);
    CHECK_EQ(l.net.acquires(), 1);                                      // the Store is refused: one MSHR
    const int q = l.c->queued();
    for (int k = 0; k < 20; k++) { l.run(1); CHECK_EQ(l.c->queued(), q); CHECK(l.net.balanced()); }
    l.net.deliver(grant(1, A, false, toB, true, 0, 3)); l.run(20);
    CHECK_EQ(l.net.count(Message::K_GRANT_ACK), 1);                     // the denied Grant ran behind the refused Store
    CHECK_EQ(l.net.acquires(), 2);
    { const Message *a = l.net.lastAcquire(); CHECK(a && a->msg_id == 1u); }   // the Load's retry holds the credit
    l.net.deliver(grant(1, A, true, toB, false, 0x11, 4)); l.run(20);
    CHECK_EQ(l.responses(1), 1);
    CHECK_EQ(l.net.acquires(), 3);
    { const Message *a = l.net.lastAcquire(); CHECK(a && a->msg_id == 2u); }   // the MSHR freed: the Store's Acquire
    l.net.deliver(grant(2, B, true, toT, false, 0x22, 5)); l.run(20);
    CHECK_EQ(l.responses(2), 1);
    CHECK_EQ(l.net.count(Message::K_GRANT_ACK), 3);
    l.cpu.deliver(load(3, B)); l.run(20);
    CHECK(l.response(3) && l.response(3)->data && l.response(3)->data[0] == 0x77);   // the Store's saved bytes
    CHECK(l.idle(A) && l.idle(B)); CHECK(l.net.balanced()); CHECK_EQ(l.c->queued(), 0);
}

TEST("TL-RETRY-MSHR-UPGRADE")
{
    L1 l(1);
    l.cpu.deliver(load(1, A)); l.run(10);
    l.net.deliver(grant(1, A, true, toB, false, 0x5a, 1)); l.run(20);
    CHECK_EQ(l.responses(1), 1);                                        // A in B (Branch)
    l.cpu.deliver(store(2, A, 0x77)); l.run(10);                        // Acquire BtoT: the one MSHR
    l.cpu.deliver(store(3, B, 0x88)); l.run(10);                        // refused
    CHECK_EQ(l.net.acquires(), 2);
    l.net.deliver(grant(2, A, false, toT, false, 0, 2)); l.run(20);     // successful, dataless
    CHECK_EQ(l.net.count(Message::K_GRANT_ACK), 2);
    CHECK_EQ(l.responses(2), 1);
    CHECK_EQ(l.net.acquires(), 3);
    { const Message *a = l.net.lastAcquire(); CHECK(a && a->msg_id == 3u); }
    l.net.deliver(grant(3, B, true, toT, false, 0x22, 3)); l.run(20);
    CHECK_EQ(l.responses(3), 1);
    CHECK_EQ(l.net.count(Message::K_GRANT_ACK), 3);
    l.cpu.deliver(load(4, A)); l.cpu.deliver(load(5, B)); l.run(20);
    CHECK(l.response(4) && l.response(4)->data && l.response(4)->data[0] == 0x77);
    CHECK(l.response(5) && l.response(5)->data && l.response(5)->data[0] == 0x88);
    CHECK(l.idle(A) && l.idle(B)); CHECK(l.net.balanced()); CHECK_EQ(l.c->queued(), 0);
}

// docs/Shared_Controller_Defects_Plan.md issue 3: a demand refused by admission keeps its place. One MSHR held by
// Load(1, B); an older access to A and a younger one are both refused; when B's fill frees the MSHR the older
// issues first and the values follow program order. Load then Store: the Load returns memory's 0x11, not the
// younger Store's 0x77 (before the fix the refused Load moved behind the Store); Store then Load: the Load
// returns the Store's 0x77.
TEST("TL-ADMIT-ORDER")
{
    for (int store_first = 0; store_first < 2; store_first++)
    {
        L1 l(1);
        l.cpu.deliver(load(1, B)); l.run(10);
        // one cycle apart, so the older access enters the controller first (the stub port hands out a message
        // carrying data, the Store, ahead of a dataless one delivered with it)
        if (store_first) { l.cpu.deliver(store(2, A, 0x77)); l.run(1); l.cpu.deliver(load(3, A)); }
        else             { l.cpu.deliver(load(2, A)); l.run(1); l.cpu.deliver(store(3, A, 0x77)); }
        l.run(10);
        CHECK_EQ(l.net.acquires(), 1);
        l.net.deliver(grant(1, B, true, toB, false, 0x22, 1)); l.run(20);
        CHECK_EQ(l.responses(1), 1);
        const Message *a = l.net.lastAcquire();
        CHECK(a && a->msg_id == 2u);                                    // the older access to A goes first
        if (!a || a->msg_id != 2u) continue;
        if (store_first)
        {
            l.net.deliver(grant(2, A, true, toT, false, 0x11, 2)); l.run(30);
            CHECK_EQ(l.responses(2), 1);
            CHECK(l.response(3) && l.response(3)->data && l.response(3)->data[0] == 0x77);
        }
        else
        {
            l.net.deliver(grant(2, A, true, toB, false, 0x11, 2)); l.run(20);
            CHECK(l.response(2) && l.response(2)->data && l.response(2)->data[0] == 0x11);
            const Message *u = l.net.lastAcquire();                     // the Store's upgrade
            CHECK(u && u->msg_id == 3u);
            l.net.deliver(grant(3, A, false, toT, false, 0, 3)); l.run(20);
            CHECK_EQ(l.responses(3), 1);
            l.cpu.deliver(load(4, A)); l.run(20);
            CHECK(l.response(4) && l.response(4)->data && l.response(4)->data[0] == 0x77);
        }
        CHECK(l.idle(A) && l.idle(B)); CHECK(l.net.balanced()); CHECK_EQ(l.c->queued(), 0);
    }
}

TEST("TL-RETRY-ARRAY")
{
    // occupancy and pipelined data array, latency 4: the denial changes nothing the CPU sees, and the Store's
    // bytes are read back in both models, with and without the denial (the occupancy model dropped them
    // before docs/Shared_Controller_Defects_Plan.md issue 2: the fill closed the array and the Store's write
    // was refused and ignored; now it rides on the fill's access)
    for (int mode = 0; mode < 2; mode++)
    {
        uint8_t got[2] = {0, 0};
        for (int deny = 0; deny < 2; deny++)
        {
            L1 l(-2, 4, mode == 1);
            l.cpu.deliver(store(1, A, 0x77)); l.run(20);
            if (deny)
            {
                l.net.deliver(grant(1, A, true, toT, true, 0xee, 3)); l.run(40);
                CHECK_EQ((int)l.cpu.tx.size(), 0);                       // nothing answered, nothing written early
                CHECK_EQ(l.net.count(Message::K_GETM), 2);
                CHECK_EQ(l.c->pendingOn(A), 1u);
            }
            l.net.deliver(grant(1, A, true, toT, false, 0x11, 4)); l.run(40);
            CHECK_EQ(l.responses(1), 1);
            l.cpu.deliver(load(2, A)); l.run(40);
            CHECK(l.response(2) && l.response(2)->data);
            got[deny] = l.response(2) && l.response(2)->data ? l.response(2)->data[0] : 0xff;
            CHECK(l.idle(A)); CHECK(l.net.balanced());
        }
        CHECK_EQ((int)got[1], (int)got[0]);                             // the retry changes nothing
        CHECK_EQ((int)got[0], 0x77);
        CHECK_EQ((int)got[1], 0x77);
    }
}

// docs/Shared_Controller_Defects_Plan.md issue 2, at the TLC L1: a Store hit right after another request's fill,
// while that fill holds the occupancy array (latency 4), waits for its own access; it is answered once and its
// bytes are read back (they were dropped: the hit answered inline and its write was refused and ignored).
TEST("TL-DATA-STORE-HIT")
{
    for (int pipelined = 0; pipelined < 2; pipelined++)
    {
        L1 l(-2, 4, pipelined == 1);
        l.cpu.deliver(load(1, A)); l.run(20);
        l.net.deliver(grant(1, A, true, toT, false, 0x11, 1));
        Message h = load(2, A); h.complementary_value = 1; { std::vector<uint8_t> b(8, 0x77); h.copy(b.data(), 8); }
        l.cpu.deliver(h); l.run(40);
        CHECK_EQ(l.responses(1), 1);
        CHECK_EQ(l.responses(2), 1);
        l.cpu.deliver(load(3, A)); l.run(40);
        const Message *r = l.response(3);
        bool ok = r && r->data && r->data_size >= 64;
        for (unsigned i = 0; ok && i < 64; i++) ok = r->data[i] == (i < 8 ? 0x77 : 0x11);
        CHECK(ok);
        CHECK(l.idle(A)); CHECK(l.net.balanced());
    }
}

// docs/Shared_Controller_Defects_Plan.md, whole-line CPU Store: the TLC control. A Store of a whole line (nonuniform
// 64 bytes, aligned) misses: answered once, after the Grant; the read-back shows every byte of the Store over the
// GrantData's (the directory and snoop L1s exited here; TLC_L1 never takes a Store's bytes for a fill)
TEST("TL-DATA-LINE")
{
    for (int mode = 0; mode < 3; mode++)
    {
        L1 l(-2, mode == 0 ? 0 : 4, mode == 2);
        Message s = load(1, A); s.complementary_value = 1;
        std::vector<uint8_t> b(64); for (int i = 0; i < 64; i++) b[i] = uint8_t(0xa1 + 7 * i);
        s.copy(b.data(), 64);
        l.cpu.deliver(s); l.run(20);
        CHECK_EQ(l.net.count(Message::K_GETM), 1);
        CHECK_EQ(l.responses(1), 0);
        l.net.deliver(grant(1, A, true, toT, false, 0x11, 1)); l.run(40);
        CHECK_EQ(l.responses(1), 1);
        l.cpu.deliver(load(2, A)); l.run(40);
        const Message *r = l.response(2);
        bool ok = r && r->data && r->data_size >= 64;
        for (int i = 0; ok && i < 64; i++) ok = r->data[i] == b[i];
        CHECK(ok);
        CHECK(l.idle(A)); CHECK(l.net.balanced());
    }
}
