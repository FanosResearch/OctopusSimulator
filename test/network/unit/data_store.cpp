// DATA-STORE (docs/Shared_Controller_Defects_Plan.md issue 2): a CPU Store's bytes reach the line, in every
// data-array model. The real directory L1 (MSI and MESI) with a CPU Store that carries its bytes:
//   - a Store miss: the fill and the Store's merge are one array access; the read-back shows the Store's
//     bytes over the fill's, the untouched fill bytes intact, and the eviction writes the merged line back;
//   - a Store hit while another request's fill holds the array: it waits for its own access and is answered
//     once, with its bytes written.
// Occupancy latency 0 and 4, pipelined latency 4. Before the fix the occupancy model with latency 4 answered
// the Store and dropped its bytes (CacheDataHandler::modifyData refused, the result was ignored).
#include "testing.h"
#include "CacheControllerDirectory.h"
#include "Configurable.h"
#include "CommunicationInterface.h"
#include "Logger.h"
#include <deque>
#include <string>
#include <vector>
using namespace octopus;

namespace
{
    struct Stub : CommunicationInterface
    {
        std::deque<Message> rx[3]; std::vector<Message> tx; int peeked = -1;
        Stub(int id) : CommunicationInterface(id) {}
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
        int responses(uint64_t id) const { int n = 0; for (auto &m : tx) if (m.msg_id == id) n++; return n; }
        const Message *response(uint64_t id) const { for (auto &m : tx) if (m.msg_id == id) return &m; return nullptr; }
        const Message *first(uint8_t kind, uint64_t line) const
        {
            for (auto &m : tx) if (m.kind == kind && ((m.addr ^ line) & ~uint64_t(63)) == 0) return &m;
            return nullptr;
        }
    };
    struct Cfg : Configurable
    {
        Cfg(const std::string &name) : Configurable(std::vector<std::string>{}, std::string(CONFIGURATION_PATH) + std::string(SYSTEM_CONFIGURATIONS), name) {}
        using Configurable::getSubMap;
    };
    void setInt(Configurable::ParametersMap &m, const std::string &k, int v) { m.erase(k); m.insert({k, Parameter(k, Parameter::Integer, v)}); }
    void setStr(Configurable::ParametersMap &m, const std::string &k, const std::string &v) { m.erase(k); m.insert({k, Parameter(k, Parameter::String, v)}); }
    void cycles(BaseController *c, int n) { ClockedObj *o = c; for (int i = 0; i < n; i++) o->cycleProcess(); }
    void prepare() { static bool once = false; if (!once) { Logger::getLogger()->registerReportPath("test/network/out"); once = true; } }

    const uint64_t A = 0x7f0000001000ull, A2 = A + 16384;   // A2: the same set of the 16 KiB direct-mapped L1

    struct L1
    {
        Stub net{0}, cpu{-1};
        CacheControllerDirectory *c;
        L1(const std::string &proto, int latency, bool pipelined)
        {
            prepare();
            Cfg cfg("MultiCoreSystem_Grid");
            Configurable::ParametersMap m = cfg.getSubMap("cache_controller", 0);
            setInt(m, "m_data_handler.m_data_access_latency", latency);
            setInt(m, "m_data_handler.m_data_array_pipelined", pipelined ? 1 : 0);
            if (proto == "MESI") { setStr(m, "protocol_type", "DIRECTORY_MESI"); setStr(m, "fsm_filename", "MESI_directory_v2"); }
            c = new CacheControllerDirectory(m, &net, &cpu, "test");
        }
        void run(int n) { cycles(c, n); }
    };
    Message load(uint64_t id, uint64_t addr) { Message m(id, addr, 1, 0, 0); m.from = (uint16_t)-1; m.to = {0}; m.kind = Message::K_DEMAND; return m; }
    Message store(uint64_t id, uint64_t addr, uint8_t v, uint16_t n)
    {
        Message m = load(id, addr); m.complementary_value = 1;
        std::vector<uint8_t> b(n, v); m.copy(b.data(), n);
        return m;
    }
    // the home's data for request `id`: exclusive, no acknowledgements pending
    Message fill(uint64_t id, uint64_t addr, uint8_t v)
    {
        Message m(id, addr, 1, 0, 0); m.from = 10; m.to = {0}; m.kind = Message::K_RESP;
        std::vector<uint8_t> b(64, v); m.copy(b.data(), 64);
        return m;
    }
    // bytes [off, off + n) are `in`, the others `out`
    bool lineIs(const Message *m, unsigned off, unsigned n, uint8_t in, uint8_t out)
    {
        if (!m || !m->data || m->data_size < 64) return false;
        for (unsigned i = 0; i < 64; i++) if (m->data[i] != ((i >= off && i < off + n) ? in : out)) return false;
        return true;
    }
    struct Mode { int latency; bool pipelined; const char *name; };
    const Mode modes[] = {{0, false, "occupancy 0"}, {4, false, "occupancy 4"}, {4, true, "pipelined 4"}};
}

TEST("DATA-STORE-MISS")
{
    for (const char *proto : {"MSI", "MESI"})
        for (const Mode &md : modes)
            for (unsigned off : {0u, 8u, 48u})
            {
                const unsigned n = off == 48 ? 16 : 8;   // below a line (a 64-byte CPU Store takes the full-line fill path)
                L1 l(proto, md.latency, md.pipelined);
                l.cpu.deliver(store(1, A + off, 0x77, (uint16_t)n)); l.run(20);
                CHECK(l.net.first(Message::K_GETM, A) != nullptr);
                l.net.deliver(fill(1, A, 0x11)); l.run(40);
                CHECK_EQ(l.cpu.responses(1), 1);                          // answered once
                l.cpu.deliver(load(2, A)); l.run(40);
                bool ok = lineIs(l.cpu.response(2), off, n, 0x77, 0x11);
                if (!ok) std::printf("    %s %s offset %u: the read-back does not show the Store's bytes\n", proto, md.name, off);
                // a conflicting miss evicts A: the write-back carries the merged line
                l.cpu.deliver(load(3, A2)); l.run(20);
                l.net.deliver(fill(3, A2, 0x33)); l.run(40);
                const Message *wb = nullptr;
                for (auto &m : l.net.tx) if (m.data != NULL && ((m.addr ^ A) & ~uint64_t(63)) == 0) wb = &m;
                bool ok_rb = ok;
                ok = lineIs(wb, off, n, 0x77, 0x11);
                if (!ok) std::printf("    %s %s offset %u: the write-back (%s) does not carry the Store's bytes\n", proto, md.name, off, wb ? "sent" : "none");
                CHECK(ok_rb && ok);
            }
}

TEST("DATA-STORE-HIT")
{
    // Store(1) misses and is filled; Store(2) to the same line arrives with the fill and hits while the fill
    // holds the array (occupancy, latency 4): it waits for its own access, is answered once, and both Stores'
    // bytes are in the line
    for (const char *proto : {"MSI", "MESI"})
        for (const Mode &md : modes)
        {
            L1 l(proto, md.latency, md.pipelined);
            l.cpu.deliver(store(1, A, 0x55, 8)); l.run(20);
            l.net.deliver(fill(1, A, 0x11));
            l.cpu.deliver(store(2, A + 8, 0x77, 8));
            l.run(40);
            CHECK_EQ(l.cpu.responses(1), 1);
            CHECK_EQ(l.cpu.responses(2), 1);
            CHECK(l.net.first(Message::K_GETM, A) && l.net.first(Message::K_GETM, A)->msg_id == 1u);   // no second miss
            l.cpu.deliver(load(3, A)); l.run(40);
            const Message *r = l.cpu.response(3);
            bool ok = r && r->data && r->data_size >= 64;
            for (unsigned i = 0; ok && i < 64; i++) ok = r->data[i] == (i < 8 ? 0x55 : i < 16 ? 0x77 : 0x11);
            if (!ok) std::printf("    %s %s: the read-back does not show both Stores\n", proto, md.name);
            CHECK(ok);
        }
}
