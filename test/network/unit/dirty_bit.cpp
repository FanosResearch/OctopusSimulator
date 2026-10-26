// DIRTY-BIT (docs/TileLink_LLC_Dirty_Writeback_Plan.md s3.1, DW-7): GenericCacheLine::dirty in Octopus's main
// cache-line model, driven through a real L1 (CacheControllerDirectory, directory MSI and MESI; occupancy 0 and 4,
// pipelined 4):
//   - a Load's fill leaves the line clean (an L1 does not set the bit from data);
//   - a Store's bytes make it dirty; readLineBits reports the bit;
//   - a protocol's state update through updateLineBits (copyBits, from a line it built) keeps the bit;
//   - the bit moves with the whole line: set while the line waits in its MSHR, it is in the array after the fill;
//   - an evicted dirty (stored, M) line is dirty in the write-back buffer until its write-back is sent, and not
//     dirty after.
#include "testing.h"
#include "CacheControllerDirectory.h"
#include "CacheDataHandler.h"
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
        const Message *first(uint8_t kind, uint64_t line) const
        {
            for (auto &m : tx) if (m.kind == kind && ((m.addr ^ line) & ~uint64_t(63)) == 0) return &m;
            return nullptr;
        }
        bool wroteBack(uint64_t line) const
        {
            for (auto &m : tx) if (m.data != NULL && ((m.addr ^ line) & ~uint64_t(63)) == 0) return true;
            return false;
        }
    };
    struct Cfg : Configurable
    {
        Cfg(const std::string &name) : Configurable(std::vector<std::string>{}, std::string(CONFIGURATION_PATH) + std::string(SYSTEM_CONFIGURATIONS), name) {}
        using Configurable::getSubMap;
    };
    void setInt(Configurable::ParametersMap &m, const std::string &k, int v) { m.erase(k); m.insert({k, Parameter(k, Parameter::Integer, v)}); }
    void setStr(Configurable::ParametersMap &m, const std::string &k, const std::string &v) { m.erase(k); m.insert({k, Parameter(k, Parameter::String, v)}); }
    void prepare() { static bool once = false; if (!once) { Logger::getLogger()->registerReportPath("test/network/out"); once = true; } }

    const uint64_t A = 0x7f0000001000ull, A2 = A + 16384;   // A2: the same set of the 16 KiB direct-mapped L1

    // the controller's data handler is protected: a test-only view
    struct Ctrl : CacheControllerDirectory
    {
        using CacheControllerDirectory::CacheControllerDirectory;
        CacheDataHandler *dh() { return m_data_handler; }
    };
    struct L1
    {
        Stub net{0}, cpu{-1};
        Ctrl *c;
        L1(const std::string &proto, int latency, bool pipelined)
        {
            prepare();
            Cfg cfg("MultiCoreSystem_Grid");
            Configurable::ParametersMap m = cfg.getSubMap("cache_controller", 0);
            setInt(m, "m_data_handler.m_data_access_latency", latency);
            setInt(m, "m_data_handler.m_data_array_pipelined", pipelined ? 1 : 0);
            if (proto == "MESI") { setStr(m, "protocol_type", "DIRECTORY_MESI"); setStr(m, "fsm_filename", "MESI_directory_v2"); }
            c = new Ctrl(m, &net, &cpu, "test");
        }
        // not deleted, as in data_store.cpp: ~BaseController crashes outside a system (pre-existing)
        void run(int n) { ClockedObj *o = c; for (int i = 0; i < n; i++) o->cycleProcess(); }
        CacheDataHandler *dh() { return c->dh(); }
    };
    Message load(uint64_t id, uint64_t addr) { Message m(id, addr, 1, 0, 0); m.from = (uint16_t)-1; m.to = {0}; m.kind = Message::K_DEMAND; return m; }
    Message store(uint64_t id, uint64_t addr, uint8_t v, uint16_t n)
    {
        Message m = load(id, addr); m.complementary_value = 1;
        std::vector<uint8_t> b(n, v); m.copy(b.data(), n);
        return m;
    }
    Message fill(uint64_t id, uint64_t addr, uint8_t v)
    {
        Message m(id, addr, 1, 0, 0); m.from = 10; m.to = {0}; m.kind = Message::K_RESP;
        std::vector<uint8_t> b(64, v); m.copy(b.data(), 64);
        return m;
    }
    struct Mode { int latency; bool pipelined; const char *name; };
    const Mode modes[] = {{0, false, "occupancy 0"}, {4, false, "occupancy 4"}, {4, true, "pipelined 4"}};
}

TEST("DIRTY-BIT-LOAD-STORE")
{
    for (const char *proto : {"MSI", "MESI"})
        for (const Mode &md : modes)
        {
            {   // a Load's fill: clean
                L1 l(proto, md.latency, md.pipelined);
                l.cpu.deliver(load(1, A)); l.run(20);
                l.net.deliver(fill(1, A, 0x11)); l.run(40);
                CHECK(l.dh()->readLineBits(A));
                CHECK(!l.dh()->isDirty(A));
            }
            L1 l(proto, md.latency, md.pipelined);
            l.cpu.deliver(store(1, A, 0x77, 8)); l.run(20);
            CHECK(l.net.first(Message::K_GETM, A) != nullptr);
            l.net.deliver(fill(1, A, 0x11)); l.run(40);
            CHECK(l.dh()->isDirty(A));                          // the Store's bytes
            GenericCacheLine x;
            CHECK(l.dh()->readLineBits(A, &x));
            CHECK(x.dirty);                                     // readLineBits reports it
            x.dirty = false;                                    // a protocol's line, written through copyBits
            CHECK(l.dh()->updateLineBits(A, &x));
            CHECK(l.dh()->isDirty(A));                          // kept
            if (!l.dh()->isDirty(A)) std::printf("    %s %s: the bit was lost\n", proto, md.name);
        }
}

TEST("DIRTY-BIT-MOVES")
{
    for (const char *proto : {"MSI", "MESI"})
        for (const Mode &md : modes)
        {
            // MSHR -> array: set while the miss waits, present after the fill
            L1 l(proto, md.latency, md.pipelined);
            l.cpu.deliver(load(1, A)); l.run(20);
            CHECK(l.dh()->lineLocation(A) == CacheDataHandler::LineLocation::MSHR);
            CHECK(l.dh()->setDirty(A, true));
            l.net.deliver(fill(1, A, 0x11)); l.run(40);
            CHECK(l.dh()->lineLocation(A) == CacheDataHandler::LineLocation::ARRAY);
            CHECK(l.dh()->isDirty(A));

        }
    for (const char *proto : {"MSI", "MESI"})
        for (const Mode &md : modes)
        {
            // array -> write-back buffer: A stored (M, dirty), a conflicting miss evicts it with its data; while
            // A waits in the buffer before its write-back is sent it is dirty, and once sent it is not
            L1 l(proto, md.latency, md.pipelined);
            l.cpu.deliver(store(1, A, 0x77, 8)); l.run(20);
            l.net.deliver(fill(1, A, 0x11)); l.run(40);
            CHECK(l.dh()->isDirty(A));
            l.cpu.deliver(load(2, A2)); l.run(20);
            l.net.deliver(fill(2, A2, 0x22));
            int in_pwb_before_wb = 0, dirty_after_wb = 0;
            for (int i = 0; i < 60; i++)
            {
                l.run(1);
                bool sent = l.net.wroteBack(A);
                if (!sent && l.dh()->lineLocation(A) == CacheDataHandler::LineLocation::PWB)
                {
                    in_pwb_before_wb++;
                    CHECK(l.dh()->isDirty(A));
                }
                if (sent && l.dh()->isDirty(A)) dirty_after_wb++;
            }
            CHECK(l.net.wroteBack(A));
            CHECK_EQ(dirty_after_wb, 0);
            std::printf("    %s %s: %d cycles in the write-back buffer before the write-back\n", proto, md.name, in_pwb_before_wb);
        }
}
