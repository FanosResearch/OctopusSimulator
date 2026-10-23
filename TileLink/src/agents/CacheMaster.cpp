/*
 * File  :      CacheMaster.cpp
 * The TL-C L1 (plan §3.5) executing the L1 table (docs/TileLink_TLC_Tables.md §2). See
 * CacheMaster.h for the engine rules around a cell.
 */
#include "tilelink/agents/CacheMaster.h"
#include "tilelink/agents/CoherentManager.h"
#include "tilelink/CoherenceCheck.h"
#include "tilelink/Kernel.h"
#include "../coh/BlockData.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

namespace octopus { namespace tl
{
    // ------------------------------------------------------------------ the default table
    static bool fileExists(const std::string &p)
    {
        struct stat st;
        return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
    }
    static std::string dirOf(const std::string &p)
    {
        size_t s = p.find_last_of('/');
        return s == std::string::npos ? "." : p.substr(0, s);
    }

    std::string findTable(const std::string &name)
    {
        std::vector<std::string> dirs;
        if (const char *e = std::getenv("TILELINK_TABLES")) dirs.push_back(e);
#ifdef TL_DEFAULT_TABLE_DIR
        dirs.push_back(TL_DEFAULT_TABLE_DIR);
#endif
        char exe[4096];
        ssize_t n = ::readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (n > 0)
        {
            exe[n] = '\0';
            std::string d = dirOf(exe);
            for (const char *rel : {"/tables", "/../tables", "/../../tables", "/../TileLink/tables", "/../../TileLink/tables"}) dirs.push_back(d + rel);
        }
        dirs.push_back("tables");
        dirs.push_back("TileLink/tables");
        // The source tree: this file is TileLink/src/agents/CacheMaster.cpp.
        dirs.push_back(dirOf(dirOf(dirOf(__FILE__))) + "/tables");
        std::string tried;
        for (auto &d : dirs)
        {
            std::string p = d + "/" + name;
            if (fileExists(p)) return p;
            tried += (tried.empty() ? "" : ", ") + d;
        }
        throw ConfigError("table " + name + " not found (looked in " + tried + "; set TILELINK_TABLES or the agent's table(s))");
    }

    // ------------------------------------------------------------------ the built-in random CPU
    namespace
    {
        class RandomCpu : public CpuSource
        {
        public:
            RandomCpu(uint64_t ops, uint64_t base, uint64_t span, unsigned store_pct, unsigned evict_pct, unsigned bytes, unsigned rate)
                : m_ops(ops), m_base(base), m_span(span), m_store(store_pct), m_evict(evict_pct), m_bytes(bytes), m_rate(rate) {}
            bool next(CpuOp &op, uint64_t cycle, uint64_t, std::mt19937_64 &rng) override
            {
                if (m_n >= m_ops) return false;
                if (m_rate < 100 && rng() % 100 >= m_rate) return false;
                op = CpuOp();
                unsigned r = unsigned(rng() % 100);
                op.kind = r < m_evict ? CpuOp::Evict : r < m_evict + m_store ? CpuOp::Store : CpuOp::Load;
                op.bytes = m_bytes;
                uint64_t slots = std::max<uint64_t>(1, m_span / m_bytes);
                op.addr = m_base + (rng() % slots) * m_bytes;
                op.not_before = cycle;
                m_n++;
                return true;
            }
            bool exhausted() const override { return m_n >= m_ops; }
        private:
            uint64_t m_ops, m_base, m_span, m_n = 0;
            unsigned m_store, m_evict, m_bytes, m_rate;
        };

        // cpu(s)=script: the CacheMaster's pushOp queue, in program order.
        class ScriptCpu : public CpuSource
        {
        public:
            explicit ScriptCpu(std::deque<CpuOp> &q) : m_q(q) {}
            bool next(CpuOp &op, uint64_t, uint64_t, std::mt19937_64 &) override
            {
                if (m_q.empty()) return false;
                op = m_q.front();
                m_q.pop_front();
                return true;
            }
            bool exhausted() const override { return m_q.empty(); }
        private:
            std::deque<CpuOp> &m_q;
        };

        class NoCpu : public CpuSource
        {
        public:
            bool next(CpuOp &, uint64_t, uint64_t, std::mt19937_64 &) override { return false; }
            bool exhausted() const override { return true; }
        };
    }

    // ------------------------------------------------------------------ the line
    struct CacheMaster::Line
    {
        uint64_t       block = 0;
        int            state = 0;
        coh::BlockData data;
        uint64_t       lru = 0;
        int64_t        parked = -1;   // seq of the CPU access parked on the line (§1.2 rule 3)
    };

    CacheMaster::CacheMaster(const std::string &name, const Config &p) : Agent(name)
    {
        std::string table = p.has("table") ? p.get<std::string>("table") : findTable("TLC_MESI_L1.csv");
        m_policy.reset(new coh::L1Policy(coh::Table::load(table)));
        if (m_policy->table().numStates() > 64) throw ConfigError(name + ": more than 64 states in " + table);
        int bb = p.get<int>("block_bytes", 64);
        if (bb < 8 || (bb & (bb - 1))) throw ConfigError(name + ": block_bytes must be a power of two >= 8");
        m_bb = unsigned(bb);
        m_lg = 0;
        while ((1u << m_lg) < m_bb) m_lg++;
        int sets = p.get<int>("sets", 64), ways = p.get<int>("ways", 4), out = p.get<int>("outstanding", 4);
        if (sets <= 0 || ways <= 0 || out <= 0) throw ConfigError(name + ": sets, ways and outstanding must be >= 1");
        m_sets = unsigned(sets); m_ways = unsigned(ways); m_outstanding = unsigned(out);
        m_debug = p.get<int>("debug", 0) != 0;
        unsigned bytes = unsigned(p.get<int>("access_bytes", 8));
        if (bytes == 0 || (bytes & (bytes - 1)) || bytes > m_bb) throw ConfigError(name + ": access_bytes must be a power of two <= block_bytes");
        std::string cpu = p.get<std::string>("cpu", p.has("trace") ? "trace" : "random");
        if (cpu == "random")
        {
            auto nn = [&](const char *k, int d) { int v = p.get<int>(k, d); if (v < 0) throw ConfigError(name + ": " + k + " must be >= 0"); return unsigned(v); };
            m_cpu.reset(new RandomCpu(nn("ops", 1000), nn("addr_base", 0), std::max(1u, nn("addr_span", 4096)), std::min(100u, nn("store_pct", 30)),
                                      std::min(100u, nn("evict_pct", 0)), bytes, std::min(100u, nn("rate", 100))));
        }
        else if (cpu == "trace")
        {
            if (!p.has("trace")) throw ConfigError(name + ": cpu trace needs trace(s)");
            std::string timing = p.get<std::string>("timing", "asap");
            if (timing != "asap" && timing != "trace") throw ConfigError(name + ": timing must be asap or trace, got " + timing);
            m_cpu.reset(new TraceReader(p.get<std::string>("trace"), bytes, timing == "trace", uint64_t(std::max(0, p.get<int>("max_gap", 1000))),
                                        uint64_t(std::max(0, p.get<int>("max_ops", 0)))));
        }
        else if (cpu == "script") { m_script_mode = true; m_keep = true; m_cpu.reset(new ScriptCpu(m_script)); }
        else if (cpu == "none") m_cpu.reset(new NoCpu());
        else throw ConfigError(name + ": cpu must be random, trace, script or none, got " + cpu);
        m_keep = m_keep || p.get<int>("keep_ops", 0) != 0;
    }

    CacheMaster::~CacheMaster() {}

    void CacheMaster::setCpu(std::unique_ptr<CpuSource> src) { m_cpu = std::move(src); m_script_mode = false; }
    void CacheMaster::pushOp(const CpuOp &op)
    {
        if (!m_script_mode) { m_script_mode = true; m_keep = true; m_cpu.reset(new ScriptCpu(m_script)); }
        m_script.push_back(op);
    }
    bool CacheMaster::cpuDone() const { return m_cpu->exhausted() && m_window.empty(); }

    Link *CacheMaster::link() const
    {
        for (auto &lr : m_links) if (lr.role == Role::Master) return lr.link;
        return nullptr;
    }

    void CacheMaster::reset(uint64_t)
    {
        Link *l = link();
        if (!l) return;
        if (l->level() != Level::C) fatal("CacheMaster " + m_name + ": link " + l->name() + " must be TL-C");
        if (l->params().maxSize() < m_lg)
            fatal("CacheMaster " + m_name + ": link " + l->name() + " (z=" + std::to_string(l->params().z) + ") cannot encode the block size 2^" + std::to_string(m_lg));
        // §6.4: the A namespace and the C (Release) namespace, 2^o each (at most 1024 used).
        uint64_t pool = l->params().o >= 10 ? 1024 : l->params().numSources();
        m_freeA.clear(); m_freeC.clear();
        for (uint64_t s = pool; s-- > 0;) { m_freeA.push_back(s); m_freeC.push_back(s); }
    }

    bool CacheMaster::idle() const
    {
        if (!m_cpu->exhausted() || !m_window.empty() || !m_rxB.empty() || !m_rxD.empty()) return false;
        for (auto &o : m_out) if (!o.q.empty()) return false;
        for (auto &kv : m_lines) if (!m_policy->table().stable(kv.second->state)) return false;
        return true;
    }

    ProgressHints CacheMaster::hints() const
    {
        ProgressHints h;
        h.is_master = true;
        uint64_t other = 1;
        if (kernel())
            for (auto &a : kernel()->agents())
                if (a.get() != this && !dynamic_cast<const CacheMaster *>(a.get()) && !dynamic_cast<const CoherentManager *>(a.get()) &&
                    !a->hints().is_master)
                {
                    // the slowest hop (the manager declares its own transaction time separately)
                    uint64_t l = a->hints().latency;
                    if (l > other) other = l;
                }
        h.latency = 4 * (1 + other);
        h.queue_depth = m_outstanding + 2;
        return h;
    }

    // ------------------------------------------------------------------ lines
    CacheMaster::Line *CacheMaster::line(uint64_t block)
    {
        auto it = m_lines.find(block);
        return it == m_lines.end() ? nullptr : it->second.get();
    }
    int CacheMaster::stateOf(uint64_t block) const
    {
        auto it = m_lines.find(block);
        return it == m_lines.end() ? m_policy->stateInvalid() : it->second->state;
    }
    unsigned CacheMaster::waysUsed(unsigned set) const
    {
        unsigned n = 0;
        for (auto &kv : m_lines) if (setOf(kv.first) == set && m_policy->table().dataValid(kv.second->state)) n++;
        return n;
    }
    std::string CacheMaster::lineState(uint64_t address) const { return m_policy->table().stateName(stateOf(blockOf(address))); }
    bool CacheMaster::lineValue(uint64_t address, unsigned bytes, std::vector<uint8_t> &out) const
    {
        auto it = m_lines.find(blockOf(address));
        if (it == m_lines.end() || it->second->data.mode != DataMode::Full) return false;
        out.clear();
        for (unsigned i = 0; i < bytes; i++) out.push_back(uint8_t(it->second->data.value(address + i)));
        return true;
    }

    CpuOp *CacheMaster::findOp(uint64_t seq)
    {
        for (auto &op : m_window) if (op.seq == seq) return &op;
        return nullptr;
    }

    bool CacheMaster::resources(int state, int event, bool line_present, bool &need_way) const
    {
        const coh::Table &t = m_policy->table();
        const coh::Cell &c = t.transition(state, event);
        need_way = false;
        unsigned a = 0, r = 0;
        for (int id : c.actions)
        {
            coh::L1Op op = m_policy->action(id).op;
            if (op == coh::L1Op::AcquireBlock) a++;
            if (op == coh::L1Op::Release || op == coh::L1Op::ReleaseData) r++;
        }
        if (a > m_freeA.size() || r > m_freeC.size()) return false;
        // The cell allocates a line (I -> a state that holds an MSHR or array line, TLC_Tables §1.1).
        if (!line_present && t.dataValid(c.next)) need_way = true;
        return true;
    }

    // ------------------------------------------------------------------ executing a cell
    void CacheMaster::emit(const MsgHeader &h, const Line *l, uint64_t)
    {
        const unsigned w = link()->params().w;
        MsgPtr m = Msg::make(h.channel, h.opcode, h.size, w, kernel()->options().data_mode);
        static_cast<MsgHeader &>(*m) = h;
        m->layout(w, kernel()->options().data_mode);
        m->id = kernel()->newMsgId();
        m->from_agent = id();
        if (m->hasData() && l) l->data.toMsg(*m, m->address, l->block, l->block + m_bb - 1);
        m_out[int(h.channel)].q.push_back(m);
    }

    void CacheMaster::report(const coh::Transition &t)
    {
        if (m_debug) std::cout << t.line() << std::endl;
        if (onTransition) onTransition(t);
    }

    void CacheMaster::perform(Line &l, CpuOp &op, uint64_t cycle)
    {
        // §1.2 rule 6: Hit serves the CPU from the line and, for a Store, writes it.
        const DataMode mode = kernel()->options().data_mode;
        CoherenceCheck *cc = CoherenceCheck::of(*kernel());
        std::vector<uint8_t> bytes;
        if (op.kind == CpuOp::Store)
        {
            m_c.stores++;
            uint64_t v = (uint64_t(id() + 1) << 48) | ++m_store_counter;   // unique per agent and store
            if (mode == DataMode::Full)
            {
                op.value.clear();
                for (unsigned i = 0; i < op.bytes; i++)
                {
                    uint8_t b = uint8_t(v >> (8 * (i % 8)));
                    l.data.set(op.addr + i, b);
                    op.value.push_back(b);
                }
            }
            else if (mode == DataMode::Stamp)
                for (uint64_t g = op.addr & ~uint64_t(7); g < op.addr + op.bytes; g += 8) l.data.set(g, v);
            for (unsigned i = 0; i < op.bytes; i++) l.data.setPoison(op.addr + i, false);   // the Store's own bytes are valid
            if (cc) cc->accessPerformed(*link(), op.addr, true, cycle);   // C-PERM (Table 29)
            if (cc && mode != DataMode::None)
            {
                uint64_t a = l.data.checkBytes(op.addr, op.bytes, bytes);
                cc->storePerformed(unsigned(id()), a, bytes.data(), unsigned(bytes.size()), cycle);
            }
        }
        else
        {
            m_c.loads++;
            if (mode == DataMode::Full)
            {
                op.value.clear();
                for (unsigned i = 0; i < op.bytes; i++) op.value.push_back(uint8_t(l.data.value(op.addr + i)));
            }
            if (cc) cc->accessPerformed(*link(), op.addr, false, cycle);  // C-PERM (Table 29)
            // a Load consuming poisoned bytes (§4.4) completes as an access error, never as clean data
            // (docs/TileLink_Review_Action_Plan.md problem 2), and reports no value
            if (l.data.anyPoisoned(op.addr, op.addr + op.bytes - 1))
            {
                op.error = true; m_c.errors++; m_c.poisoned_loads++;
            }
            else if (cc && mode != DataMode::None)
            {
                uint64_t a = l.data.checkBytes(op.addr, op.bytes, bytes);
                cc->loadPerformed(unsigned(id()), a, bytes.data(), unsigned(bytes.size()), cycle);
            }
        }
        op.done = true;
        op.t_done = cycle;
        l.lru = ++m_use;
    }

    void CacheMaster::execute(uint64_t block, int event, const MsgPtr &trigger, CpuOp *cpu, uint64_t cycle)
    {
        const coh::Table &t = m_policy->table();
        Line *l = line(block);
        const int from = l ? l->state : m_policy->stateInvalid();
        const coh::Cell &c = t.transition(from, event);
        coh::Transition tr;
        tr.cycle = cycle; tr.agent = m_name; tr.block = block;
        tr.from = t.stateName(from); tr.event = t.eventName(event); tr.cell = c.text; tr.to = t.stateName(c.next);
        if (trigger) tr.msg = trigger->describe();
        if (!l && t.dataValid(c.next))
        {
            // the line is allocated (the caller checked a way is free)
            std::unique_ptr<Line> nl(new Line());
            nl->block = block;
            nl->state = from;
            nl->data.init(kernel()->options().data_mode, block, m_bb);
            l = nl.get();
            m_lines[block] = std::move(nl);
        }
        // The data handler's fill: a GrantData that is not denied writes the line (TLC_Tables §2.4
        // IS_a/IM_a/SM_a + GrantData; S9.3.8 "an up-to-date copy").
        if (l && trigger && trigger->channel == Channel::D && trigger->opcode == unsigned(OpD::GrantData) && !trigger->denied)
            l->data.fromMsg(*trigger, block, block, block + m_bb - 1, false);
        const bool cpu_event = event == m_policy->eventLoad() || event == m_policy->eventStore();
        bool answered = false;
        for (int id : c.actions)
        {
            const coh::L1Action &a = m_policy->action(id);
            MsgHeader h;
            switch (a.op)
            {
                case coh::L1Op::Stall:
                    fatal("CacheMaster " + m_name + ": internal: a Stall cell was executed (" + tr.line() + ")");
                case coh::L1Op::Fault:
                    fatal("CacheMaster " + m_name + ": table Fault cell (" + t.name() + "): " + tr.line());
                case coh::L1Op::Hit:
                case coh::L1Op::Error:
                {
                    // §1.2 rule 3: on a CPU event the access itself; otherwise the parked one.
                    CpuOp *op = cpu_event ? cpu : (l && l->parked >= 0 ? findOp(uint64_t(l->parked)) : nullptr);
                    if (!op) fatal("CacheMaster " + m_name + ": " + std::string(coh::l1OpName(a.op)) + " with no access to serve: " + tr.line());
                    if (a.op == coh::L1Op::Hit) perform(*l, *op, cycle);
                    else { op->done = true; op->error = true; op->t_done = cycle; m_c.errors++; }
                    if (l && !cpu_event) l->parked = -1;
                    answered = true;
                    break;
                }
                case coh::L1Op::AcquireBlock:
                {
                    uint64_t s = m_freeA.back();
                    m_freeA.pop_back();
                    m_srcA[s] = block;
                    m_policy->build(id, block, m_lg, trigger.get(), s, h);
                    emit(h, l, cycle);
                    m_c.acquires++;
                    break;
                }
                case coh::L1Op::Release:
                case coh::L1Op::ReleaseData:
                {
                    uint64_t s = m_freeC.back();
                    m_freeC.pop_back();
                    m_srcC[s] = block;
                    m_policy->build(id, block, m_lg, trigger.get(), s, h);
                    emit(h, l, cycle);
                    m_c.releases++;
                    break;
                }
                case coh::L1Op::GrantAck:
                case coh::L1Op::ProbeAck:
                case coh::L1Op::ProbeAckData:
                    m_policy->build(id, block, m_lg, trigger.get(), 0, h);
                    emit(h, l, cycle);
                    if (a.op != coh::L1Op::GrantAck) m_c.probes++;
                    break;
            }
        }
        // §1.2 rule 3: a CPU access that leaves a stable state for a transient one is parked on the line.
        if (cpu_event && !answered && cpu && l) { l->parked = int64_t(cpu->seq); if (cpu->t_issue == NEVER) cpu->t_issue = cycle; }
        if (cpu && cpu->t_issue == NEVER) cpu->t_issue = cycle;
        if (l) { l->state = c.next; if (cpu_event) l->lru = ++m_use; }
        if (c.next < 64) m_c.states_entered[c.next]++;
        if (l && !t.dataValid(c.next))
        {
            if (l->parked >= 0) fatal("CacheMaster " + m_name + ": the line leaves with a parked access: " + tr.line());
            m_lines.erase(block);   // the way is free (I)
        }
        report(tr);
    }

    // ------------------------------------------------------------------ the queues
    bool CacheMaster::processD(uint64_t cycle)
    {
        for (auto it = m_rxD.begin(); it != m_rxD.end(); ++it)
        {
            const MsgPtr m = *it;
            coh::Decoded d = m_policy->decode(*m);
            if (!d.ok()) fatal("CacheMaster " + m_name + " cycle " + std::to_string(cycle) + ": decode fault: " + d.fault + " (" + m->describe() + ")");
            // D carries no address (Table 9): the block is the one the answered request named.
            bool relack = m->opcode == unsigned(OpD::ReleaseAck);
            auto &srcs = relack ? m_srcC : m_srcA;
            auto s = srcs.find(m->source);
            if (s == srcs.end()) fatal("CacheMaster " + m_name + ": " + m->describe() + " answers no outstanding request (source " + std::to_string(m->source) + ")");
            uint64_t block = s->second;
            int st = stateOf(block);
            if (m_policy->table().hasAction(st, d.event, m_policy->table().actionId("Stall"))) { m_c.stalls++; continue; }
            if (!relack) { m_c.grants++; if (m->denied) m_c.denied++; }
            execute(block, d.event, m, nullptr, cycle);
            srcs.erase(s);
            (relack ? m_freeC : m_freeA).push_back(m->source);   // §6.4: reusable once answered
            m_rxD.erase(it);
            return true;
        }
        return false;
    }

    bool CacheMaster::processB(uint64_t cycle)
    {
        const int stall = m_policy->table().actionId("Stall");
        for (auto it = m_rxB.begin(); it != m_rxB.end(); ++it)
        {
            const MsgPtr m = *it;
            coh::Decoded d = m_policy->decode(*m);
            if (!d.ok()) fatal("CacheMaster " + m_name + " cycle " + std::to_string(cycle) + ": decode fault: " + d.fault + " (" + m->describe() + ")");
            uint64_t block = blockOf(m->address);
            int st = stateOf(block);
            // §1.2 rule 2: a Stall parks the Probe in its queue (SI_r/EI_r/MI_r: no ProbeAck before
            // the ReleaseAck, S9.2-R); it is re-decoded every cycle, i.e. when the line changes.
            if (stall >= 0 && m_policy->table().hasAction(st, d.event, stall))
            {
                m_c.stalls++;
                auto &seen = m_stall_seen[m.get()];
                if (seen != st + 1 && (m_debug || onTransition))
                {
                    const coh::Table &t = m_policy->table();
                    coh::Transition tr;
                    tr.cycle = cycle; tr.agent = m_name; tr.block = block; tr.from = t.stateName(st); tr.event = t.eventName(d.event);
                    tr.cell = t.transition(st, d.event).text; tr.to = "Stall"; tr.stalled = true; tr.msg = m->describe();
                    report(tr);
                }
                seen = st + 1;
                continue;
            }
            m_stall_seen.erase(m.get());
            execute(block, d.event, m, nullptr, cycle);
            m_rxB.erase(it);
            return true;
        }
        return false;
    }

    bool CacheMaster::raiseReplacement(unsigned set, uint64_t cycle)
    {
        // The victim: the LRU line of the set whose Replacement cell is executable (TLC_Tables §2.4:
        // S, E, M; the transient states Stall it, §5.4, and the _r states Fault it).
        const coh::Table &t = m_policy->table();
        const int ev = m_policy->eventReplacement();
        Line *best = nullptr;
        for (auto &kv : m_lines)
        {
            Line &l = *kv.second;
            if (setOf(l.block) != set) continue;
            const coh::Cell &c = t.transition(l.state, ev);
            bool ok = !c.empty;
            for (int id : c.actions) { auto op = m_policy->action(id).op; if (op == coh::L1Op::Stall || op == coh::L1Op::Fault) ok = false; }
            if (!ok) continue;
            if (!best || l.lru < best->lru) best = &l;
        }
        if (!best) return false;
        bool need_way;
        if (!resources(best->state, ev, true, need_way)) return false;
        m_c.replacements++;
        execute(best->block, ev, nullptr, nullptr, cycle);
        return true;
    }

    bool CacheMaster::processCpu(uint64_t cycle)
    {
        const coh::Table &t = m_policy->table();
        const int stall = t.actionId("Stall"), fault = t.actionId("Fault");
        for (size_t i = 0; i < m_window.size(); i++)
        {
            CpuOp &op = m_window[i];
            if (op.done || op.not_before > cycle) continue;
            uint64_t block = blockOf(op.addr);
            Line *l = line(block);
            if (l && l->parked == int64_t(op.seq)) continue;   // waiting for its Grant
            // program order per block: an older access to the block goes first
            bool older = false;
            for (size_t j = 0; j < i; j++) if (!m_window[j].done && blockOf(m_window[j].addr) == block) older = true;
            if (older) continue;
            int st = stateOf(block);
            if (op.kind == CpuOp::Evict)
            {
                // A CPU-side eviction request: the line's Replacement, when the table can run it now.
                const int ev = m_policy->eventReplacement();
                if (!l || (fault >= 0 && t.hasAction(st, ev, fault))) { op.done = true; op.t_issue = op.t_done = cycle; m_c.evicts++; continue; }
                if (stall >= 0 && t.hasAction(st, ev, stall)) { m_c.stalls++; continue; }
                bool need_way;
                if (!resources(st, ev, true, need_way)) continue;
                execute(block, ev, nullptr, nullptr, cycle);
                op.done = true; op.t_issue = op.t_done = cycle; m_c.evicts++;
                return true;
            }
            coh::Decoded d = m_policy->decodeCpu(op.kind == CpuOp::Store);
            if (stall >= 0 && t.hasAction(st, d.event, stall)) { m_c.stalls++; continue; }
            bool need_way;
            if (!resources(st, d.event, l != nullptr, need_way)) continue;
            if (need_way && waysUsed(setOf(block)) >= m_ways)
            {
                if (raiseReplacement(setOf(block), cycle)) return true;
                continue;
            }
            execute(block, d.event, nullptr, &op, cycle);
            return true;
        }
        return false;
    }

    void CacheMaster::retire(uint64_t cycle)
    {
        (void)cycle;
        while (!m_window.empty() && m_window.front().done)
        {
            if (m_keep) m_finished.push_back(m_window.front());
            m_window.pop_front();
        }
        // completed accesses behind an older pending one leave too (the window is a set of
        // accesses in flight, ordered per block)
        for (auto it = m_window.begin(); it != m_window.end();)
        {
            if (it->done) { if (m_keep) m_finished.push_back(*it); it = m_window.erase(it); }
            else ++it;
        }
    }

    // ------------------------------------------------------------------ the three phases
    void CacheMaster::offer(uint64_t)
    {
        Link *l = link();
        if (!l || kernel()->inReset()) return;
        for (Channel ch : {Channel::A, Channel::C, Channel::E})
        {
            OutQ &o = m_out[int(ch)];
            if (!o.q.empty()) l->chan(ch).offer(o.q.front(), o.beat);   // a burst is presented to its end (rule 2)
        }
    }

    void CacheMaster::accept(uint64_t)
    {
        Link *l = link();
        if (!l) return;
        // §1.2 rule 2: the receive queues always accept (outside reset), whatever is parked.
        bool r = !kernel()->inReset();
        l->chan(Channel::B).setReady(r);
        l->chan(Channel::D).setReady(r);
    }

    void CacheMaster::commit(uint64_t cycle)
    {
        Link *l = link();
        if (!l) return;
        for (Channel ch : {Channel::A, Channel::C, Channel::E})
        {
            const Beat &b = l->chan(ch).accepted();
            if (!b.msg) continue;
            OutQ &o = m_out[int(ch)];
            if (b.last()) { o.q.pop_front(); o.beat = 0; } else o.beat = b.index + 1;
        }
        for (Channel ch : {Channel::B, Channel::D})
        {
            const Beat &b = l->chan(ch).accepted();
            if (b.msg && b.last()) (ch == Channel::B ? m_rxB : m_rxD).push_back(b.msg);   // the whole message is in
        }
        if (kernel()->inReset()) return;
        // the CPU side: fill the window in program order
        while (m_window.size() < m_outstanding)
        {
            CpuOp op;
            if (!m_cpu->next(op, cycle, m_last_issue, rng())) break;
            if ((op.addr & (op.bytes - 1)) || blockOf(op.addr) != blockOf(op.addr + op.bytes - 1))
                fatal("CacheMaster " + m_name + ": access at 0x" + std::to_string(op.addr) + " of " + std::to_string(op.bytes) + " bytes is not aligned inside one block");
            op.seq = m_seq++;
            m_last_issue = std::max(cycle, op.not_before);
            m_window.push_back(op);
        }
        // priority D > B > the CPU side (plan §5.2: a stalled B never blocks D)
        processD(cycle);
        processB(cycle);
        processCpu(cycle);
        retire(cycle);
    }
}}
