// DATA-LINE (docs/Shared_Controller_Defects_Plan.md, whole-line CPU Store): a CPU Store whose bytes cover a whole
// line (64 bytes, aligned) is a Store, not a fill. Before the fix CacheController::updateCacheLine took its payload
// for fill data: a miss wrote an absent line and the process exited with status 0 ("update data of an unfound
// line"), an upgrade wrote the line before the GetM was granted. Directory MSI, directory MESI and snoop MSI (the
// real snoop controller: its own request observed on the bus, then the data), occupancy latency 0 and 4 and
// pipelined latency 4, nonuniform 64-byte patterns compared byte by byte.
// Each case runs in a child process that must exit 0 and print its completion marker: a test that only read the
// status would pass on the old exit(0). The fatal path (a fill for a line that is not there) must exit nonzero.
#include "testing.h"
#include "CacheControllerDirectory.h"
#include "CacheController.h"
#include "CacheSim.h"
#include "ExternalCPU.h"
#include "Configurable.h"
#include "CommunicationInterface.h"
#include "Logger.h"
#include "Protocols/MSIProtocol.h"
#include "Protocols/MSIDirectory.h"
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
        const Message *lastRequest(uint64_t line, uint64_t type) const
        {
            for (auto it = tx.rbegin(); it != tx.rend(); ++it)
                if (it->data == NULL && it->complementary_value == type && ((it->addr ^ line) & ~uint64_t(63)) == 0) return &*it;
            return nullptr;
        }
        const Message *lastData(uint64_t line) const
        {
            for (auto it = tx.rbegin(); it != tx.rend(); ++it) if (it->data != NULL && ((it->addr ^ line) & ~uint64_t(63)) == 0) return &*it;
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

    const uint64_t A = 0x7f0000001000ull, NB = A + 64, A2 = A + 16384;   // NB: the next line; A2: A's set (16 KiB, direct mapped)
    typedef std::vector<uint8_t> Line;
    Line pattern(uint8_t seed, uint8_t step) { Line v(64); for (int i = 0; i < 64; i++) v[i] = uint8_t(seed + step * i); return v; }
    const Line SP = pattern(0xa1, 7), FP = pattern(0x10, 3), OP = pattern(0x40, 5), NP = pattern(0x99, 11), HP = pattern(0x05, 13);
    bool same(const Message *m, const Line &want) { if (!m || !m->data || m->data_size < 64) return false; for (int i = 0; i < 64; i++) if (m->data[i] != want[i]) return false; return true; }
    std::string bytes(const Message *m) { if (!m || !m->data) return "none"; char b[16]; std::snprintf(b, sizeof b, "%02x..%02x", m->data[0], m->data[63]); return b; }

    // one L1 under test with its two stub interfaces: "MSI" / "MESI" (directory, the grid preset), "SNOOP" (snoop MSI)
    struct L1
    {
        std::string proto; Stub up{0}, cpu{-1}; BaseController *c;
        L1(const std::string &p, int latency, bool pipelined) : proto(p)
        {
            prepare();
            Cfg cfg(p == "SNOOP" ? "MultiCoreSystem_Snoop" : "MultiCoreSystem_Grid");
            Configurable::ParametersMap m = cfg.getSubMap("cache_controller", 0);
            setInt(m, "m_data_handler.m_data_access_latency", latency);
            setInt(m, "m_data_handler.m_data_array_pipelined", pipelined ? 1 : 0);
            if (p == "MESI") { setStr(m, "protocol_type", "DIRECTORY_MESI"); setStr(m, "fsm_filename", "MESI_directory_v2"); }
            if (p == "SNOOP") c = new CacheController(m, &up, &cpu, "test");
            else c = new CacheControllerDirectory(m, &up, &cpu, "test");
        }
        void run(int n) { cycles(c, n); }
        bool snoop() const { return proto == "SNOOP"; }
        Message cpuMsg(uint64_t id, uint64_t addr, uint64_t type, const Line *b)
        {
            Message m(id, addr, 1, type, 0); m.from = (uint16_t)-1; m.to = {0};
            if (b) m.copy(b->data(), 64);
            return m;
        }
        void load(uint64_t id, uint64_t addr) { cpu.deliver(cpuMsg(id, addr, 0, nullptr)); }
        void store(uint64_t id, uint64_t addr, const Line &b) { cpu.deliver(cpuMsg(id, addr, 1, &b)); }
        // the home's data (directory: from the home 10, `acks` invalidations pending; snoop: from the LLC 10)
        void data(uint64_t id, uint64_t addr, const Line &b, uint64_t acks = 0)
        {
            Message m(id, addr, 1, acks, 0); m.from = 10; m.to = {0}; m.kind = Message::K_RESP; m.copy(b.data(), 64);
            up.deliver(m);
        }
        // snoop: the bus orders the L1's own request and shows it back to it
        void ownRequest(uint64_t id, uint64_t addr, uint64_t type)
        {
            Message m(id, addr, 1, type, 0); m.from = 0; m.to = {0};
            m.kind = type == MSIProtocol::REQUEST_TYPE_GETS ? Message::K_GETS : type == MSIProtocol::REQUEST_TYPE_GETM ? Message::K_GETM : Message::K_PUTM;
            up.deliver(m);
        }
        // a whole miss for request `id`: directory: data from the home; snoop: own request on the bus, then data
        void miss(uint64_t id, uint64_t addr, uint64_t snoop_type, const Line &b)
        {
            if (snoop()) { ownRequest(id, addr, snoop_type); run(10); }
            data(id, addr, b); run(40);
        }
        uint64_t getM() const { return snoop() ? MSIProtocol::REQUEST_TYPE_GETM : MSIDirectory::REQUEST_TYPE_GETM; }
        uint64_t getS() const { return snoop() ? MSIProtocol::REQUEST_TYPE_GETS : MSIDirectory::REQUEST_TYPE_GETS; }
        // evict A through a conflicting miss on A2 (request `id`); the write-back of A (directory PutM_Data; snoop:
        // PutM, its own PutM observed, then the data)
        const Message *evictA(uint64_t id)
        {
            load(id, A2); run(20);
            if (snoop())
            {
                ownRequest(id, A2, MSIProtocol::REQUEST_TYPE_GETS); run(10);
                data(id, A2, NP); run(40);
                const Message *put = up.lastRequest(A, MSIProtocol::REQUEST_TYPE_PUTM);
                if (put) { ownRequest(put->msg_id, A, MSIProtocol::REQUEST_TYPE_PUTM); run(20); }
            }
            else { data(id, A2, NP); run(40); }
            return up.lastData(A);
        }
    };
    struct Mode { int latency; bool pipelined; const char *name; };
    const Mode modes[] = {{0, false, "occ0"}, {4, false, "occ4"}, {4, true, "pipe4"}};
    const char *protos[] = {"MSI", "MESI", "SNOOP"};

    // ---- the cases, each in its own process (CHILD); `which` = "<proto>-<mode>"
    void parse(const std::string &which, std::string &proto, Mode &md)
    {
        size_t d = which.find('-'); proto = which.substr(0, d); std::string mn = which.substr(d + 1);
        for (const Mode &x : modes) if (mn == x.name) md = x;
    }
    // a whole-line Store misses on an absent line: the GetM goes out, nothing is answered before the data, the line
    // reads back the Store's 64 bytes, the next line's fill installs its own bytes, the eviction writes the Store's
    void caseMiss(const std::string &which)
    {
        std::string proto; Mode md{0, false, ""}; parse(which, proto, md);
        L1 l(proto, md.latency, md.pipelined);
        l.store(1, A, SP); l.run(20);
        CHECK(l.up.lastRequest(A, l.getM()) != nullptr);
        CHECK_EQ(l.cpu.responses(1), 0);
        l.miss(1, A, l.getM(), FP);
        CHECK_EQ(l.cpu.responses(1), 1);
        l.load(2, A); l.run(40);
        CHECK_MSG(same(l.cpu.response(2), SP), "read-back " + bytes(l.cpu.response(2)));
        l.load(3, NB); l.run(20); l.miss(3, NB, l.getS(), NP);
        CHECK_MSG(same(l.cpu.response(3), NP), "the next line's fill " + bytes(l.cpu.response(3)));
        const Message *wb = l.evictA(4);
        CHECK_MSG(same(wb, SP), "the write-back of A " + bytes(wb));
    }
    // a whole-line Store hits a line in M: answered once, its bytes read back and written back
    void caseHit(const std::string &which)
    {
        std::string proto; Mode md{0, false, ""}; parse(which, proto, md);
        L1 l(proto, md.latency, md.pipelined);
        l.store(1, A, HP); l.run(20); l.miss(1, A, l.getM(), FP);
        CHECK_EQ(l.cpu.responses(1), 1);
        l.store(2, A, SP); l.run(40);
        CHECK_EQ(l.cpu.responses(2), 1);
        l.load(3, A); l.run(40);
        CHECK_MSG(same(l.cpu.response(3), SP), "read-back " + bytes(l.cpu.response(3)));
        const Message *wb = l.evictA(4);
        CHECK_MSG(same(wb, SP), "the write-back of A " + bytes(wb));
    }
    // a whole-line Store upgrades a line in S. Before any response, and while the permission is pending (directory: the data came with an
    // invalidation acknowledgement still to arrive, SM_a; snoop: the data came before the L1's own GetM was seen on
    // the bus, SM_a), the line keeps its old bytes (a Load of this core reads them) and the Store is not answered;
    // once the permission arrives the Store is answered once and the line holds its bytes
    void caseUpgrade(const std::string &which)
    {
        std::string proto; Mode md{0, false, ""}; parse(which, proto, md);
        L1 l(proto, md.latency, md.pipelined);
        l.load(1, A); l.run(20); l.miss(1, A, l.getS(), OP);
        CHECK_MSG(same(l.cpu.response(1), OP), "the Load's fill " + bytes(l.cpu.response(1)));
        l.store(2, A, SP); l.run(20);
        CHECK(l.up.lastRequest(A, l.getM()) != nullptr);
        // SM_ad: the GetM is out, nothing has come back; the line still holds its old bytes (the early write
        // happened here, and the home's data then overwrote it, which hid it from the checks below)
        l.load(6, A); l.run(40);
        CHECK_MSG(same(l.cpu.response(6), OP), "a Load before any response read " + bytes(l.cpu.response(6)));
        CHECK_EQ(l.cpu.responses(2), 0);
        if (l.snoop()) l.data(2, A, OP);                 // SM_ad + OwnData -> SM_a: waits for its own GetM
        else l.data(2, A, OP, 1);                        // SM_ad + Data_nonZeroAck -> SM_a: one InvAck to come
        l.run(40);
        CHECK_EQ(l.cpu.responses(2), 0);
        l.load(3, A); l.run(40);
        CHECK_MSG(same(l.cpu.response(3), OP), "a Load while the permission is pending read " + bytes(l.cpu.response(3)));
        if (l.snoop()) l.ownRequest(2, A, MSIProtocol::REQUEST_TYPE_GETM);
        else
        {
            Message ack(2, A, 1, MSIDirectory::REQUEST_TYPE_INV_ACK, 1); ack.from = 1; ack.to = {0}; ack.kind = Message::K_ACK;
            l.up.deliver(ack);
        }
        l.run(40);
        CHECK_EQ(l.cpu.responses(2), 1);
        l.load(4, A); l.run(40);
        CHECK_MSG(same(l.cpu.response(4), SP), "read-back " + bytes(l.cpu.response(4)));
        const Message *wb = l.evictA(5);
        CHECK_MSG(same(wb, SP), "the write-back of A " + bytes(wb));
    }
    struct Reg
    {
        Reg()
        {
            for (const char *p : protos)
                for (const Mode &md : modes)
                {
                    std::string w = std::string(p) + "-" + md.name;
                    octest::children().push_back({"line-miss-" + w, [w] { caseMiss(w); }});
                    octest::children().push_back({"line-hit-" + w, [w] { caseHit(w); }});
                    octest::children().push_back({"line-upgrade-" + w, [w] { caseUpgrade(w); }});
                }
        }
    } reg;
}

TEST("DATA-LINE-MISS")
{
    for (const char *p : protos) for (const Mode &md : modes) CHECK_CHILD_OK(std::string("line-miss-") + p + "-" + md.name);
}
TEST("DATA-LINE-HIT")
{
    for (const char *p : protos) for (const Mode &md : modes) CHECK_CHILD_OK(std::string("line-hit-") + p + "-" + md.name);
}
TEST("DATA-LINE-UPGRADE")
{
    for (const char *p : protos) for (const Mode &md : modes) CHECK_CHILD_OK(std::string("line-upgrade-") + p + "-" + md.name);
}

// an LLC's lower interface carries full-line data that is not a CPU Store: an L1's write-back (PutM with its line)
// installs its bytes, and the next reader gets them
CHILD("llc-writeback")
{
    prepare();
    Cfg cfg("MultiCoreSystem_Grid");
    Stub mem(100), net(10);
    auto *llc = new CacheControllerDirectory(cfg.getSubMap("llc_controller"), &mem, &net, "test");
    llc->setLogRole(Logger::Role::LLC);
    auto serveMemory = [&]() {
        for (auto &m : mem.tx) if (m.kind == Message::K_MEM_READ && !m.to.empty() && m.to[0] == 100)
        { Message f(m.msg_id, m.addr, 1, 0, m.owner); f.from = 100; f.to = {10}; f.kind = Message::K_FILL; f.copy(FP.data(), 64); mem.deliver(f); m.to.clear(); }
    };
    Message g(100, A, 1, MSIDirectory::REQUEST_TYPE_GETM, 0); g.from = 0; g.to = {10}; g.kind = Message::K_GETM;
    net.deliver(g);
    for (int k = 0; k < 6; k++) { cycles(llc, 4); serveMemory(); }
    cycles(llc, 20);
    CHECK_MSG(same(net.lastData(A), FP), "the LLC's data to the writer " + bytes(net.lastData(A)));
    Message put(101, A, 1, MSIDirectory::REQUEST_TYPE_PUTM, 0); put.from = 0; put.to = {10}; put.kind = Message::K_PUTM; put.copy(SP.data(), 64);
    net.deliver(put);
    cycles(llc, 30);
    Message r(102, A, 1, MSIDirectory::REQUEST_TYPE_GETS, 1); r.from = 1; r.to = {10}; r.kind = Message::K_GETS;
    net.deliver(r);
    for (int k = 0; k < 6; k++) { cycles(llc, 4); serveMemory(); }
    cycles(llc, 20);
    CHECK_MSG(same(net.lastData(A), SP), "the reader after the write-back got " + bytes(net.lastData(A)));
}
TEST("DATA-LINE-LLC-WRITEBACK") { CHECK_CHILD_OK("llc-writeback"); }

// the public API: ExternalCPU::addRequest's WRITE with its default size (64) and no kind, on the gem5 preset (snoop
// MSI), then a READ of the line: the read returns the Store's 64 bytes; each request completes once
namespace
{
    struct Sink
    {
        std::map<uint64_t, int> done; std::vector<uint8_t> read; uint64_t read_id = 0;
        void cb(uint64_t id, uint64_t, uint64_t, RequestType, uint8_t *data) { done[id]++; if (id == read_id && data) read.assign(data, data + 64); }
        void commit(uint64_t, uint64_t) {}
        void inv(uint64_t) {}
    };
}
CHILD("api-default-write")
{
    std::vector<std::string> cl = {"workload_path(s)=test/network/out/"};
    CacheSim sim("MultiCoreSystem", cl, false, "MultiCoreSystem_gem5");
    CHECK(!ExternalCPU::getExtCPUs()->empty());
    ExternalCPU *cpu = ExternalCPU::getExtCPUs()->begin()->second;
    Sink s;
    cpu->registerCPUCallback(new CPUCallback<Sink, uint64_t, uint64_t, uint64_t, RequestType, uint8_t *>(&s, &Sink::cb));
    cpu->registerCommitCallback(new CPUCallback<Sink, uint64_t, uint64_t>(&s, &Sink::commit));
    cpu->registerInvalidateCallback(new CPUCallback<Sink, uint64_t>(&s, &Sink::inv));
    for (int i = 0; i < 4; i++) sim.step();
    Line b = SP;
    uint64_t w = cpu->addRequest(0x400000, WRITE, b.data());      // the default size: a whole line
    for (int i = 0; i < 200000 && !s.done.count(w); i++) sim.step();
    CHECK_EQ(s.done[w], 1);
    s.read_id = cpu->addRequest(0x400000, READ, NULL, 8);
    for (int i = 0; i < 200000 && !s.done.count(s.read_id); i++) sim.step();
    CHECK_EQ(s.done[s.read_id], 1);
    CHECK_MSG(s.read.size() == 64 && std::equal(s.read.begin(), s.read.end(), SP.begin()), "the READ after the default-size WRITE");
}
TEST("DATA-LINE-API") { CHECK_CHILD_OK("api-default-write"); }

// the invariant failure: a fill admitted for a line that is neither in the array nor in an MSHR must stop the run
// with a nonzero status and a diagnostic (it exited 0)
namespace
{
    struct Probe : CacheControllerDirectory
    {
        using CacheControllerDirectory::CacheControllerDirectory;
        void fillAbsent()
        {
            Message msg(7, A, 1, 0, 0); msg.source = Message::Source::UPPER_INTERCONNECT; msg.from = 10; msg.copy(FP.data(), 64);
            GenericCacheLine line; line.valid = true; line.state = 0;
            uint8_t *blob = new uint8_t[sizeof(Message) + sizeof(GenericCacheLine)];
            new (blob) Message(msg);
            new (blob + sizeof(Message)) GenericCacheLine(line);
            action_functions[ControllerAction::Type::WRITE_CACHE_LINE_DATA](blob);
        }
    };
}
CHILD("fill-absent")
{
    prepare();
    Cfg cfg("MultiCoreSystem_Grid");
    Stub up(0), cpu(-1);
    Probe p(cfg.getSubMap("cache_controller", 0), &up, &cpu, "test");
    p.fillAbsent();
}
TEST("DATA-LINE-FATAL")
{
    octest::ChildResult r = octest::runChild("fill-absent");
    CHECK_MSG(r.status != 0, "status " + octest::str(r.status) + ": " + r.out);
    CHECK_MSG(r.out.find("found no line to install into") != std::string::npos, "diagnostic: " + r.out);
    CHECK(!r.done());
}
