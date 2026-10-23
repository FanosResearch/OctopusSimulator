/*
 * File  :      CoherentManager.cpp
 * The inclusive TL-C manager (plan §3.5) executing the manager table (docs/TileLink_TLC_Tables.md
 * §3). See CoherentManager.h for the engine rules around a cell.
 */
#include "tilelink/Atomic.h"
#include "tilelink/agents/CoherentManager.h"
#include "tilelink/CoherenceCheck.h"
#include "tilelink/Kernel.h"
#include "tilelink/agents/CacheMaster.h"
#include "tilelink/agents/Xbar.h"
#include "../coh/BlockData.h"
#include <algorithm>
#include <iostream>

namespace octopus { namespace tl
{
    struct CoherentManager::Entry
    {
        uint64_t       block = 0;
        int            state = 0;
        std::set<int>  sharers;
        int            owner = -1;
        unsigned       probes = 0;          // Probes sent and not answered (§1.2 rule 4)
        coh::BlockData data;                // the LLC copy
        bool           has_data = false;
        Rx             req;                 // the parked request (§1.2 rule 3)
        bool           parked = false;
        int            sink_port = -1;      // the sink reserved for the Grant (plan §5.2)
        uint64_t       sink = 0;
        bool           sink_held = false;
        uint64_t       lru = 0;
    };

    // the atomics (plan §1.3, Tables 23 and 25): tl::atomicOp in tilelink/Atomic.h, shared with ValueCheck
    static bool isPut(unsigned op) { return op == unsigned(OpA::PutFullData) || op == unsigned(OpA::PutPartialData); }
    static bool laneActive(const Msg &m, uint64_t x)
    {
        LanePos p = lanePos(m, x);
        size_t i = size_t(p.beat) * m.w + p.lane;
        return i < m.mask.size() && m.mask[i] != 0;
    }

    // ------------------------------------------------------------------ construction
    CoherentManager::CoherentManager(const std::string &name, const Config &p) : Agent(name)
    {
        std::string proto = p.get<std::string>("protocol", "MESI");
        if (proto != "MESI" && proto != "MSI") throw ConfigError(name + ": protocol must be MESI or MSI, got " + proto);
        std::string table = p.has("table") ? p.get<std::string>("table") : findTable(proto == "MSI" ? "TLC_MSI_LLC.csv" : "TLC_MESI_LLC.csv");
        m_policy.reset(new coh::ManagerPolicy(coh::Table::load(table)));
        int bb = p.get<int>("block_bytes", 64);
        if (bb < 8 || (bb & (bb - 1))) throw ConfigError(name + ": block_bytes must be a power of two >= 8");
        m_bb = unsigned(bb);
        m_lg = 0;
        while ((1u << m_lg) < m_bb) m_lg++;
        int sets = p.get<int>("sets", 1024), ways = p.get<int>("ways", 8);
        if (sets <= 0 || ways <= 0) throw ConfigError(name + ": sets and ways must be >= 1");
        m_sets = unsigned(sets); m_ways = unsigned(ways);
        if (p.has("addr_size")) m_region = AddrRegion{uint64_t(unsigned(p.get<int>("addr_base", 0))), uint64_t(unsigned(p.get<int>("addr_size")))};
        m_debug = p.get<int>("debug", 0) != 0;
    }

    CoherentManager::~CoherentManager() {}

    Link *CoherentManager::outer() const
    {
        for (auto &lr : m_links) if (lr.role == Role::Master) return lr.link;
        return nullptr;
    }

    int CoherentManager::declaredMaxSize(const Link &) const { return int(m_lg); }
    std::vector<AddrRegion> CoherentManager::declaredRegions(const Link &) const
    {
        if (m_region.size == 0) return {};
        return {m_region};
    }

    void CoherentManager::discoverClients(Link *l, int port, uint64_t base, int depth)
    {
        Agent *m = l->master();
        if (depth < 16)
        {
            if (Xbar *x = dynamic_cast<Xbar *>(m))
            {
                // §6.4: the Xbar packs its upstream links' sources at sourceBase(up) in one namespace.
                for (auto &lr : x->links())
                    if (lr.role == Role::Slave) discoverClients(lr.link, port, base + x->sourceBase(*lr.link), depth + 1);
                return;
            }
            if (dynamic_cast<TopologyAgent *>(m))
            {
                // A one-in-one-out adapter (Throttle, Buffer, WidthWidget) keeps the sources.
                Link *up = nullptr;
                unsigned ups = 0, downs = 0;
                for (auto &lr : m->links()) { if (lr.role == Role::Slave) { ups++; up = lr.link; } else downs++; }
                if (ups == 1 && downs == 1) { discoverClients(up, port, base, depth + 1); return; }
            }
        }
        Client c;
        c.port = port;
        c.base = base;
        c.count = l->params().numSources();
        c.name = l->name();
        m_clients.push_back(c);
    }

    void CoherentManager::reset(uint64_t)
    {
        m_ports.clear();
        m_clients.clear();
        for (auto &lr : m_links)
        {
            if (lr.role != Role::Slave) continue;
            Link *l = lr.link;
            if (l->params().maxSize() < m_lg)
                fatal("CoherentManager " + m_name + ": link " + l->name() + " (z=" + std::to_string(l->params().z) + ") cannot encode the block size");
            Port p;
            p.link = l;
            uint64_t sinks = l->params().i >= 10 ? 1024 : l->params().numSinks();
            for (uint64_t s = sinks; s-- > 0;) p.free_sinks.push_back(s);
            m_ports.push_back(p);
            discoverClients(l, int(m_ports.size()) - 1, 0, 0);
        }
        if (m_ports.empty()) fatal("CoherentManager " + m_name + ": no inner link (the manager is the slave of none)");
        Link *o = outer();
        if (!o) fatal("CoherentManager " + m_name + ": no outer link (the manager must be the master of one link toward memory)");
        if (o->params().maxSize() < m_lg) fatal("CoherentManager " + m_name + ": outer link " + o->name() + " cannot encode the block size");
        uint64_t n = o->params().o >= 10 ? 1024 : o->params().numSources();
        m_outer_free.clear();
        for (uint64_t s = n; s-- > 0;) m_outer_free.push_back(s);
    }

    ProgressHints CoherentManager::hints() const
    {
        ProgressHints h;
        uint64_t other = 1;
        unsigned clients = 0;
        for (auto &lr : m_links) if (lr.role == Role::Slave) clients++;
        if (kernel())
            for (auto &a : kernel()->agents())
                if (a.get() != this && !dynamic_cast<const CacheMaster *>(a.get()) && !dynamic_cast<const CoherentManager *>(a.get()) &&
                    !a->hints().is_master)
                    other = std::max(other, a->hints().latency);
        clients = std::max<unsigned>(std::max<unsigned>(clients, unsigned(m_clients.size())), 1);
        h.latency = 8 * (1 + other);
        h.queue_depth = 4 * clients;
        h.is_master = false;
        return h;
    }

    // ------------------------------------------------------------------ directory
    CoherentManager::Entry *CoherentManager::entry(uint64_t block)
    {
        auto it = m_dir.find(block);
        return it == m_dir.end() ? nullptr : it->second.get();
    }
    CoherentManager::Entry &CoherentManager::makeEntry(uint64_t block)
    {
        auto &p = m_dir[block];
        if (!p)
        {
            p.reset(new Entry());
            p->block = block;
            p->state = m_policy->stateN();
        }
        return *p;
    }
    int CoherentManager::stateOf(uint64_t block) const
    {
        auto it = m_dir.find(block);
        return it == m_dir.end() ? m_policy->stateN() : it->second->state;
    }
    unsigned CoherentManager::waysUsed(unsigned set) const
    {
        unsigned n = 0;
        for (auto &kv : m_dir) if (setOf(kv.first) == set && m_policy->table().dataValid(kv.second->state)) n++;
        return n;
    }
    int CoherentManager::clientOf(int port, uint64_t source) const
    {
        for (size_t c = 0; c < m_clients.size(); c++)
        {
            const Client &k = m_clients[c];
            if (k.port == port && source >= k.base && source - k.base < k.count) return int(c);
        }
        return -1;
    }
    coh::DirView CoherentManager::view(uint64_t block, int client) const
    {
        coh::DirView v;
        auto it = m_dir.find(block);
        if (it == m_dir.end()) return v;
        const Entry &e = *it->second;
        v.sender = client >= 0 && e.owner == client ? coh::Holder::Tip
                 : client >= 0 && e.sharers.count(client) ? coh::Holder::Branch : coh::Holder::None;
        v.holders = unsigned(e.sharers.size()) + (e.owner >= 0 ? 1 : 0);
        v.probes = e.probes;
        return v;
    }
    bool CoherentManager::isStall(int state, int event) const
    {
        for (int a : m_policy->table().transition(state, event).actions)
            if (m_policy->action(a).op == coh::MgrOp::Stall) return true;
        return false;
    }

    std::string CoherentManager::blockState(uint64_t address) const { return m_policy->table().stateName(stateOf(blockOf(address))); }
    bool CoherentManager::blockValue(uint64_t address, unsigned bytes, std::vector<uint8_t> &out) const
    {
        auto it = m_dir.find(blockOf(address));
        if (it == m_dir.end() || !it->second->has_data || it->second->data.mode != DataMode::Full) return false;
        out.clear();
        for (unsigned i = 0; i < bytes; i++) out.push_back(uint8_t(it->second->data.value(address + i)));
        return true;
    }
    int CoherentManager::owner(uint64_t address) const
    {
        auto it = m_dir.find(blockOf(address));
        return it == m_dir.end() ? -1 : it->second->owner;
    }
    std::vector<int> CoherentManager::sharers(uint64_t address) const
    {
        auto it = m_dir.find(blockOf(address));
        if (it == m_dir.end()) return {};
        return std::vector<int>(it->second->sharers.begin(), it->second->sharers.end());
    }
    std::string CoherentManager::clientName(int c) const { return c >= 0 && c < int(m_clients.size()) ? m_clients[size_t(c)].name : "?"; }

    bool CoherentManager::idle() const
    {
        if (!m_rxA.empty() || !m_rxC.empty() || !m_rxE.empty() || !m_rxD.empty()) return false;
        if (!m_outer_pending.empty() || !m_outerA.q.empty() || !m_outer_out.empty()) return false;
        for (auto &p : m_ports) if (!p.outB.q.empty() || !p.outD.q.empty()) return false;
        for (auto &kv : m_dir) if (!m_policy->table().stable(kv.second->state)) return false;
        return true;
    }

    void CoherentManager::reportStall(uint64_t block, int event, const Rx &rx, uint64_t cycle)
    {
        m_c.stalls++;
        int st = stateOf(block);
        auto &seen = m_stall_seen[rx.msg.get()];
        if (seen == st + 1) return;   // reported once per parked message and state
        seen = st + 1;
        if (!m_debug && !onTransition) return;
        const coh::Table &t = m_policy->table();
        coh::Transition tr;
        tr.cycle = cycle; tr.agent = m_name; tr.block = block; tr.from = t.stateName(st); tr.event = t.eventName(event);
        tr.cell = t.transition(st, event).text; tr.to = "Stall"; tr.stalled = true; tr.msg = rx.msg->describe(); tr.client = rx.client;
        report(tr);
    }

    void CoherentManager::report(const coh::Transition &t)
    {
        if (m_debug) std::cout << t.line() << std::endl;
        if (onTransition) onTransition(t);
    }

    // ------------------------------------------------------------------ messages
    static MsgPtr makeMsg(Kernel &k, int from, const MsgHeader &h, unsigned w)
    {
        MsgPtr m = Msg::make(h.channel, h.opcode, h.size, w, k.options().data_mode);
        static_cast<MsgHeader &>(*m) = h;
        m->layout(w, k.options().data_mode);
        m->id = k.newMsgId();
        m->from_agent = from;
        return m;
    }

    void CoherentManager::sendOuter(const MsgPtr &m, uint64_t block, bool evict)
    {
        OuterRec r;
        r.block = block;
        r.evict = evict;
        r.req = m;
        if (evict) m_h1[block]++;   // H1: later outer requests for the block wait for its AccessAck
        m_outer_pending.push_back({m, r});
    }

    void CoherentManager::promoteOuter()
    {
        // In order; the head takes a source when one is free (2^o, §6.4) and is held while an
        // eviction write of its block is unacknowledged (H1, TLC_Tables §1.2 rule 7).
        if (m_outer_pending.empty() || m_outer_free.empty()) return;
        auto &front = m_outer_pending.front();
        if (!front.second.evict && m_h1.count(front.second.block)) { m_c.h1_holds++; return; }
        uint64_t s = m_outer_free.back();
        m_outer_free.pop_back();
        front.first->source = s;
        m_outer_out[s] = front.second;
        m_outerA.q.push_back(front.first);
        m_outer_pending.pop_front();
    }

    // The bytes a checker compares, from a value function: Full: the runs of active bytes;
    // Stamp: each touched 8-byte group as the little-endian bytes of its stamp.
    template <typename Val, typename Active>
    static void reportBytes(CoherenceCheck *cc, bool store, unsigned agent, DataMode mode, uint64_t first, uint64_t last,
                            Val val, Active active, uint64_t cycle)
    {
        if (!cc || mode == DataMode::None) return;
        std::vector<uint8_t> run;
        uint64_t start = 0;
        auto flush = [&]()
        {
            if (run.empty()) return;
            if (store) cc->storePerformed(agent, start, run.data(), unsigned(run.size()), cycle);
            else cc->loadPerformed(agent, start, run.data(), unsigned(run.size()), cycle);
            run.clear();
        };
        if (mode == DataMode::Full)
        {
            for (uint64_t x = first; x <= last; x++)
            {
                if (!active(x)) { flush(); continue; }
                if (run.empty()) start = x;
                run.push_back(uint8_t(val(x)));
            }
            flush();
            return;
        }
        for (uint64_t g = first & ~uint64_t(7); g <= last; g += 8)
        {
            bool any = false;
            for (uint64_t x = std::max(g, first); x <= std::min(g + 7, last); x++) if (active(x)) any = true;
            if (!any) { flush(); continue; }
            uint64_t s = val(std::max(g, first));
            if (run.empty()) start = g;
            for (unsigned b = 0; b < 8; b++) run.push_back(uint8_t(s >> (8 * b)));
        }
        flush();
    }

    void CoherentManager::executeAccess(Entry &e, const Msg &req, Msg &resp, uint64_t cycle)
    {
        // TLC_Tables §3.3 Execute: a Put writes the LLC copy (masked bytes, §4.5), an atomic reads the
        // old value into the AccessAckData and writes the result. The LLC copy is the only copy here
        // (every holder was probed toN, plan §8 decision 8), so the effect is visible everywhere (§6.5).
        const DataMode mode = kernel()->options().data_mode;
        CoherenceCheck *cc = CoherenceCheck::of(*kernel());
        const uint64_t a0 = req.address, a1 = req.address + req.bytes() - 1;
        // poisoned bytes (§4.4) are not known values: never reported to the value check
        auto valid = [&](uint64_t x) { return !e.data.poisoned(x); };
        auto active = [&](uint64_t x) { return laneActive(req, x) && valid(x); };
        if (isPut(req.opcode))
        {
            e.data.fromMsg(req, req.address, a0, a1, true);   // a corrupt beat poisons the bytes it writes
            reportBytes(cc, true, unsigned(id()), mode, a0, a1, [&](uint64_t x) { return e.data.value(x); }, active, cycle);
            return;
        }
        // atomic. A poisoned old value or a corrupt operand makes the whole result and the returned old
        // value invalid (the plan's conservative rule: no clean result is derived from invalid data).
        bool bad = e.data.anyPoisoned(a0, a1);
        for (auto c : req.corrupt) if (c) bad = true;
        e.data.toMsg(resp, req.address, a0, a1);          // the old value
        reportBytes(cc, false, unsigned(id()), mode, a0, a1, [&](uint64_t x) { return e.data.value(x); }, valid, cycle);
        if (mode == DataMode::Full)
        {
            size_t n = size_t(req.bytes());
            std::vector<uint8_t> old(n), opnd(n), res(n);
            for (size_t i = 0; i < n; i++) { old[i] = uint8_t(e.data.value(a0 + i)); opnd[i] = uint8_t(laneValue(req, a0 + i)); }
            atomicOp(req.opcode, req.param, old.data(), opnd.data(), res.data(), n);
            for (size_t i = 0; i < n; i++) e.data.set(a0 + i, res[i]);
        }
        else if (mode == DataMode::Stamp)
            e.data.fromMsg(req, req.address, a0, a1, false);   // RamSlave: the operand's stamps are stored
        for (uint64_t x = a0; x <= a1; x++) e.data.setPoison(x, bad);
        if (bad) for (auto &c : resp.corrupt) c = 1;
        reportBytes(cc, true, unsigned(id()), mode, a0, a1, [&](uint64_t x) { return e.data.value(x); }, valid, cycle);
    }

    void CoherentManager::reportForwarded(const Msg &req, const Msg &resp, uint64_t cycle)
    {
        // N_u + AccessAck[Data] -> ForwardIn: the access was performed at memory, with no inner copy
        // anywhere and the block blocked until now, so it is reported at the response.
        if (resp.denied) return;
        const DataMode mode = kernel()->options().data_mode;
        CoherenceCheck *cc = CoherenceCheck::of(*kernel());
        const uint64_t a0 = req.address, a1 = req.address + req.bytes() - 1;
        auto all = [](uint64_t) { return true; };
        auto respVal = [&](uint64_t x) { return laneValue(resp, req.address, x); };
        if (req.opcode == unsigned(OpA::Get)) { reportBytes(cc, false, unsigned(id()), mode, a0, a1, respVal, all, cycle); return; }
        if (isPut(req.opcode))
        {
            reportBytes(cc, true, unsigned(id()), mode, a0, a1, [&](uint64_t x) { return laneValue(req, x); },
                        [&](uint64_t x) { return laneActive(req, x); }, cycle);
            return;
        }
        reportBytes(cc, false, unsigned(id()), mode, a0, a1, respVal, all, cycle);
        if (mode == DataMode::Full)
        {
            size_t n = size_t(req.bytes());
            std::vector<uint8_t> old(n), opnd(n), res(n);
            for (size_t i = 0; i < n; i++) { old[i] = uint8_t(respVal(a0 + i)); opnd[i] = uint8_t(laneValue(req, a0 + i)); }
            atomicOp(req.opcode, req.param, old.data(), opnd.data(), res.data(), n);
            reportBytes(cc, true, unsigned(id()), mode, a0, a1, [&](uint64_t x) { return uint64_t(res[size_t(x - a0)]); }, all, cycle);
        }
        else
            reportBytes(cc, true, unsigned(id()), mode, a0, a1, [&](uint64_t x) { return laneValue(req, x); }, all, cycle);
    }

    // ------------------------------------------------------------------ executing a cell
    void CoherentManager::execute(uint64_t block, int event, const Rx *rx, uint64_t cycle)
    {
        const coh::Table &t = m_policy->table();
        Entry &e = makeEntry(block);
        const int from = e.state;
        const coh::Cell &c = t.transition(from, event);
        coh::Transition tr;
        tr.cycle = cycle; tr.agent = m_name; tr.block = block;
        tr.from = t.stateName(from); tr.event = t.eventName(event); tr.cell = c.text; tr.to = t.stateName(c.next);
        if (rx) { tr.msg = rx->msg->describe(); tr.client = rx->client; }
        const Msg *consumed = rx ? rx->msg.get() : nullptr;
        const bool consumed_inner_a = rx && rx->port >= 0 && consumed->channel == Channel::A;
        const Rx *req = consumed_inner_a ? rx : (e.parked ? &e.req : nullptr);
        const int requester = req ? req->client : -1;
        const DataMode mode = kernel()->options().data_mode;
        CoherenceCheck *cc = CoherenceCheck::of(*kernel());
        auto fail = [&](const std::string &why) { fatal("CoherentManager " + m_name + ": " + why + ": " + tr.line()); };
        if (t.dataValid(c.next) && !e.has_data) { e.data.init(mode, block, m_bb); e.has_data = true; }   // the LLC line is allocated
        Link *o = outer();
        bool answered = false;
        for (int id : c.actions)
        {
            const coh::MgrAction &a = m_policy->action(id);
            switch (a.op)
            {
                case coh::MgrOp::Stall: fail("internal: a Stall cell was executed"); break;
                case coh::MgrOp::Fault: fail("table Fault cell (" + t.name() + ")"); break;
                case coh::MgrOp::GetOut:
                {
                    MsgPtr m = makeMsg(*kernel(), this->id(), coh::ManagerPolicy::outerGet(block, m_lg), o->params().w);
                    sendOuter(m, block, false);
                    m_c.outer_reads++;
                    break;
                }
                case coh::MgrOp::PutFullDataOut:
                {
                    MsgPtr m = makeMsg(*kernel(), this->id(), coh::ManagerPolicy::outerPut(block, m_lg), o->params().w);
                    e.data.wholeToMsg(*m);
                    // §1.2 rule 7: an eviction row's write (its next state is stable) is the port's.
                    bool evict = t.stable(c.next);
                    sendOuter(m, block, evict);
                    m_c.outer_writes++;
                    if (evict) m_c.evictions++;
                    break;
                }
                case coh::MgrOp::ForwardOut:
                {
                    if (!req) fail("ForwardOut with no request");
                    const Msg &r = *req->msg;
                    MsgPtr m = cloneMsg(r, o->params().w);
                    m->id = kernel()->newMsgId();
                    m->from_agent = this->id();
                    for (uint64_t x = r.address; x < r.address + r.bytes(); x++) copyByte(*m, r, x);   // mask and data as received
                    sendOuter(m, block, false);
                    break;
                }
                case coh::MgrOp::ForwardIn:
                {
                    if (!req || !consumed) fail("ForwardIn with no request or response");
                    const Msg &r = *req->msg;
                    MsgHeader h = coh::ManagerPolicy::accessAck(r, consumed->denied);
                    h.opcode = consumed->opcode;   // the outer response, relayed (TLC_Tables §3.3)
                    MsgPtr m = makeMsg(*kernel(), this->id(), h, m_ports[size_t(req->port)].link->params().w);
                    if (m->hasData())
                    {
                        for (uint64_t x = r.address; x < r.address + r.bytes(); x++) copyByte(*m, r.address, *consumed, r.address, x);
                        bool any = false;
                        for (auto cr : consumed->corrupt) if (cr) any = true;
                        if (any || consumed->denied) for (auto &cr : m->corrupt) cr = 1;   // §4.4
                    }
                    reportForwarded(r, *m, cycle);
                    m_ports[size_t(req->port)].outD.q.push_back(m);
                    m_c.uncached++;
                    break;
                }
                case coh::MgrOp::SaveData:
                {
                    if (!consumed || !consumed->hasData()) fail("SaveData with no data");
                    // the fill (outer AccessAckData, placed by the Get's address) or the ProbeAckData / ReleaseData
                    uint64_t geom = consumed->channel == Channel::D ? block : consumed->address;
                    e.data.fromMsg(*consumed, geom, block, block + m_bb - 1, false);
                    break;
                }
                case coh::MgrOp::Drop:
                    e.has_data = false;
                    break;
                case coh::MgrOp::GrantData:
                case coh::MgrOp::Grant:
                case coh::MgrOp::GrantDenied:
                {
                    if (!req || !e.sink_held) fail("a Grant with no Acquire or no reserved sink");
                    MsgHeader h = coh::ManagerPolicy::grant(a, *req->msg, e.sink);
                    MsgPtr m = makeMsg(*kernel(), this->id(), h, m_ports[size_t(req->port)].link->params().w);
                    if (m->hasData()) e.data.toMsg(*m, block, block, block + m_bb - 1);
                    m_grant_sink[{req->port, e.sink}] = block;
                    m_ports[size_t(req->port)].outD.q.push_back(m);
                    m_c.grants++;
                    if (a.op == coh::MgrOp::GrantDenied) m_c.denied_grants++;
                    break;
                }
                case coh::MgrOp::ReleaseAck:
                {
                    if (!rx || rx->port < 0) fail("ReleaseAck with no Release");
                    MsgPtr m = makeMsg(*kernel(), this->id(), coh::ManagerPolicy::releaseAck(*consumed), m_ports[size_t(rx->port)].link->params().w);
                    m_ports[size_t(rx->port)].outD.q.push_back(m);
                    m_c.releases++;
                    break;
                }
                case coh::MgrOp::ProbeAll:
                case coh::MgrOp::ProbeOwner:
                {
                    std::vector<int> targets;
                    if (a.op == coh::MgrOp::ProbeOwner)
                    {
                        if (e.owner < 0) fail("ProbeBlock to the owner with no owner listed");
                        targets.push_back(e.owner);
                    }
                    else
                    {
                        // TLC_Tables §3.3: one per holder except the requester.
                        if (e.owner >= 0 && e.owner != requester) targets.push_back(e.owner);
                        for (int s : e.sharers) if (s != requester) targets.push_back(s);
                    }
                    if (targets.empty()) fail("ProbeBlock_toN_all with no holder to probe (the counter would never reach 0)");
                    for (int cl : targets)
                    {
                        const Client &k = m_clients[size_t(cl)];
                        Port &p = m_ports[size_t(k.port)];
                        if (!p.link->has(Channel::B)) fail("a Probe to client " + k.name + ", whose link has no channel B");
                        MsgPtr m = makeMsg(*kernel(), this->id(), coh::ManagerPolicy::probe(block, m_lg, a.param, k.base), p.link->params().w);
                        p.outB.q.push_back(m);
                        m_c.probes++;
                    }
                    e.probes = unsigned(targets.size());   // §1.2 rule 4: the Probes sent
                    break;
                }
                case coh::MgrOp::SetOwner:
                    if (requester < 0) fail("SetOwner with no requester");
                    e.owner = requester;
                    e.sharers.clear();
                    break;
                case coh::MgrOp::AddSharer:
                    if (requester < 0) fail("AddSharer with no requester");
                    e.sharers.insert(requester);
                    break;
                case coh::MgrOp::OwnerToSharer:
                    if (e.owner >= 0) e.sharers.insert(e.owner);
                    e.owner = -1;
                    break;
                case coh::MgrOp::RemoveHolder:
                    if (!rx) fail("RemoveHolder with no message");
                    if (e.owner == rx->client) e.owner = -1;
                    e.sharers.erase(rx->client);
                    break;
                case coh::MgrOp::DecCount:
                    if (e.probes == 0) fail("DecCount with the probe counter at 0");
                    e.probes--;
                    m_c.probe_acks++;
                    break;
                case coh::MgrOp::Execute:
                case coh::MgrOp::AccessAckDataIn:
                {
                    if (!req) fail("an uncached response with no request");
                    const Msg &r = *req->msg;
                    if (r.address < block || r.address + r.bytes() > block + m_bb) fail("an uncached access crosses its block");
                    MsgPtr m = makeMsg(*kernel(), this->id(), coh::ManagerPolicy::accessAck(r, false), m_ports[size_t(req->port)].link->params().w);
                    if (a.op == coh::MgrOp::Execute) executeAccess(e, r, *m, cycle);
                    else
                    {
                        // TLC_Tables §3.3 AccessAckData_in: the LLC copy, masked to the request
                        e.data.toMsg(*m, r.address, r.address, r.address + r.bytes() - 1);
                        reportBytes(cc, false, unsigned(this->id()), mode, r.address, r.address + r.bytes() - 1,
                                    [&](uint64_t x) { return e.data.value(x); }, [&](uint64_t x) { return !e.data.poisoned(x); }, cycle);
                    }
                    m_ports[size_t(req->port)].outD.q.push_back(m);
                    m_c.uncached++;
                    break;
                }
                case coh::MgrOp::HintAck:
                case coh::MgrOp::HintAckDenied:
                {
                    if (!req) fail("HintAck with no Intent");
                    MsgPtr m = makeMsg(*kernel(), this->id(), coh::ManagerPolicy::accessAck(*req->msg, a.op == coh::MgrOp::HintAckDenied),
                                       m_ports[size_t(req->port)].link->params().w);
                    m_ports[size_t(req->port)].outD.q.push_back(m);
                    m_c.hint_acks++;
                    break;
                }
            }
            if (coh::answers(a.op)) answered = true;
        }
        // §1.2 rule 3: an inner-A cell that does not answer parks its request; the answering cell releases it.
        if (consumed_inner_a && !answered)
        {
            if (e.parked) fail("a second request parked on the block");
            e.req = *rx;
            e.parked = true;
        }
        else if (!consumed_inner_a && answered && e.parked) { e.parked = false; e.req = Rx(); }
        e.state = c.next;
        if (consumed_inner_a) e.lru = ++m_use;
        if (!t.dataValid(c.next)) e.has_data = false;
        if (t.stable(c.next)) m_evicting.erase(block);
        report(tr);
        if (c.next == m_policy->stateN() && !e.parked && e.owner < 0 && e.sharers.empty() && !e.sink_held && e.probes == 0)
            m_dir.erase(block);
    }

    bool CoherentManager::raiseReplacement(unsigned set, uint64_t cycle)
    {
        for (uint64_t b : m_evicting) if (setOf(b) == set) return false;   // one eviction per set at a time
        const coh::Table &t = m_policy->table();
        const int ev = m_policy->eventReplacement();
        Entry *best = nullptr;
        for (auto &kv : m_dir)
        {
            Entry &e = *kv.second;
            if (setOf(e.block) != set || !t.dataValid(e.state)) continue;
            const coh::Cell &c = t.transition(e.state, ev);
            bool ok = !c.empty;
            for (int a : c.actions) { auto op = m_policy->action(a).op; if (op == coh::MgrOp::Stall || op == coh::MgrOp::Fault) ok = false; }
            if (!ok) continue;
            if (!best || e.lru < best->lru) best = &e;
        }
        if (!best) return false;
        uint64_t b = best->block;
        if (!t.stable(t.transition(best->state, ev).next)) m_evicting.insert(b);
        execute(b, ev, nullptr, cycle);
        return true;
    }

    // ------------------------------------------------------------------ the queues
    bool CoherentManager::processE(uint64_t cycle)
    {
        if (m_rxE.empty()) return false;
        Rx rx = m_rxE.front();
        auto it = m_grant_sink.find({rx.port, rx.msg->sink});
        if (it == m_grant_sink.end())
            fatal("CoherentManager " + m_name + ": GrantAck sink " + std::to_string(rx.msg->sink) + " answers no Grant");
        uint64_t block = it->second;
        coh::Decoded d = m_policy->decodeE();
        if (isStall(stateOf(block), d.event)) { m_c.stalls++; return false; }
        m_grant_sink.erase(it);
        if (Entry *e = entry(block)) { if (e->sink_held) { m_ports[size_t(e->sink_port)].free_sinks.push_back(e->sink); e->sink_held = false; } }
        m_rxE.pop_front();
        execute(block, d.event, &rx, cycle);
        return true;
    }

    bool CoherentManager::processOuterD(uint64_t cycle)
    {
        if (m_rxD.empty()) return false;
        Rx rx = m_rxD.front();
        auto it = m_outer_out.find(rx.msg->source);
        if (it == m_outer_out.end()) fatal("CoherentManager " + m_name + ": outer " + rx.msg->describe() + " answers nothing outstanding");
        OuterRec rec = it->second;
        if (rec.evict)
        {
            // §1.2 rule 7: the eviction's AccessAck is the port's (K_MEM_WACK), never a table event.
            if (rx.msg->opcode != unsigned(OpD::AccessAck)) fatal("CoherentManager " + m_name + ": eviction write answered by " + rx.msg->describe());
            if (--m_h1[rec.block] == 0) m_h1.erase(rec.block);
            m_outer_out.erase(it);
            m_outer_free.push_back(rx.msg->source);
            m_rxD.pop_front();
            return true;
        }
        coh::Decoded d = m_policy->decodeOuterD(*rx.msg);
        if (!d.ok()) fatal("CoherentManager " + m_name + " cycle " + std::to_string(cycle) + ": decode fault: " + d.fault);
        if (isStall(stateOf(rec.block), d.event)) { m_c.stalls++; return false; }
        m_outer_out.erase(it);
        m_outer_free.push_back(rx.msg->source);
        m_rxD.pop_front();
        execute(rec.block, d.event, &rx, cycle);
        return true;
    }

    bool CoherentManager::processC(uint64_t cycle)
    {
        for (auto it = m_rxC.begin(); it != m_rxC.end(); ++it)
        {
            uint64_t block = blockOf(it->msg->address);
            coh::Decoded d = m_policy->decodeC(*it->msg, view(block, it->client));
            if (!d.ok())
                fatal("CoherentManager " + m_name + " cycle " + std::to_string(cycle) + ": decode fault: " + d.fault + " (" + it->msg->describe() +
                      " from " + clientName(it->client) + ", block state " + blockState(block) + ")");
            // §1.2 rule 2: a Release in T_g/B_g (it overtook its own GrantAck, TLC_Tables §11.5) is parked.
            if (isStall(stateOf(block), d.event)) { reportStall(block, d.event, *it, cycle); continue; }
            Rx rx = *it;
            m_stall_seen.erase(rx.msg.get());
            m_rxC.erase(it);
            execute(block, d.event, &rx, cycle);
            return true;
        }
        return false;
    }

    bool CoherentManager::processA(uint64_t cycle)
    {
        const coh::Table &t = m_policy->table();
        std::set<uint64_t> skipped;   // per-block arrival order (TLC_Tables §4 inv. 6)
        for (auto it = m_rxA.begin(); it != m_rxA.end(); ++it)
        {
            const Msg &m = *it->msg;
            uint64_t block = blockOf(m.address);
            if (skipped.count(block)) continue;
            if (blockOf(m.address + m.bytes() - 1) != block)
                fatal("CoherentManager " + m_name + ": " + m.describe() + " spans more than one block (block_bytes " + std::to_string(m_bb) + ")");
            coh::Decoded d = m_policy->decodeA(m, view(block, it->client));
            if (!d.ok())
                fatal("CoherentManager " + m_name + " cycle " + std::to_string(cycle) + ": decode fault: " + d.fault + " (" + m.describe() +
                      " from " + clientName(it->client) + ", block state " + blockState(block) + ")");
            int st = stateOf(block);
            if (isStall(st, d.event)) { reportStall(block, d.event, *it, cycle); skipped.insert(block); continue; }
            const bool acquire = m.opcode == unsigned(OpA::AcquireBlock);
            Port &p = m_ports[size_t(it->port)];
            // plan §5.2: a Grant needs a free sink, reserved when the Acquire is popped
            if (acquire && p.free_sinks.empty()) { skipped.insert(block); continue; }
            // a cell that allocates the LLC copy needs a way (TLC_Tables §1.2 rule 6)
            const coh::Cell &c = t.transition(st, d.event);
            if (t.dataValid(c.next) && !t.dataValid(st) && waysUsed(setOf(block)) >= m_ways)
            {
                raiseReplacement(setOf(block), cycle);
                skipped.insert(block);
                continue;
            }
            Rx rx = *it;
            m_stall_seen.erase(rx.msg.get());
            m_rxA.erase(it);
            if (acquire)
            {
                Entry &e = makeEntry(block);
                if (e.sink_held) fatal("CoherentManager " + m_name + ": a second Grant sink reserved on block " + std::to_string(block));
                e.sink = p.free_sinks.back();
                p.free_sinks.pop_back();
                e.sink_port = rx.port;
                e.sink_held = true;
                m_c.acquires++;
            }
            else if (m.opcode == unsigned(OpA::Intent)) m_c.cmos++;
            execute(block, d.event, &rx, cycle);
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------------ the three phases
    void CoherentManager::offer(uint64_t)
    {
        if (kernel()->inReset()) return;
        for (auto &p : m_ports)
        {
            if (!p.outB.q.empty()) p.link->chan(Channel::B).offer(p.outB.q.front(), p.outB.beat);
            if (!p.outD.q.empty()) p.link->chan(Channel::D).offer(p.outD.q.front(), p.outD.beat);
        }
        if (!m_outerA.q.empty()) outer()->chan(Channel::A).offer(m_outerA.q.front(), m_outerA.beat);
    }

    void CoherentManager::accept(uint64_t)
    {
        // §1.2 rule 2: every receive queue accepts (outside reset), whatever is parked in it.
        bool r = !kernel()->inReset();
        for (auto &p : m_ports)
            for (Channel ch : {Channel::A, Channel::C, Channel::E})
                if (p.link->has(ch)) p.link->chan(ch).setReady(r);
        if (Link *o = outer()) o->chan(Channel::D).setReady(r);
    }

    void CoherentManager::commit(uint64_t cycle)
    {
        auto advance = [](OutQ &o, ChannelState &ch)
        {
            const Beat &b = ch.accepted();
            if (!b.msg) return;
            if (b.last()) { o.q.pop_front(); o.beat = 0; } else o.beat = b.index + 1;
        };
        for (auto &p : m_ports)
        {
            if (p.link->has(Channel::B)) advance(p.outB, p.link->chan(Channel::B));
            advance(p.outD, p.link->chan(Channel::D));
        }
        Link *o = outer();
        if (o) advance(m_outerA, o->chan(Channel::A));
        for (size_t i = 0; i < m_ports.size(); i++)
        {
            Port &p = m_ports[i];
            for (Channel ch : {Channel::A, Channel::C, Channel::E})
            {
                if (!p.link->has(ch)) continue;
                const Beat &b = p.link->chan(ch).accepted();
                if (!b.msg || !b.last()) continue;
                Rx rx;
                rx.msg = b.msg;
                rx.port = int(i);
                if (ch != Channel::E)
                {
                    rx.client = clientOf(int(i), b.msg->source);
                    if (rx.client < 0) fatal("CoherentManager " + m_name + ": " + b.msg->describe() + " on " + p.link->name() + ": source matches no client");
                }
                (ch == Channel::A ? m_rxA : ch == Channel::C ? m_rxC : m_rxE).push_back(rx);
            }
        }
        if (o)
        {
            const Beat &b = o->chan(Channel::D).accepted();
            if (b.msg && b.last()) { Rx rx; rx.msg = b.msg; rx.port = -1; m_rxD.push_back(rx); }
        }
        if (kernel()->inReset()) return;
        // the order of plan §5.2: E, D, C before A (a stalled lower channel never blocks a higher one)
        processE(cycle);
        processOuterD(cycle);
        processC(cycle);
        processA(cycle);
        promoteOuter();
    }
}}
