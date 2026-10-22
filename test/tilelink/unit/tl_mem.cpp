// TL-MEM-*: memory over TileLink (docs/TileLink_Plan.md s5.3 I-B, step T8). A TLMemMaster (the LLC
// bank's memory side) pushes the contract's tagged K_MEM_READ (Get) / K_MEM_WRITE (PutFullData)
// through a TL-UH crossbar to a TLMemSlave over a real MainMemoryController or MCsimInterface,
// which answer untagged K_FILL / K_MEM_WACK; the slave supplies the D header. Every link carries a
// monitor in fatal mode, so a non-conforming beat ends the run with status 2. The read-after-write
// detector (header/MemHazard.h) is switched on for this binary (OCTOPUS_MEM_HAZARD=1 unless the
// environment says otherwise) and every test checks its own NET / MC deltas.
#include "tl_common.h"
#include "TLMemSlave.h"
#include "MainMemoryController.h"
#include "MCsimInterface.h"
#include "MemHazard.h"
#include <cstring>
#include <map>
#include <memory>
#include <set>
using namespace octopus;
using namespace tltest;

// before main, so before the detector's first on(): the hooks count in every test of this binary
static int s_hazard_env = (setenv("OCTOPUS_MEM_HAZARD", "1", 0), 0);

namespace
{
    // MCsimInterface with its address map and read count visible
    struct MCsimProbe : public MCsimInterface
    {
        using MCsimInterface::MCsimInterface;
        uint64_t mc(uint64_t a) const { return mcAddr(a); }
        uint64_t reads() const { return m_read_count; }
        size_t unperformed() const { return m_unperformed.size(); }
    };

    struct MemSys
    {
        TileLinkInterconnect *ic = nullptr;
        std::map<int, TLMemMaster *> masters;
        std::map<int, TLMemSlave *> slaves;
        std::map<int, ClockedObj *> mcs;
        std::map<int, MCsimProbe *> mcsim;
        std::map<int, MainMemoryController *> basic;
        // ports: the banks' memory ports; mems: the controllers; routes: <slave>:<base>:<size>
        MemSys(bool use_mcsim, std::vector<int> ports = {110}, std::vector<int> mems = {100},
               std::vector<std::pair<std::string, int>> ints = {}, std::vector<std::string> routes = {},
               int mc_queue = 32, int latency = 50, int ilv_shift = 0)
        {
            Configurable::ParametersMap m;
            auto put = [&](const Parameter &p) { m.erase(p.name); m.insert({p.name, p}); };
            std::vector<int> all = ports; all.insert(all.end(), mems.begin(), mems.end());
            put(Parameter("master_ids", Parameter::Vector_Integer, ports));
            put(Parameter("slave_ids", Parameter::Vector_Integer, mems));
            put(Parameter("memory_ids", Parameter::Vector_Integer, all));
            put(Parameter("level", Parameter::String, std::string("UH")));
            put(Parameter("tl_monitor", Parameter::Integer, 1));
            put(Parameter("tl_stats", Parameter::Integer, 0));
            put(Parameter("o", Parameter::Integer, 5));
            if (!routes.empty()) put(Parameter("routes", Parameter::Vector_String, routes));
            for (auto &kv : ints) put(Parameter(kv.first, Parameter::Integer, kv.second));
            ic = new TileLinkInterconnect(m, "test");
            ic->elaborate();
            for (int p : ports) masters[p] = new TLMemMaster(ic->port(p));
            for (int id : mems)
            {
                slaves[id] = new TLMemSlave(ic->port(id));
                if (use_mcsim)
                {
                    MCsimProbe *x = new MCsimProbe(slaves[id], id, ports[0], 4, 64, 100, "FRFCFS", (int)mems.size(), ilv_shift);
                    mcsim[id] = x; mcs[id] = x;
                }
                else
                {
                    Configurable::ParametersMap mm;
                    auto mput = [&](const Parameter &p) { mm.erase(p.name); mm.insert({p.name, p}); };
                    mput(Parameter("m_id", Parameter::Integer, id));
                    mput(Parameter("m_memory_latency", Parameter::Integer, latency));
                    mput(Parameter("processing_queue_size", Parameter::Integer, mc_queue));
                    MainMemoryController *x = new MainMemoryController(mm, slaves[id], "test");
                    basic[id] = x; mcs[id] = x;
                }
            }
        }
        // one core cycle: the controllers (rising edge), then the kernel (falling edge)
        void step() { for (auto &kv : mcs) kv.second->cycleProcess(); ic->step(); }
        void run(int n) { for (int i = 0; i < n; i++) step(); }
    };

    Message memRead(uint64_t id, uint64_t addr, uint32_t tag, int to = 100)
    {
        Message g(id, addr);
        g.kind = Message::K_MEM_READ; g.tl.channel = 1; g.tl.opcode = (uint8_t)tl::OpA::Get; g.tl.size = 6; g.tl.source = tag;
        g.to = {(uint16_t)to};
        return g;
    }
    Message memWrite(uint64_t id, uint64_t addr, uint32_t tag, const std::vector<uint8_t> &bytes, int to = 100)
    {
        Message p(id, addr);
        p.kind = Message::K_MEM_WRITE; p.tl.channel = 1; p.tl.opcode = (uint8_t)tl::OpA::PutFullData; p.tl.size = 6; p.tl.source = tag;
        p.copy(bytes.data(), (uint16_t)bytes.size());
        p.to = {(uint16_t)to};
        return p;
    }
    // the memory controller of a line under mem_interleave_shift (spec s25.4)
    int memOf(uint64_t addr, const std::vector<int> &mems, int shift) { return mems[((addr / 64) >> shift) % mems.size()]; }
    // routes for a bounded test region: [0, span) cut into 2^shift-line pages, page p -> mems[p mod M]
    std::vector<std::string> interleavedRanges(const std::vector<int> &mems, int shift, uint64_t span)
    {
        std::vector<std::string> r;
        uint64_t page = uint64_t(64) << shift;
        for (uint64_t b = 0, p = 0; b < span; b += page, p++)
            r.push_back(std::to_string(mems[p % mems.size()]) + ":" + std::to_string(b) + ":" + std::to_string(page));
        return r;
    }

    // pushMessage takes an lvalue (the port stamps `from`)
    bool push(TLMemMaster *m, Message msg) { return m->pushMessage(msg); }

    struct HazardDelta
    {
        MemHazard::Counts c0 = MemHazard::counts();
        MemHazard::Counts now() const
        {
            MemHazard::Counts c = MemHazard::counts();
            return MemHazard::Counts{c.reads - c0.reads, c.writes - c0.writes, c.net - c0.net, c.mc - c0.mc, c.link - c0.link,
                                     c.forwarded - c0.forwarded, c.mc_forwarded - c0.mc_forwarded, c.llc_forwarded - c0.llc_forwarded};
        }
    };
}

// a Get and a PutFullData through the crossbar to each memory model: the D header per the contract
// (opcode, size, the requester's own tag restored), the fill's 64 bytes as the controller produced
// them, the acknowledgement without data and with msg_id 0 (docs/NoC_Memory_Plan.md M1)
static void roundtrip(bool use_mcsim)
{
    CHECK(MemHazard::on());
    HazardDelta hz;
    MemSys s(use_mcsim);
    TLMemMaster *m = s.masters[110];
    TLMemSlave *sl = s.slaves[100];
    Message g = memRead(41, 0x1008, 7);
    g.owner = 2;
    CHECK(m->pushMessage(g));
    CHECK(push(m, memWrite(42, 0x2000, 9, pattern(0x80))));
    Message resp;
    std::vector<Message> got;
    int fill_at = -1;
    for (int t = 0; t < 2000 && got.size() < 2; t++)
    {
        s.step();
        if (m->peekMessage(&resp)) { if (resp.kind == Message::K_FILL) fill_at = t; got.push_back(resp); m->popFrontMessage(); }
    }
    CHECK_EQ(got.size(), (size_t)2);
    const Message *fill = nullptr, *ack = nullptr;
    for (auto &x : got) (x.kind == Message::K_FILL ? fill : ack) = &x;
    CHECK(fill != nullptr); CHECK(ack != nullptr);
    CHECK_EQ((int)fill->tl.channel, 4); CHECK_EQ((int)fill->tl.opcode, (int)tl::OpD::AccessAckData); CHECK_EQ((int)fill->tl.size, 6);
    CHECK_EQ(fill->tl.source, (uint32_t)7); CHECK_EQ(fill->msg_id, (uint64_t)41); CHECK_EQ(fill->addr, (uint64_t)0x1008);
    CHECK_EQ((int)fill->to.size(), 1); CHECK_EQ((int)fill->to[0], 110);   // the bank that asked (msg.from at memory)
    CHECK(fill->data != NULL); CHECK_EQ((int)fill->data_size, 64);
    if (use_mcsim)
    {
        uint64_t v = 0; std::memcpy(&v, fill->data, 8);
        CHECK_EQ(v, (uint64_t)0);                                   // MCsimInterface's fill carries its read index
        CHECK_EQ(s.mcsim[100]->reads(), (uint64_t)1);
        CHECK_MSG(fill_at > 20, "an MCsim read took " + std::to_string(fill_at) + " cycles");   // DRAM, not forwarded
    }
    else
    {
        CHECK(sameBytes(fill->data, std::vector<uint8_t>(64, 0)));  // MainMemoryController's fill is zeros
        CHECK_MSG(fill_at >= 50, "a basic-controller read took " + std::to_string(fill_at) + " cycles, latency 50");
    }
    CHECK_EQ((int)ack->tl.channel, 4); CHECK_EQ((int)ack->tl.opcode, (int)tl::OpD::AccessAck); CHECK_EQ((int)ack->tl.size, 6);
    CHECK_EQ(ack->tl.source, (uint32_t)9); CHECK_EQ(ack->msg_id, (uint64_t)0); CHECK(ack->data == NULL);
    CHECK_EQ(ack->addr, (uint64_t)0x2000);
    CHECK_EQ(sl->gets(), (uint64_t)1); CHECK_EQ(sl->puts(), (uint64_t)1); CHECK_EQ(sl->answered(), (uint64_t)2);
    CHECK_EQ(sl->outstanding(), (size_t)0);
    CHECK_EQ(m->port()->idsInUse(1), 0);
    CHECK_EQ(s.ic->monitorViolations(), (uint64_t)0);
    CHECK_EQ(s.ic->inFlight(), (size_t)0);
    MemHazard::Counts d = hz.now();
    CHECK_EQ(d.reads, (uint64_t)1); CHECK_EQ(d.net, (uint64_t)0); CHECK_EQ(d.mc, (uint64_t)0);
}

TEST("TL-MEM-ROUNDTRIP-BASIC") { roundtrip(false); }
TEST("TL-MEM-ROUNDTRIP-MCSIM") { roundtrip(true); }

// H2 (docs/NoC_Memory_Plan.md M1): a read of a line whose write MCsim has accepted and not yet
// performed is answered from that write (no DRAM read), and the detector sees no overtake
TEST("TL-MEM-H2-FORWARD")
{
    HazardDelta hz;
    MemSys s(true);
    TLMemMaster *m = s.masters[110];
    CHECK(push(m, memWrite(50, 0x3000, 1, pattern(0x21))));
    CHECK(push(m, memRead(51, 0x3010, 2)));      // same line, sent after the write
    CHECK(push(m, memRead(52, 0x5000, 3)));      // another line: DRAM
    std::map<uint64_t, int> at;
    bool ack = false;
    Message resp;
    for (int t = 0; t < 3000 && (at.size() < 2 || !ack); t++)
    {
        s.step();
        if (m->peekMessage(&resp))
        {
            if (resp.kind == Message::K_FILL) at[resp.msg_id] = t; else if (resp.kind == Message::K_MEM_WACK) ack = true;
            m->popFrontMessage();
        }
    }
    CHECK(ack); CHECK_EQ(at.size(), (size_t)2);
    MemHazard::Counts d = hz.now();
    CHECK_EQ(d.mc_forwarded, (uint64_t)1);                           // read 51 answered from write 50
    CHECK_EQ(d.net, (uint64_t)0); CHECK_EQ(d.mc, (uint64_t)0);
    CHECK_MSG(at[51] < at[52], "the forwarded read (" + std::to_string(at[51]) + ") is not ahead of the DRAM read (" + std::to_string(at[52]) + ")");
    CHECK_EQ(s.mcsim[100]->reads(), (uint64_t)2);                    // MCsimInterface counts both fills
    CHECK_EQ(s.ic->monitorViolations(), (uint64_t)0);
    CHECK_EQ(s.slaves[100]->outstanding(), (size_t)0);
}

// back-pressure with tiny queues: every TLPort queue 1 deep, the basic controller's queue 2, the
// master's sink slow. Requests wait on the wire and in the master's TX (refused pushes), responses
// in the controller's ordered output queue; every request is answered once, with its own tag
static void backpressure(bool use_mcsim)
{
    HazardDelta hz;
    MemSys s(use_mcsim, {110}, {100}, {{"rx_depth", 1}, {"tx_depth", 1}, {"o", 4}}, {}, /*mc_queue=*/2, /*latency=*/20);
    TLMemMaster *m = s.masters[110];
    const int N = 48;
    std::vector<Message> todo;
    for (int k = 0; k < N; k++)
    {
        uint64_t addr = 0x10000 + 64 * (uint64_t)(k % 12);       // 12 lines, reads and writes of each interleaved
        todo.push_back(k % 3 == 2 ? memWrite(1000 + k, addr, (uint32_t)k, pattern(k)) : memRead(1000 + k, addr, (uint32_t)k));
    }
    size_t next = 0;
    std::multiset<uint32_t> tags;
    int refused = 0, reads = 0, acks = 0;
    Message resp;
    for (int t = 0; t < 40000 && (int)tags.size() < N; t++)
    {
        s.step();
        if (next < todo.size())
        {
            // a push is refused when the TX queue or the source pool (o = 4: 16) is full
            if (m->txFreeSlotsCh(1) > 0 && m->txFreeIdsCh(1) > 0) { CHECK(m->pushMessage(todo[next])); next++; }
            else refused++;
        }
        if (t % 7 == 0 && m->peekMessage(&resp))                   // a slow sink: D backs up into memory
        {
            tags.insert(resp.tl.source);
            (resp.kind == Message::K_FILL ? reads : acks)++;
            m->popFrontMessage();
        }
    }
    CHECK_EQ((int)tags.size(), N);
    CHECK_EQ((int)std::set<uint32_t>(tags.begin(), tags.end()).size(), N);   // each tag exactly once
    CHECK_EQ(reads, 32); CHECK_EQ(acks, 16);
    CHECK_MSG(refused > 0, "no back-pressure reached the master");
    CHECK(s.ic->kernel().link("L110")->chan(tl::Channel::D).stats().stall > 0);   // D held at the master
    CHECK(s.ic->kernel().link("S100")->chan(tl::Channel::A).stats().stall > 0 || use_mcsim);   // A held at memory (basic: queue 2)
    CHECK_EQ(s.slaves[100]->outstanding(), (size_t)0);
    CHECK_EQ(s.ic->monitorViolations(), (uint64_t)0);
    MemHazard::Counts d = hz.now();
    CHECK_EQ(d.net, (uint64_t)0); CHECK_EQ(d.mc, (uint64_t)0);
}

TEST("TL-MEM-BACKPRESSURE-BASIC") { backpressure(false); }
TEST("TL-MEM-BACKPRESSURE-MCSIM") { backpressure(true); }

// two banks' memory ports and two controllers, 4 KiB pages interleaved (mem_interleave_shift 6,
// as MultiCoreSystem_Grid2B2M): every line reaches the controller memOf names (the wire's route
// equals Message::to, else TLPort is fatal), the per-controller counts equal the formula, every
// fill returns to the port that asked, and MCsim sees each controller's lines densely (the
// controller-select bit removed, spec s25.4)
static void interleave(bool use_mcsim, bool interleave_form)
{
    HazardDelta hz;
    const std::vector<int> ports = {110, 111}, mems = {100, 101};
    const int shift = 6;
    const uint64_t span = 64 * 1024;
    // the crossbar's interleave form, as MultiCoreSystem_Mesh::memoryRoutes writes it, or ranges over the test region
    std::vector<std::string> routes = interleave_form ? std::vector<std::string>{"100:interleave:6:2:0", "101:interleave:6:2:1"}
                                                       : interleavedRanges(mems, shift, span);
    MemSys s(use_mcsim, ports, mems, {{"o", 6}}, routes, 32, 30, shift);
    std::map<int, int> expect_reads, expect_writes;
    std::vector<std::pair<int, Message>> todo;
    for (int k = 0; k < 400; k++)
    {
        uint64_t addr = (uint64_t)((k * 37) % 1024) * 64;          // lines spread over the 64 KiB region
        int port = ports[k % 2], mc = memOf(addr, mems, shift);
        if (k % 4 == 3) { todo.push_back({port, memWrite(2000 + k, addr, (uint32_t)k, pattern(k), mc)}); expect_writes[mc]++; }
        else            { todo.push_back({port, memRead(2000 + k, addr, (uint32_t)k, mc)}); expect_reads[mc]++; }
    }
    std::map<int, std::set<uint32_t>> back;
    size_t next = 0;
    Message resp;
    int done = 0;
    for (int t = 0; t < 60000 && done < (int)todo.size(); t++)
    {
        s.step();
        for (int burst = 0; burst < 2 && next < todo.size(); burst++)
        {
            TLMemMaster *m = s.masters[todo[next].first];
            if (m->txFreeSlotsCh(1) <= 0 || m->txFreeIdsCh(1) <= 0) break;
            CHECK(m->pushMessage(todo[next].second)); next++;
        }
        for (int p : ports)
            if (s.masters[p]->peekMessage(&resp))
            {
                CHECK(resp.to.size() == 1 && resp.to[0] == p);
                CHECK_MSG(back[p].insert(resp.tl.source).second, "tag " + std::to_string(resp.tl.source) + " answered twice");
                s.masters[p]->popFrontMessage();
                done++;
            }
    }
    CHECK_EQ(done, (int)todo.size());
    for (int mc : mems)
    {
        CHECK_EQ((int)s.slaves[mc]->gets(), expect_reads[mc]);
        CHECK_EQ((int)s.slaves[mc]->puts(), expect_writes[mc]);
        CHECK_EQ(s.slaves[mc]->outstanding(), (size_t)0);
    }
    CHECK(expect_reads[100] > 100 && expect_reads[101] > 100);       // both controllers carry load
    for (int p : ports) CHECK_EQ((int)back[p].size(), 200);
    if (use_mcsim)
    {
        // controller-select bit = line bit 6 (address bit 12): removed, the pages of each controller packed
        CHECK_EQ(s.mcsim[100]->mc(0x0000), (uint64_t)0x0000);
        CHECK_EQ(s.mcsim[101]->mc(0x1000), (uint64_t)0x0000);
        CHECK_EQ(s.mcsim[100]->mc(0x2040), (uint64_t)0x1040);
        CHECK_EQ(s.mcsim[101]->mc(0x3fc0), (uint64_t)0x1fc0);
        for (int mc : mems) CHECK_EQ((int)s.mcsim[mc]->reads(), expect_reads[mc]);
    }
    CHECK_EQ(s.ic->monitorViolations(), (uint64_t)0);
    MemHazard::Counts d = hz.now();
    CHECK_EQ(d.net, (uint64_t)0); CHECK_EQ(d.mc, (uint64_t)0);
    CHECK_EQ((int)d.reads, expect_reads[100] + expect_reads[101]);
}

TEST("TL-MEM-INTERLEAVE-BASIC") { interleave(false, false); }
TEST("TL-MEM-INTERLEAVE-MCSIM") { interleave(true, false); }
TEST("TL-MEM-INTERLEAVE-ROUTES") { interleave(true, true); }    // the Xbar's link:interleave:<shift>:<count>:<index> form

// the detector is live on this path (H3): two ports, the write of a line sent first from one and
// its read in the same cycle from the other; the crossbar grants the read's input first, so the
// read reaches memory ahead of the older write: NET counts it (an artificial case: with banking a
// line has one home, so one port, and a TL-UH port's A queue is FIFO)
TEST("TL-MEM-HAZARD-LIVE")
{
    const char *e = std::getenv("OCTOPUS_MEM_HAZARD");
    if (e && std::string(e) == "fatal") return;                      // =fatal would end the run at the hazard
    HazardDelta hz;
    MemSys s(false, {110, 111}, {100}, {{"w", 8}});
    CHECK(push(s.masters[111], memWrite(60, 0x7000, 1, pattern(3))));
    CHECK(push(s.masters[110], memRead(61, 0x7000, 2)));
    Message resp;
    int done = 0;
    for (int t = 0; t < 2000 && done < 2; t++)
    {
        s.step();
        for (int p : {110, 111}) if (s.masters[p]->peekMessage(&resp)) { s.masters[p]->popFrontMessage(); done++; }
    }
    CHECK_EQ(done, 2);
    MemHazard::Counts d = hz.now();
    CHECK_EQ(d.net, (uint64_t)1);
    CHECK_EQ(d.link, (uint64_t)1);                                   // overtaken in the crossbar, before delivery
    CHECK_EQ(d.mc, (uint64_t)1);                                     // and so completed ahead of it too
    CHECK_EQ(s.ic->monitorViolations(), (uint64_t)0);
}
