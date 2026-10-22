// Shared fixtures of the TileLink adapter tests (docs/TileLink_Plan.md s5.2): a crossbar system built
// from the shipped interconnect defaults (configuration/Interconnect/TileLink.csv) with overrides, and
// the messages of the echo stand-in.
#ifndef OCTOPUS_TL_COMMON_H
#define OCTOPUS_TL_COMMON_H
#include "testing.h"
#include "TileLinkInterconnect.h"
#include "TLPort.h"
#include "Logger.h"
#include "tilelink/Types.h"
#include "tilelink/agents/Xbar.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace tltest
{
    using namespace octopus;

    struct Sys
    {
        TileLinkInterconnect *ic = nullptr;
        // masters (the L1 ids) and one slave, endpoint 10
        Sys(std::vector<std::pair<std::string, int>> ints, std::vector<int> masters = {0}, int monitor = 1, std::string level = "C")
        {
            Configurable::ParametersMap m;
            auto put = [&](const Parameter &p) { m.erase(p.name); m.insert({p.name, p}); };
            put(Parameter("master_ids", Parameter::Vector_Integer, masters));
            put(Parameter("slave_ids", Parameter::Vector_Integer, std::vector<int>{10}));
            put(Parameter("level", Parameter::String, level));
            put(Parameter("tl_monitor", Parameter::Integer, monitor));
            put(Parameter("tl_stats", Parameter::Integer, 0));
            for (auto &kv : ints) put(Parameter(kv.first, Parameter::Integer, kv.second));
            ic = new TileLinkInterconnect(m, "test");
            ic->elaborate();
        }
        ~Sys() { delete ic; }
        TLPort *port(int id) { return ic->port(id); }
        void run(int n) { for (int i = 0; i < n; i++) ic->step(); }
        bool runUntil(const std::function<bool()> &f, int max)
        {
            for (int i = 0; i < max; i++) { ic->step(); if (f()) return true; }
            return false;
        }
    };

    inline std::vector<uint8_t> pattern(int seed) { std::vector<uint8_t> v(64); for (int i = 0; i < 64; i++) v[i] = (uint8_t)(seed + 3 * i); return v; }
    inline bool sameBytes(const uint8_t *a, const std::vector<uint8_t> &b) { return a && std::equal(b.begin(), b.end(), a); }

    // a Get of a line on A from master 0 to the LLC 10; `tag` is the requester's own source value
    inline Message get(uint64_t id, uint64_t addr, uint32_t tag)
    {
        Message g(id, addr);
        g.kind = Message::K_GETS; g.tl.channel = 1; g.tl.opcode = (uint8_t)tl::OpA::Get; g.tl.size = 6; g.tl.source = tag;
        g.to = {10};
        return g;
    }
    inline Message put(uint64_t id, uint64_t addr, uint32_t tag, const std::vector<uint8_t> &bytes)
    {
        Message p(id, addr);
        p.kind = Message::K_PUTM; p.tl.channel = 1; p.tl.opcode = (uint8_t)tl::OpA::PutFullData; p.tl.size = 6; p.tl.source = tag;
        p.copy(bytes.data(), (uint16_t)bytes.size());
        p.to = {10};
        return p;
    }
    // the slave's answer on D: AccessAckData with bytes, AccessAck without
    inline Message answer(const Message &req, uint8_t kind, const std::vector<uint8_t> &bytes)
    {
        Message d(req.msg_id, req.addr);
        d.kind = kind; d.tl.channel = 4; d.tl.size = req.tl.size; d.tl.source = req.tl.source;
        d.tl.opcode = (uint8_t)(bytes.empty() ? tl::OpD::AccessAck : tl::OpD::AccessAckData);
        if (!bytes.empty()) d.copy(bytes.data(), (uint16_t)bytes.size());
        d.to = {req.from};
        return d;
    }

    // pushMessage takes an lvalue (it stamps `from`)
    inline bool send(TLPort *p, Message m) { return p->pushMessage(m); }

    // the raw event trace (docs/Trace.md): 8-byte magic, 32-byte records, chunk index, trailer
    // (index offset u64, chunk count u64, magic)
    struct TraceRec { uint64_t cycle, msg_id, addr; uint8_t resource, phase, kind, core; uint16_t comp, flags; };
    inline std::string traceFile() { const char *f = std::getenv("OCTOPUS_TRACE"); return f ? f : ""; }
    inline std::vector<TraceRec> readTrace(const std::string &path)
    {
        std::vector<TraceRec> out;
        FILE *f = std::fopen(path.c_str(), "rb");
        if (!f) return out;
        std::fseek(f, -24, SEEK_END);
        uint64_t idx = 0; if (std::fread(&idx, 8, 1, f) != 1) { std::fclose(f); return out; }
        std::fseek(f, 8, SEEK_SET);
        size_t n = (size_t)((idx - 8) / sizeof(TraceRec));
        out.resize(n);
        if (n && std::fread(out.data(), sizeof(TraceRec), n, f) != n) out.clear();
        std::fclose(f);
        return out;
    }
}
#endif
