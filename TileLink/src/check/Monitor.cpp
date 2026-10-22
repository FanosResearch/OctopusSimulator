/*
 * File  :      Monitor.cpp
 * The per-link conformance monitor: every rule of docs/TileLink_Monitor_Rules.md, cited by its
 * catalogue id and spec section at the check. See Monitor.h for the rule table and the order.
 */
#include "tilelink/Monitor.h"
#include "tilelink/agents/Xbar.h"
#include <algorithm>
#include <cstdio>
#include <sstream>

namespace octopus { namespace tl
{
    // ------------------------------------------------------------------ options, formatting
    bool MonitorOptions::parseMode(const std::string &s, Mode &m)
    {
        if (s == "fatal")  { m = Fatal;  return true; }
        if (s == "report") { m = Report; return true; }
        return false;
    }
    const char *MonitorOptions::modeName(Mode m) { return m == Fatal ? "fatal" : "report"; }

    std::string Violation::line() const
    {
        return rule + " " + section + " link=" + link + " cycle=" + std::to_string(cycle) + " ch=" + channelName(ch) +
               " msg=" + msg + ": " + text;
    }

    static std::string hex(uint64_t v)
    {
        char buf[32];
        std::snprintf(buf, sizeof buf, "0x%llx", (unsigned long long)v);
        return buf;
    }

    std::string Monitor::msgText(const Msg *m, unsigned beat) const
    {
        if (!m) return "-";
        std::ostringstream o;
        o << opcodeName(m->channel, m->opcode) << "[param=" << unsigned(m->param) << " size=" << unsigned(m->size)
          << " src=" << m->source << " sink=" << m->sink << " addr=" << hex(m->address) << " beat=" << beat << "]";
        return o.str();
    }

    // ------------------------------------------------------------------ construction
    Monitor::Monitor(Kernel &k, Link &l, const MonitorOptions &o) : m_k(k), m_l(l), m_o(o), m_p(l.params())
    {
        m_max_size = m_o.max_size < 0 ? m_p.maxSize() : unsigned(m_o.max_size);
        m_l.forEachChannel([&](ChannelState &c)
        {
            const int i = static_cast<int>(c.id());
            m_prev_offer[i] = c.onOffer; m_prev_accept[i] = c.onAccept; m_prev_violation[i] = c.onViolation;
            c.onOffer = [this, i](const ChannelState &cs, const Beat &b, uint64_t t) { if (m_prev_offer[i]) m_prev_offer[i](cs, b, t); onOffer(cs, b, t); };
            c.onAccept = [this, i](const ChannelState &cs, const Beat &b, uint64_t t) { if (m_prev_accept[i]) m_prev_accept[i](cs, b, t); onAccept(cs, b, t); };
            // The monitor owns the core's violation path (plan §3.6: reported, not exited, in report mode).
            c.onViolation = [this](const ChannelState &cs, const std::string &w, uint64_t t) { onCoreViolation(cs, w, t); };
        });
    }

    Monitor::~Monitor()
    {
        m_l.forEachChannel([&](ChannelState &c)
        {
            const int i = static_cast<int>(c.id());
            c.onOffer = m_prev_offer[i]; c.onAccept = m_prev_accept[i]; c.onViolation = m_prev_violation[i];
        });
    }

    std::string Monitor::checkResetCycles(uint64_t reset_cycles, bool standalone)
    {
        // M-RESET-2, §3.2.2: reset asserted for at least 100 cycles standalone (0 only inside Octopus, plan §3.2).
        if (!standalone || reset_cycles >= 100) return "";
        return "M-RESET-2 §3.2.2 link=- cycle=- ch=- msg=-: reset_cycles=" + std::to_string(reset_cycles) + " < 100 (standalone minimum)";
    }

    void Monitor::offerOnMissingChannel(Channel ch, const Msg &m, unsigned beat, uint64_t cycle)
    {
        // M-LEVEL-4, §3.4, §3.5, §3.7, Table 1: a TL-UL or TL-UH link has no B, C or E channel.
        if (m_l.has(ch)) return;
        report("M-LEVEL-4", "§3.4, §3.5, §3.7, Table 1", ch, &m, beat, cycle,
               std::string("beat on ") + channelName(ch) + " of a TL-" + levelName(m_p.level) + " link");
    }

    std::vector<std::unique_ptr<Monitor>> Monitor::attachAll(Kernel &k, const MonitorOptions &o)
    {
        std::vector<std::unique_ptr<Monitor>> v;
        for (auto &l : k.links())
        {
            MonitorOptions lo = o;
            const SlaveLimits *sl = dynamic_cast<const SlaveLimits *>(l->slave());
            if (sl)
            {
                if (lo.max_size < 0) lo.max_size = sl->declaredMaxSize(*l);
                if (lo.regions.empty()) lo.regions = sl->declaredRegions(*l);
            }
            v.emplace_back(new Monitor(k, *l, lo));
        }
        return v;
    }
    void Monitor::finishAll(std::vector<std::unique_ptr<Monitor>> &ms, uint64_t end_cycle)
    {
        for (auto &m : ms) m->finish(end_cycle);
    }
    uint64_t Monitor::totalViolations(const std::vector<std::unique_ptr<Monitor>> &ms)
    {
        uint64_t n = 0;
        for (auto &m : ms) n += m->violations();
        return n;
    }

    uint64_t Monitor::count(const std::string &rule) const
    {
        auto it = m_counts.find(rule);
        return it == m_counts.end() ? 0 : it->second;
    }

    std::string Monitor::summary() const
    {
        std::string s = "monitor " + m_l.name() + ": " + std::to_string(m_log.size()) + " violation" + (m_log.size() == 1 ? "" : "s");
        if (!m_counts.empty())
        {
            s += " (";
            bool first = true;
            for (auto &kv : m_counts) { s += (first ? "" : ", ") + kv.first + ": " + std::to_string(kv.second); first = false; }
            s += ")";
        }
        return s;
    }

    // Catalogue §0.2: reported once per (message, beat, rule). Fatal mode exits on the first.
    bool Monitor::report(const std::string &rule, const std::string &section, Channel ch, const Msg *m,
                         unsigned beat, uint64_t cycle, const std::string &text)
    {
        auto rk = std::make_tuple(rule, m ? key(*m) : NEVER, beat, static_cast<int>(ch));
        if (!m_reported.insert(rk).second) return false;
        Violation v;
        v.rule = rule; v.section = section; v.link = m_l.name(); v.cycle = cycle; v.ch = ch;
        v.msg = msgText(m, beat); v.text = text;
        m_log.push_back(v);
        m_counts[rule]++;
        if (m_o.print && m_o.out) *m_o.out << v.line() << std::endl;
        if (m_o.mode == MonitorOptions::Fatal) fatal(v.line());
        return true;
    }

    // ------------------------------------------------------------------ helpers
    std::vector<uint64_t> Monitor::blockSet(uint64_t address, unsigned size) const
    {
        // Catalogue §0.4: the blocks intersecting [address, address + 2^size).
        std::vector<uint64_t> v;
        unsigned lg = 0;
        while ((1ull << lg) < m_o.block_bytes) lg++;
        uint64_t last = address + (uint64_t(1) << size) - 1;
        for (uint64_t b = address >> lg; b <= (last >> lg); b++) { v.push_back(b); if (v.size() > 4096) break; }
        return v;
    }

    char &Monitor::permOf(uint64_t block, uint64_t cl)
    {
        auto it = m_perm.find({block, cl});
        if (it == m_perm.end()) it = m_perm.emplace(std::make_pair(block, cl), 'N').first;   // N at reset (catalogue §12)
        return it->second;
    }
    unsigned Monitor::lgBlock() const
    {
        unsigned lg = 0;
        while ((1ull << lg) < m_o.block_bytes) lg++;
        return lg;
    }
    char Monitor::heldPerm(uint64_t block, uint64_t cl)
    {
        auto it = m_prune.find({block, cl});
        return it != m_prune.end() ? it->second : permOf(block, cl);
    }
    char Monitor::perm(uint64_t address) const { return perm(address, 0); }
    char Monitor::perm(uint64_t address, uint64_t source) const
    {
        unsigned lg = 0;
        while ((1ull << lg) < m_o.block_bytes) lg++;
        auto it = m_perm.find({address >> lg, client(source)});
        return it == m_perm.end() ? 'N' : it->second;
    }

    uint64_t Monitor::client(uint64_t source) const
    {
        auto c = m_client.find(source);
        if (c != m_client.end()) return c->second;
        // §9.2's concurrency limits bind each master; a link behind an Xbar carries several. Trace the
        // source upstream to the leaf master that owns it (the topology is fixed after elaboration).
        const Link *l = &m_l;
        uint64_t s = source, result = 0;
        for (int depth = 0; depth < 64; depth++)
        {
            const Agent *m = l->master();
            std::vector<const Link *> ups;
            for (auto &lr : m->links()) if (lr.role == Role::Slave) ups.push_back(lr.link);
            result = m->id();
            if (ups.empty()) break;                                    // a leaf master
            if (auto *x = dynamic_cast<const Xbar *>(m))
            {
                const Link *hit = nullptr; uint64_t base = 0;
                for (const Link *u : ups)
                {
                    uint64_t b = x->sourceBase(*u), n = u->params().numSources();
                    if (s >= b && s - b < n) { hit = u; base = b; break; }
                }
                if (!hit) break;                                       // outside every range: the Xbar itself
                l = hit; s -= base;                                    // §6.4: the upstream link's own namespace
                continue;
            }
            if (ups.size() == 1) { l = ups[0]; continue; }             // a pass-through adapter: same source
            break;                                                     // an agent of another kind: one master
        }
        m_client[source] = result;
        return result;
    }

    std::map<uint64_t, uint8_t>::const_iterator Monitor::ofClient(const std::map<uint64_t, uint8_t> &set, uint64_t cl) const
    {
        for (auto it = set.begin(); it != set.end(); ++it) if (client(it->first) == cl) return it;
        return set.end();
    }

    bool Monitor::isResponseOpcode(Channel ch, unsigned op)
    {
        // Catalogue §9: D responses are AccessAck, AccessAckData, HintAck, Grant, GrantData,
        // ReleaseAck; C responses are AccessAck, AccessAckData, HintAck, ProbeAck, ProbeAckData;
        // E GrantAck. (Grant[Data] is also a request, handled in both roles.)
        switch (ch)
        {
            case Channel::D: return op <= 2 || op == 4 || op == 5 || op == 6;
            case Channel::C: return op <= 2 || op == 4 || op == 5;
            case Channel::E: return true;
            default: return false;
        }
    }

    Monitor::Out *Monitor::findA(uint64_t source)
    {
        auto it = m_outA.find(source);
        return (it == m_outA.end() || it->second.empty()) ? nullptr : &it->second.front();
    }
    Monitor::Out *Monitor::findC(uint64_t source)
    {
        auto it = m_outC.find(source);
        return (it == m_outC.end() || it->second.empty()) ? nullptr : &it->second.front();
    }
    Monitor::Out *Monitor::findGrant(uint64_t sink)
    {
        auto it = m_grants.find(sink);
        return (it == m_grants.end() || it->second.empty()) ? nullptr : &it->second.front();
    }

    void Monitor::eraseEntry(int table, const Out &e)
    {
        std::map<uint64_t, std::deque<Out>> *tab = table == 0 ? &m_outA : table == 1 ? &m_outC : table == 2 ? &m_outB : &m_grants;
        uint64_t key = table == 2 ? e.address : table == 3 ? e.sink : e.source;
        auto it = tab->find(key);
        if (it == tab->end()) return;
        auto &dq = it->second;
        for (auto q = dq.begin(); q != dq.end(); ++q)
            if (q->msg_id == e.msg_id) { dq.erase(q); break; }
        if (dq.empty()) tab->erase(it);
    }

    // ------------------------------------------------------------------ core hook
    void Monitor::onCoreViolation(const ChannelState &c, const std::string &what, uint64_t cycle)
    {
        // The core reports the reset rule (Kernel::checkReset, after every offer of the cycle) and
        // the burst-lock rules (ChannelState::offer, before onOffer). The monitor checks all of them
        // itself, in the catalogue's order (§0.2), so the lock reports are dropped here and the reset
        // report is deduplicated against onOffer's M-RESET-1 by (message, beat, rule). Anything else
        // the core refuses (two offers in one cycle, a message for another channel) is not a rule of
        // the catalogue: it is reported as M-CORE so that report mode still counts it.
        const Beat &b = c.offered();
        if (what.find("during reset") != std::string::npos)
        {   // M-RESET-1, §3.2.2
            report("M-RESET-1", "§3.2.2", c.id(), b.msg.get(), b.index, cycle,
                   "valid HIGH on " + std::string(channelName(c.id())) + " during reset (cycle " + std::to_string(cycle) +
                   " of " + std::to_string(m_k.options().reset_cycles) + ")");
            return;
        }
        if (what.find("burst in progress") != std::string::npos || what.find("control fields changed") != std::string::npos ||
            what.find("offered but the message has") != std::string::npos)
            return;   // M-BURST-1/2/3: checked by checkBurstOffer
        report("M-CORE", "§4.1", c.id(), nullptr, 0, cycle, what);
    }

    // ------------------------------------------------------------------ offer
    void Monitor::onOffer(const ChannelState &c, const Beat &b, uint64_t cycle)
    {
        Channel ch = c.id();
        const Msg &m = *b.msg;
        if (b.index == 0 && !m_first_offer.count(key(m))) m_first_offer[key(m)] = cycle;
        // M-RESET-1, §3.2.2: valid LOW on every channel while reset is asserted.
        if (m_k.inReset())
            report("M-RESET-1", "§3.2.2", ch, &m, b.index, cycle,
                   "valid HIGH on " + std::string(channelName(ch)) + " during reset (cycle " + std::to_string(cycle) + " of " +
                   std::to_string(m_k.options().reset_cycles) + ")");
        checkStatic(ch, m, b.index, cycle);
        checkBurstOffer(ch, m, b.index, cycle);
        trackRetraction(ch, b.msg, b.index, cycle);
        // A ProbeAck[Data] (4, 5) or Release[Data] (6, 7) offered: the master already holds the pruned
        // permission (see m_prune). A re-offer after a retraction overwrites it with the latest param.
        if (b.index == 0 && ch == Channel::C && m_p.level == Level::C && m.opcode >= 4 && m.param <= 5)
            for (uint64_t blk : blockSet(m.address, m.size)) m_prune[{blk, client(m.source)}] = pruneTarget(m.param);
        // M-BURST-5 / M-RESP-7 bookkeeping: a response beat 0 offered marks its request.
        if (b.index == 0 && isResponseOpcode(ch, m.opcode))
        {
            Out *e = nullptr;
            if (ch == Channel::D) e = (m.opcode == unsigned(OpD::ReleaseAck)) ? findC(m.source) : findA(m.source);
            else if (ch == Channel::E) e = findGrant(m.sink);
            else if (ch == Channel::C)
            {
                auto it = m_outB.find(m.address);
                if (it != m_outB.end() && !it->second.empty()) e = &it->second.front();
            }
            if (e) e->resp_offered = true;
        }
    }

    void Monitor::checkStatic(Channel ch, const Msg &m, unsigned beat, uint64_t cycle)
    {
        const Level level = m_p.level;
        const unsigned op = m.opcode;
        const std::string opn = opcodeName(ch, op);
        const bool ul = level == Level::UL;

        // ---- M-LEVEL (§1.1 Table 1, Tables 12, 13, §7, §7.2)
        if (ch == Channel::A && !legalOn(level, ch, op))
        {   // M-LEVEL-1, Tables 12, 13; §7, §8, §9: the A opcode is legal at the link's level
            report("M-LEVEL-1", "Tables 12, 13", ch, &m, beat, cycle, "A opcode " + std::to_string(op) + " not legal at TL-" + levelName(level));
            return;
        }
        if (ch == Channel::D && !legalOn(level, ch, op))
        {   // M-LEVEL-2, Tables 12, 13: the D opcode is legal at the link's level (3 and 7 never)
            report("M-LEVEL-2", "Tables 12, 13", ch, &m, beat, cycle, "D opcode " + std::to_string(op) + " not legal at TL-" + levelName(level));
            return;
        }
        if ((ch == Channel::B || ch == Channel::C) && !opcodeLegal(ch, op))
        {   // M-LEVEL-3, Table 13: B opcode in 0..7, C opcode in {0,1,2,4,5,6,7}
            report("M-LEVEL-3", "Table 13", ch, &m, beat, cycle, std::string(channelName(ch)) + " opcode " + std::to_string(op) + " unassigned");
            return;
        }
        if (ch == Channel::E && op != 0)
        {   // M-LEVEL-3, Table 10: E has no opcode field (the model names GrantAck 0)
            report("M-LEVEL-3", "Table 10", ch, &m, beat, cycle, "E opcode " + std::to_string(op) + " unassigned");
            return;
        }
        // M-LEVEL-4 (a beat on B, C or E of a TL-UL/UH link) cannot reach a hook: the link has no
        // such ChannelState; it arrives through onCoreViolation from an agent that models the wire.
        if (ul && (ch == Channel::A || ch == Channel::D) && (uint64_t(1) << m.size) > m_p.w)
        {   // M-LEVEL-5, §7 "in TL-UL every message fits within a single beat", §7.2.1-§7.2.5
            report("M-LEVEL-5", "§7, §7.2", ch, &m, beat, cycle,
                   "size " + std::to_string(m.size) + " (2^s > w=" + std::to_string(m_p.w) + ") on a TL-UL link");
        }

        // ---- M-PARAM (§7.2, §8.2, §9.3, §9.5, Tables 23, 25, 27, 31, 51; plan §8 decision 7)
        if (!paramLegal(ch, op, m.param))
        {
            const std::string p = std::to_string(m.param);
            if (ch == Channel::A || ch == Channel::B)
            {
                switch (op)
                {
                    case 0: case 1: case 4:   // M-PARAM-1, §7.2.1-§7.2.3, §9.5.1-§9.5.3: reserved, 0
                        report("M-PARAM-1", "§7.2.1-§7.2.3", ch, &m, beat, cycle, opn + " param " + p + " != 0 (reserved)"); break;
                    case 2:                   // M-PARAM-2, §8.2.1 Table 23, §9.5.6
                        report("M-PARAM-2", "§8.2.1 Table 23", ch, &m, beat, cycle, "ArithmeticData param " + p + " not in 0..4"); break;
                    case 3:                   // M-PARAM-3, §8.2.2 Table 25, §9.5.7
                        report("M-PARAM-3", "§8.2.2 Table 25", ch, &m, beat, cycle, "LogicalData param " + p + " not in 0..3"); break;
                    case 5:                   // M-PARAM-4, §8.2.3 Table 27; §9.5.8 Table 51 (B: Prefetch only)
                        report("M-PARAM-4", "§8.2.3 Table 27, §9.5.8 Table 51", ch, &m, beat, cycle,
                               "Intent param " + p + " not legal on " + channelName(ch)); break;
                    case 6: case 7:
                        if (ch == Channel::A)   // M-PARAM-6, §9.3.1, §9.3.2 Tables 32, 33: Grow
                            report("M-PARAM-6", "§9.3.1, §9.3.2", ch, &m, beat, cycle, opn + " param " + p + " not a Grow");
                        else                    // M-PARAM-7, §9.3.3, §9.3.4 Tables 34, 35: Cap
                            report("M-PARAM-7", "§9.3.3, §9.3.4", ch, &m, beat, cycle, opn + " param " + p + " not a Cap");
                        break;
                }
            }
            else if (ch == Channel::C)
            {
                if (op <= 2)                  // M-PARAM-5, §9.5.4, §9.5.5, §9.5.9: reserved, 0
                    report("M-PARAM-5", "§9.5.4, §9.5.5, §9.5.9", ch, &m, beat, cycle, opn + " param " + p + " != 0 (reserved)");
                else                          // M-PARAM-8, §9.3.5, §9.3.6, §9.3.10, §9.3.11: Prune/Report, NtoN only on ProbeAck
                    report("M-PARAM-8", "§9.3.5, §9.3.6, §9.3.10, §9.3.11", ch, &m, beat, cycle,
                           opn + " param " + p + " not legal (NtoN forbidden on " + opn + ")");
            }
            else if (ch == Channel::D)
            {
                if (op <= 2)                  // M-PARAM-5, §7.2.4, §7.2.5, §8.2.4: reserved, 0
                    report("M-PARAM-5", "§7.2.4, §7.2.5, §8.2.4", ch, &m, beat, cycle, opn + " param " + p + " != 0 (reserved)");
                else if (op == 6)             // M-PARAM-10, §9.3.12 Table 43
                    report("M-PARAM-10", "§9.3.12 Table 43", ch, &m, beat, cycle, "ReleaseAck param " + p + " != 0 (reserved)");
                else                          // M-PARAM-9, §9.3.7, §9.3.8; plan §8 decision 7: toN never encoded
                    report("M-PARAM-9", "§9.3.7, §9.3.8, plan §8.7", ch, &m, beat, cycle,
                           opn + " param " + p + " (toN or unassigned) on a Grant");
            }
        }

        // ---- M-ALIGN (§4.5, §3.3-§3.7, §6.3, Table 4)
        const bool abc = ch == Channel::A || ch == Channel::B || ch == Channel::C;
        if (abc && !aligned(m.address, m.size))   // M-ALIGN-1, §4.5 "(address & (2^size - 1)) = 0 always holds"
            report("M-ALIGN-1", "§4.5", ch, &m, beat, cycle, "address " + hex(m.address) + " not aligned to size " + std::to_string(m.size));
        if (ch != Channel::E && m.size > m_p.maxSize())   // M-ALIGN-2, Table 4 (z): size < 2^z
            report("M-ALIGN-2", "Table 4", ch, &m, beat, cycle, "size " + std::to_string(m.size) + " does not fit z=" + std::to_string(m_p.z) + " bits");
        if (ch == Channel::A && m.size > m_max_size)      // M-ALIGN-3, §6.3 "must not generate operations larger than slaves support"
            report("M-ALIGN-3", "§6.3", ch, &m, beat, cycle, "size " + std::to_string(m.size) + " > slave max_size " + std::to_string(m_max_size));
        if (abc && !m_p.addressFits(m.address))           // M-ALIGN-4, Table 4 (a)
            report("M-ALIGN-4", "Table 4", ch, &m, beat, cycle, "address " + hex(m.address) + " does not fit a=" + std::to_string(m_p.a) + " bits");
        if (ch == Channel::A && !m_o.regions.empty())     // M-ALIGN-5, §6.3 "must not under- or over-run the boundaries of the slave"
        {
            uint64_t bytes = uint64_t(1) << m.size;
            bool inside = false;
            uint64_t boundary = 0;
            for (auto &r : m_o.regions)
            {
                if (r.contains(m.address, bytes)) { inside = true; break; }
                if (m.address >= r.base && m.address < r.base + r.size) boundary = r.base + r.size;   // starts inside, leaves
            }
            if (!inside)
                report("M-ALIGN-5", "§6.3", ch, &m, beat, cycle, "operation [" + hex(m.address) + "," + hex(m.address + bytes) +
                       (boundary ? ") crosses slave region boundary " + hex(boundary) : std::string(") outside every slave region")));
        }
        if (ch != Channel::E && m.source >= m_p.numSources())   // M-ALIGN-6, Table 4 (o), §6.4
            report("M-ALIGN-6", "Table 4, §6.4", ch, &m, beat, cycle, "source " + std::to_string(m.source) + " does not fit o=" + std::to_string(m_p.o) + " bits");
        if ((ch == Channel::D || ch == Channel::E) && m.sink >= m_p.numSinks() && (ch == Channel::E || op == 4 || op == 5))
        {   // M-ALIGN-7, Table 4 (i), §6.4: sink < 2^i on D (Grants: the only D sinks that mean anything, §6.4) and E
            report("M-ALIGN-7", "Table 4, §6.4", ch, &m, beat, cycle, "sink " + std::to_string(m.sink) + " does not fit i=" + std::to_string(m_p.i) + " bits");
        }

        // ---- M-MASK (§4.5): A and B only
        if (hasMask(ch) && beat < m.beats())
        {
            const unsigned w = m_p.w;
            const uint64_t bytes = uint64_t(1) << m.size;
            std::vector<uint8_t> active(w, 0);
            if (bytes >= w) for (auto &a : active) a = 1;
            else { size_t off = size_t(m.address % w); for (uint64_t i = 0; i < bytes && off + i < w; i++) active[off + size_t(i)] = 1; }
            const uint8_t *mk = m.beatMask(beat);
            uint64_t inactive_high = 0, active_low = 0, maskv = 0;
            for (unsigned i = 0; i < w && i < 64; i++)
            {
                if (mk[i]) maskv |= uint64_t(1) << i;
                if (mk[i] && !active[i]) inactive_high |= uint64_t(1) << i;
                if (!mk[i] && active[i]) active_low |= uint64_t(1) << i;
            }
            const bool partial = op == unsigned(OpA::PutPartialData);
            if (!partial && inactive_high)   // M-MASK-1, §4.5 "the mask must always be LOW for all inactive byte lanes"
                report("M-MASK-1", "§4.5", ch, &m, beat, cycle, "mask " + hex(maskv) + " HIGH on inactive lane(s) " + hex(inactive_high) +
                       " (size " + std::to_string(m.size) + ", address " + hex(m.address) + ")");
            if (!partial && active_low)      // M-MASK-2, §4.5 "for all messages other than PutPartialData ... active byte lanes must be HIGH"
                report("M-MASK-2", "§4.5", ch, &m, beat, cycle, "mask " + hex(maskv) + " LOW on active lane(s) " + hex(active_low) + " (" + opn +
                       " size " + std::to_string(m.size) + ", address " + hex(m.address) + ")");
            if (partial && inactive_high)    // M-MASK-3, §7.2.3 "any HIGH bits of a_mask must be contained within an aligned region of a_size"
                report("M-MASK-3", "§7.2.3", ch, &m, beat, cycle, "PutPartialData mask " + hex(maskv) + " outside its aligned region (size " +
                       std::to_string(m.size) + ", address " + hex(m.address) + ")");
        }

        // ---- M-RSV (reserved fields)
        if (!m.hasData() && m.beatCorrupt(beat))   // M-RSV-1, §7.2.1, §7.2.4, §8.2.3, §8.2.4, §9.3.x, §9.5.x "corrupt is reserved and must be 0"
            report("M-RSV-1", "§7.2.1, §7.2.4, §8.2.3, §8.2.4, §9.3, §9.5", ch, &m, beat, cycle, "corrupt HIGH on dataless " + opn);
        if (ch == Channel::D && op == unsigned(OpD::ReleaseAck) && m.denied)   // M-RSV-2, §9.3.12 Table 43 "d_denied is reserved and must be 0"
            report("M-RSV-2", "§9.3.12 Table 43", ch, &m, beat, cycle, "ReleaseAck denied HIGH (reserved)");

        // ---- M-ERR (§4.4)
        if (ch != Channel::D && m.denied)   // M-ERR-1, §4.4 "denied is only present on the D channel"
            report("M-ERR-1", "§4.4", ch, &m, beat, cycle, std::string("denied set on ") + channelName(ch) + " (D-only field)");
        if (ch == Channel::D && m.denied && m.hasData() && !m.beatCorrupt(beat))
        {   // M-ERR-2, §4.4 "when a response message that carries data is denied, it must mark all beats of the message as corrupt"
            report("M-ERR-2", "§4.4, §7.2.5, §9.3.8", ch, &m, beat, cycle, "denied " + opn + " beat " + std::to_string(beat) + " with corrupt LOW");
        }
        // M-ERR-3, §4.4, §9.3.7, §9.3.8: a denied Grant changes no permission; a state rule applied
        // in acceptLast (the M-PERM exemption), no report of its own.
    }

    void Monitor::checkBurstOffer(Channel ch, const Msg &m, unsigned beat, uint64_t cycle)
    {
        ChanView &cv = m_cv[static_cast<int>(ch)];
        const std::string opn = opcodeName(ch, m.opcode);
        if (cv.lock)
        {
            if (cv.lock.get() != &m)
            {   // M-BURST-3, §4 "once a burst has begun, the sender must not send beats for any other message"; §4.1
                report("M-BURST-3", "§4, §4.1", ch, &m, beat, cycle, "beat of " + opn + " src " + std::to_string(m.source) + " offered while " +
                       opcodeName(ch, cv.lock->opcode) + " src " + std::to_string(cv.lock->source) + " in progress (beat " +
                       std::to_string(cv.next) + " of " + std::to_string(cv.total) + ")");
                return;
            }
            if (beat != cv.next)
            {   // M-BURST-1, §4 "the duration of a burst is determined by the size field"; §4.1 "only accepted beats cause the burst to make progress"
                report("M-BURST-1", "§4, §4.1, §4.4", ch, &m, beat, cycle, "beat index " + std::to_string(beat) + " offered, expected " +
                       std::to_string(cv.next) + " (" + opn + " size " + std::to_string(m.size) + ", " + std::to_string(cv.total) + " beats)");
            }
            if (m.header() != cv.lock_hdr)
            {   // M-BURST-2, §4.1 "control signals identical to those of the first beat"; §8.3
                const MsgHeader &h = cv.lock_hdr;
                std::string what;
                auto diff = [&](const char *name, uint64_t a, uint64_t b) { if (a != b) what += std::string(what.empty() ? "" : ", ") + name + ": " + hex(a) + " -> " + hex(b); };
                diff("opcode", h.opcode, m.opcode); diff("param", h.param, m.param); diff("size", h.size, m.size);
                diff("source", h.source, m.source); diff("sink", h.sink, m.sink); diff("address", h.address, m.address);
                diff("denied", h.denied, m.denied);
                report("M-BURST-2", "§4.1, §8.3", ch, &m, beat, cycle, "control field changed within burst: " + what);
            }
        }
        else if (beat != 0 || beat >= m.beats())
        {   // M-BURST-1: no burst in progress, beat 0 expected; a beat beyond the message's count never exists
            report("M-BURST-1", "§4, §4.1, §4.4", ch, &m, beat, cycle, "beat index " + std::to_string(beat) + " offered, expected 0 (" + opn +
                   " size " + std::to_string(m.size) + ", " + std::to_string(m.beats()) + " beats)");
        }
    }

    void Monitor::trackRetraction(Channel ch, const MsgPtr &m, unsigned beat, uint64_t cycle)
    {
        // M-BURST-5 bookkeeping (§4.1 Figure 7): the previous first-beat offer on this channel, if
        // rejected and followed by another message, was retracted; a retracted response must still
        // be accepted eventually. A rejected beat of a burst in progress is not a retraction (the
        // burst stays in progress, M-BURST-4).
        ChanView &cv = m_cv[static_cast<int>(ch)];
        if (cv.off_valid && !cv.off_accepted && cv.off_beat == 0 && cv.off_msg != m && isResponseOpcode(ch, cv.off_msg->opcode))
        {
            Out o; o.ch = ch; o.opcode = cv.off_msg->opcode; o.source = cv.off_msg->source; o.sink = cv.off_msg->sink;
            o.address = cv.off_msg->address; o.size = cv.off_msg->size; o.msg_id = key(*cv.off_msg);
            m_retracted.emplace(key(*cv.off_msg), std::make_pair(o, cv.off_cycle));
        }
        cv.off_valid = true; cv.off_accepted = false; cv.off_msg = m; cv.off_beat = beat; cv.off_cycle = cycle;
    }

    // ------------------------------------------------------------------ accept
    void Monitor::onAccept(const ChannelState &c, const Beat &b, uint64_t cycle)
    {
        Channel ch = c.id();
        const Msg &m = *b.msg;
        ChanView &cv = m_cv[static_cast<int>(ch)];
        cv.off_accepted = true;
        if (b.index == 0)
        {
            m_retracted.erase(key(m));   // M-BURST-5: a retracted response re-offered and accepted
            if (m.beats() > 1) { cv.lock = b.msg; cv.lock_hdr = m.header(); cv.next = 1; cv.total = m.beats(); }
            const bool request = m.isRequest();                 // A, B, C Release*, D Grant*
            const bool response = isResponseOpcode(ch, m.opcode);
            if (response) acceptResponseFirst(ch, m, cycle);     // a Grant is matched to its Acquire first (M-RESP)
            if (request) acceptRequestFirst(ch, m, cycle);       // then enters the tables as a request (M-ID-4)
            m_first_offer.erase(key(m));
        }
        else if (cv.lock.get() == &m) cv.next = b.index + 1;
        if (b.last())
        {
            if (cv.lock.get() == &m) { cv.lock = nullptr; cv.next = cv.total = 0; }
            acceptLast(ch, m, cycle);
        }
    }

    void Monitor::acceptRequestFirst(Channel ch, const Msg &m, uint64_t cycle)
    {
        const unsigned op = m.opcode;
        const std::string opn = opcodeName(ch, op);
        Out e; e.ch = ch; e.opcode = m.opcode; e.param = m.param; e.size = m.size; e.source = m.source; e.sink = m.sink;
        e.address = m.address; e.denied = m.denied; e.msg_id = key(m); e.t_accept = cycle;
        auto fo = m_first_offer.find(key(m));
        e.t_offer = fo == m_first_offer.end() ? cycle : fo->second;
        const bool tlc = m_p.level == Level::C;

        if (ch == Channel::A)
        {
            // M-ID-1, §6.4 "that value cannot be reused within each channel while the request on that channel is unanswered"
            if (Out *o = findA(m.source))
                report("M-ID-1", "§6.4", ch, &m, 0, cycle, "A source " + std::to_string(m.source) + " reused while outstanding (" +
                       opcodeName(Channel::A, o->opcode) + " accepted at cycle " + std::to_string(o->t_accept) + ")");
            m_outA[m.source].push_back(e);
            if (tlc)
            {
                const bool acquire = op == 6 || op == 7;
                const bool access = op <= 4;   // Get, Put*, Atomics (Intent is not in the §9.2 list)
                const uint64_t cl = client(m.source);   // §9.2 binds each master: this request's
                for (uint64_t blk : blockSet(m.address, m.size))
                {
                    Pend &p = pend(blk);
                    auto acq = ofClient(p.acq, cl), acc = ofClient(p.acc, cl), rel = ofClient(p.rel, cl);
                    if (acquire)
                    {
                        // M-CONC-1, §9.2 Acquire "should not issue an Acquire if there is a pending Grant, GrantData"; plan §1.4 one Acquire per block
                        if (acq != p.acq.end() && !m_o.allow_acquire_under_acquire)
                            report("M-CONC-1", "§9.2", ch, &m, 0, cycle, "Acquire on block " + hex(blk) + " while Acquire src " +
                                   std::to_string(acq->first) + " pending (Grant outstanding)");
                        // M-CONC-2, §9.2 Acquire "... AccessAck, or AccessAckData on the block"
                        if (acc != p.acc.end())
                            report("M-CONC-2", "§9.2", ch, &m, 0, cycle, "Acquire on block " + hex(blk) + " while " +
                                   opcodeName(Channel::A, acc->second) + " src " + std::to_string(acc->first) + " pending");
                    }
                    else if (access)
                    {
                        // M-CONC-3, §9.2 Acquire "the master should not issue an Acquire, Get, Put, ArithmeticData, or LogicalData on that block until it receives a Grant"
                        if (acq != p.acq.end())
                            report("M-CONC-3", "§9.2", ch, &m, 0, cycle, opn + " on block " + hex(blk) + " while Acquire src " +
                                   std::to_string(acq->first) + " pending");
                    }
                    // M-CONC-6, §9.2 Release "... Acquire, Get, Put, ArithmeticData, LogicalData, or further Release on the block until it receives a ReleaseAck"
                    if ((acquire || access) && rel != p.rel.end())
                        report("M-CONC-6", "§9.2", ch, &m, 0, cycle, opn + " on block " + hex(blk) + " while Release src " +
                               std::to_string(rel->first) + " pending");
                    if (acquire)
                    {
                        // M-PERM-1, §9.1.4 Grow "notes both the previous permissions and the new"; §9.3.1, §9.3.2
                        char P = heldPerm(blk, cl);
                        char from = m.param == unsigned(Grow::BtoT) ? 'B' : 'N';
                        static const char *grow[3] = {"NtoB", "NtoT", "BtoT"};
                        // A Probe to this master on this block still outstanding: the master may have
                        // answered it internally (its permission at or below both P and the probe's cap)
                        // while its ProbeAck is not yet on C. A and C are independent and §9.2 orders
                        // no ProbeAck before a later Acquire, so such an Acquire is legal; its `from`
                        // must still not exceed what the master could hold.
                        auto rank = [](char x) { return x == 'T' ? 2 : x == 'B' ? 1 : 0; };
                        bool probed_ok = false;
                        auto ob = m_outB.find(blk << lgBlock());
                        if (P != from && ob != m_outB.end())
                            for (auto &o : ob->second)
                                if ((o.opcode == 6 || o.opcode == 7) && o.param <= 2 && client(o.source) == cl)
                                {
                                    int cap = o.param == unsigned(Cap::toT) ? 2 : o.param == unsigned(Cap::toB) ? 1 : 0;
                                    if (rank(from) <= std::min(rank(P), cap)) probed_ok = true;
                                }
                        // Overtaken (Figure 29): issued at a permission held since the last Grant/ReleaseAck.
                        auto sc = m_since.find({blk, cl});
                        char S = sc == m_since.end() ? P : sc->second;
                        bool overtaken_ok = rank(from) >= rank(P) && rank(from) <= rank(S);
                        if (P != from && !probed_ok && !overtaken_ok)
                            report("M-PERM-1", "§9.1.4, §9.3.1, §9.3.2", ch, &m, 0, cycle, std::string("Acquire ") + (m.param <= 2 ? grow[m.param] : "?") +
                                   " from tracked permission " + P + " on block " + hex(blk));
                    }
                    // pend.acc holds the accesses §9.2 names ("a pending ... AccessAck, or AccessAckData"):
                    // Get, Put, Atomics. An Intent (a prefetch hint or a CMO) is answered by a HintAck, which
                    // no §9.2 sentence lists, so it blocks neither an Acquire (M-CONC-2) nor a Release (M-CONC-4).
                    if (acquire) p.acq[m.source] = uint8_t(op); else if (access) p.acc[m.source] = uint8_t(op);
                }
            }
        }
        else if (ch == Channel::B)
        {
            // M-ID-3, §6.4 "the combination of b_source and b_address be unique"
            auto &dq = m_outB[m.address];
            for (auto &o : dq)
                if (o.source == m.source)
                { report("M-ID-3", "§6.4", ch, &m, 0, cycle, "B (source " + std::to_string(m.source) + ", address " + hex(m.address) + ") reused while outstanding"); break; }
            const bool probe = op == 6 || op == 7;
            if (probe)
                for (uint64_t blk : blockSet(m.address, m.size))
                {
                    Pend &p = pend(blk);
                    // M-CONC-8, §9.2 Grant "once the Grant or GrantData is issued, the slave should not issue Probes on that block until it receives a GrantAck"
                    if (!p.gack.empty())
                        report("M-CONC-8", "§9.2", ch, &m, 0, cycle, "Probe on block " + hex(blk) + " while Grant sink " +
                               std::to_string(p.gack.begin()->first) + " awaits GrantAck");
                    // M-CONC-9, §9.2 Probe "should not issue further Probes on that block until it receives a ProbeAck or ProbeAckData":
                    // per master (plan §1.4); probing several masters of one block at once is legal (§6.4 B relaxation)
                    auto pr = ofClient(p.probe, client(m.source));
                    if (pr != p.probe.end())
                        report("M-CONC-9", "§9.2", ch, &m, 0, cycle, "second Probe on block " + hex(blk) + " while Probe src " +
                               std::to_string(pr->first) + " pending");
                    p.probe[m.source] = uint8_t(op);
                }
            dq.push_back(e);
        }
        else if (ch == Channel::C)
        {
            // C requests: Release, ReleaseData (their own namespace, §6.4)
            // M-ID-2, §6.4 "separate namespaces" for A and C sources
            if (findC(m.source))
                report("M-ID-2", "§6.4", ch, &m, 0, cycle, "C source " + std::to_string(m.source) + " reused while outstanding");
            m_outC[m.source].push_back(e);
            const uint64_t cl = client(m.source);
            for (uint64_t blk : blockSet(m.address, m.size))
            {
                Pend &p = pend(blk);
                auto acq = ofClient(p.acq, cl), acc = ofClient(p.acc, cl), rel = ofClient(p.rel, cl);
                // M-CONC-4, §9.2 Release "should not issue a Release if there is a pending Grant, GrantData, AccessAck, or AccessAckData on the block"
                if (acq != p.acq.end())
                    report("M-CONC-4", "§9.2", ch, &m, 0, cycle, "Release on block " + hex(blk) + " while " + opcodeName(Channel::A, acq->second) +
                           " src " + std::to_string(acq->first) + " pending");
                else if (acc != p.acc.end())
                    report("M-CONC-4", "§9.2", ch, &m, 0, cycle, "Release on block " + hex(blk) + " while " + opcodeName(Channel::A, acc->second) +
                           " src " + std::to_string(acc->first) + " pending");
                // M-CONC-6, §9.2 Release "... further Release on the block until it receives a ReleaseAck"
                if (rel != p.rel.end())
                    report("M-CONC-6", "§9.2", ch, &m, 0, cycle, opn + " on block " + hex(blk) + " while Release src " +
                           std::to_string(rel->first) + " pending");
                // M-PERM-2, §9.1.4 Prune/Report "notes both the previous permissions and the new, lesser permissions"; §9.3.10, §9.3.11
                char P = permOf(blk, cl);
                static const char *pr[6] = {"TtoB", "TtoN", "BtoN", "TtoT", "BtoB", "NtoN"};
                char from = m.param <= 1 || m.param == 3 ? 'T' : m.param == 2 || m.param == 4 ? 'B' : 'N';
                if (m.param <= 5 && P != from)
                    report("M-PERM-2", "§9.1.4, §9.3.10, §9.3.11", ch, &m, 0, cycle, opn + " " + pr[m.param] + " from tracked permission " + P +
                           " on block " + hex(blk));
                if (m.param <= 5) { permOf(blk, cl) = pruneTarget(m.param); m_prune.erase({blk, cl}); }   // committed at beat 0
                p.rel[m.source] = uint8_t(op);
            }
        }
        else if (ch == Channel::D)
        {
            // D requests: Grant, GrantData (also responses, matched in acceptResponseFirst)
            // M-ID-4, §6.4 "Channel D must provide unique d_sink transaction identifiers for unanswered Grant requests"
            if (findGrant(m.sink))
                report("M-ID-4", "§6.4", ch, &m, 0, cycle, "D sink " + std::to_string(m.sink) + " reused while its GrantAck is pending");
            // The Grant's block is its Acquire's (D carries no address): take it from the matched request.
            Out *req = findA(m.source);
            if (req) { e.address = req->address; e.size = req->size; }
            m_grants[m.sink].push_back(e);
            if (req)
                for (uint64_t blk : blockSet(e.address, e.size))
                {
                    Pend &p = pend(blk);
                    // M-CONC-7, §9.2 Grant "should not issue a Grant or GrantData if there is a pending ProbeAck or ProbeAckData on the block"; Figure 30
                    if (!p.probe.empty())
                        report("M-CONC-7", "§9.2, Figure 30", ch, &m, 0, cycle, "Grant on block " + hex(blk) + " while Probe src " +
                               std::to_string(p.probe.begin()->first) + " pending");
                    p.gack[m.sink] = uint8_t(op);
                }
        }
    }

    void Monitor::acceptResponseFirst(Channel ch, const Msg &m, uint64_t cycle)
    {
        const unsigned op = m.opcode;
        const std::string opn = opcodeName(ch, op);
        ChanView &cv = m_cv[static_cast<int>(ch)];
        cv.resp_active = false;
        Out *req = nullptr;
        int table = -1;

        if (ch == Channel::D)
        {
            // M-RESP-2, §6.4, §4.2 "not before", §5.1: a D beat matches an outstanding request in its namespace
            // (AccessAck, AccessAckData, HintAck, Grant, GrantData answer outA; ReleaseAck answers outC).
            const bool releaseAck = op == unsigned(OpD::ReleaseAck);
            req = releaseAck ? findC(m.source) : findA(m.source);
            table = releaseAck ? 1 : 0;
            if (!req)
            {
                report("M-RESP-2", "§6.4, §4.2, §5.1", ch, &m, 0, cycle, "unsolicited " + opn + " src " + std::to_string(m.source) + ": no outstanding " +
                       (releaseAck ? "C" : "A") + " request (response before its request, or never requested)");
                return;
            }
        }
        else if (ch == Channel::C)
        {
            // M-RESP-3, Table 14, §9.5.4 "c_address must match the b_address from the request": matched by address,
            // the oldest B request at that address whose opcode admits the response (c_source is not used, §6.4).
            auto it = m_outB.find(m.address);
            if (it == m_outB.end() || it->second.empty())
            {
                report("M-RESP-3", "Table 14, §9.5.4", ch, &m, 0, cycle, "unsolicited " + opn + " addr " + hex(m.address) + ": no outstanding B request");
                return;
            }
            table = 2;
            // §6.4: on B the pair (source, address) is unique and c_source "may be any b_source associated
            // with the sender": with several masters behind an Xbar the answer belongs to the Probe of the
            // same address sent to the same master (client). One client: the oldest at the address.
            const uint64_t ccl = client(m.source);
            for (int pass = 0; pass < 2 && !req; pass++)
                for (auto &o : it->second)
                {
                    if (pass == 0 && client(o.source) != ccl) continue;
                    for (auto &r : responseOf(Channel::B, o.opcode)) if (r.ch == Channel::C && r.opcode == op) { req = &o; break; }
                    if (req) break;
                }
            if (!req)
            {   // M-RESP-4, Tables 12, 13, 30: the C response opcode is allowed for the matched B request
                req = &it->second.front();
                report("M-RESP-4", "Tables 12, 13, 30", ch, &m, 0, cycle, opn + " is not a legal response to " + opcodeName(Channel::B, req->opcode) +
                       " (addr " + hex(m.address) + ")");
            }
        }
        else if (ch == Channel::E)
        {
            // M-RESP-5, §9.3.9 Table 40 "e_sink ... copied from d_sink"; §6.4
            req = findGrant(m.sink);
            table = 3;
            if (!req)
            {
                report("M-RESP-5", "§9.3.9 Table 40, §6.4", ch, &m, 0, cycle, "GrantAck sink " + std::to_string(m.sink) + " with no unanswered Grant");
                return;
            }
        }
        if (!req) return;

        if (ch == Channel::D)
        {
            // M-RESP-1, Tables 12, 13, 30 "the only allowed responses" (GrantData never answers AcquirePerm)
            bool ok = false;
            for (auto &r : responseOf(req->ch, req->opcode)) if (r.ch == Channel::D && r.opcode == op) ok = true;
            if (!ok)
                report("M-RESP-1", "Tables 12, 13, 30", ch, &m, 0, cycle, opn + " is not a legal response to " + opcodeName(req->ch, req->opcode) +
                       " (src " + std::to_string(m.source) + ")");
        }
        if (ch == Channel::D || ch == Channel::C)
        {
            // M-RESP-6, §7.2.4, §7.2.5, §8.2.4 "d_size and a_size must always correspond"; §9.3.5-§9.3.8, §9.3.12
            if (m.size != req->size)
                report("M-RESP-6", "§7.2.4, §7.2.5, §8.2.4, §9.3", ch, &m, 0, cycle, opn + " size " + std::to_string(m.size) + " != request size " +
                       std::to_string(req->size));
        }
        if (ch == Channel::D && (op == 4 || op == 5) && !m.denied && (req->opcode == 6 || req->opcode == 7))
        {
            // M-PARAM-11, §9.3.7, §9.3.8 "may exceed those requested"; plan §8 decision 7: a successful Grant meets or exceeds the Acquire
            static const char *grow[3] = {"NtoB", "NtoT", "BtoT"};
            static const char *cap[3] = {"toT", "toB", "toN"};
            bool ok = req->param == unsigned(Grow::NtoB) ? (m.param <= 1) : (m.param == unsigned(Cap::toT));
            if (!ok)
                report("M-PARAM-11", "§9.3.7, §9.3.8, plan §8.7", ch, &m, 0, cycle, std::string("Grant cap ") + (m.param <= 2 ? cap[m.param] : "?") +
                       " below Acquire " + (req->param <= 2 ? grow[req->param] : "?") + " (src " + std::to_string(m.source) + ")");
        }
        if (ch == Channel::C && (op == 4 || op == 5))
        {
            static const char *pr[6] = {"TtoB", "TtoN", "BtoN", "TtoT", "BtoB", "NtoN"};
            static const char *capn[3] = {"toT", "toB", "toN"};
            // the answering master: the Probe's target (b_source routes it, Table 14); c_source "may be any of the sender's" (§6.4)
            const uint64_t pcl = client(req->source);
            for (uint64_t blk : blockSet(m.address, m.size))
            {
                Pend &p = pend(blk);
                // M-CONC-5, §9.2 Release "the master should not issue a ProbeAck, ProbeAckData ... on the block until it receives a ReleaseAck"
                auto rel = ofClient(p.rel, pcl);
                if (rel != p.rel.end())
                    report("M-CONC-5", "§9.2", ch, &m, 0, cycle, "ProbeAck on block " + hex(blk) + " while Release src " +
                           std::to_string(rel->first) + " pending (ReleaseAck outstanding)");
            }
            // M-CONC-11, §9.2 "a master should only issue a ProbeAckData.TtoT if C channel messages are kept in order ... for a given cacheline address"
            if (op == 5 && m.param == unsigned(Report::TtoT) && !m_o.c_ordered)
                report("M-CONC-11", "§9.2", ch, &m, 0, cycle, "ProbeAckData TtoT on a link whose C channel is not ordered per address");
            for (uint64_t blk : blockSet(m.address, m.size))
            {
                // M-PERM-2, §9.1.4, §9.3.5, §9.3.6: the ProbeAck param starts from the tracked permission
                char P = permOf(blk, pcl);
                char from = m.param <= 1 || m.param == 3 ? 'T' : m.param == 2 || m.param == 4 ? 'B' : 'N';
                if (m.param <= 5 && P != from)
                    report("M-PERM-2", "§9.1.4, §9.3.5, §9.3.6", ch, &m, 0, cycle, opn + " " + pr[m.param] + " from tracked permission " + P +
                           " on block " + hex(blk));
                if (m.param <= 5) { permOf(blk, pcl) = pruneTarget(m.param); m_prune.erase({blk, client(m.source)}); }   // committed at beat 0
            }
            // M-PERM-3, §9.3.3, §9.3.4 Cap "what they should become"; §4.4: the answer satisfies the Probe's cap
            if (m.param <= 5 && req->param <= 2)
            {
                char to = m.param == 0 || m.param == 4 ? 'B' : m.param == 3 ? 'T' : 'N';
                bool ok = req->param == unsigned(Cap::toT) ? true : req->param == unsigned(Cap::toB) ? (to != 'T') : (to == 'N');
                if (!ok)
                    report("M-PERM-3", "§9.3.3, §9.3.4, §4.4", ch, &m, 0, cycle, opn + " " + pr[m.param] + " does not satisfy Probe cap " +
                           capn[req->param] + " on block " + hex(blockSet(m.address, m.size).front()));
            }
        }
        if (ch == Channel::D && op == unsigned(OpD::AccessAckData))
        {
            // M-CONC-10, §9.2 AccessAck "a slave should not issue an AccessAckData if there is a pending ProbeAckData on the block"
            for (uint64_t blk : blockSet(req->address, req->size))
                for (auto &pb : pend(blk).probe)
                    if (pb.second == unsigned(OpB::ProbeBlock))
                    { report("M-CONC-10", "§9.2", ch, &m, 0, cycle, "AccessAckData on block " + hex(blk) + " while ProbeBlock src " + std::to_string(pb.first) + " pending"); break; }
        }
        // Remember which entry this response completes (its last beat erases it).
        cv.resp_active = true;
        cv.resp_entry = *req;
        cv.resp_table = table;
    }

    void Monitor::acceptLast(Channel ch, const Msg &m, uint64_t cycle)
    {
        ChanView &cv = m_cv[static_cast<int>(ch)];
        const unsigned op = m.opcode;
        if (cv.resp_active)
        {
            const Out &req = cv.resp_entry;
            // Plan §4: operation latency, request first beat offered to response last beat accepted.
            m_k.stats().recordOperation(m_l.name(), req.ch, req.opcode, cycle - req.t_offer);
            // Catalogue §0.3: outstanding through the cycle of the response's last beat, inclusive.
            eraseEntry(cv.resp_table, req);
            if (m_p.level == Level::C)
            {
                for (uint64_t blk : blockSet(req.address, req.size))
                {
                    Pend &p = pend(blk);
                    if (req.ch == Channel::A) { p.acq.erase(req.source); p.acc.erase(req.source); }
                    else if (req.ch == Channel::C)
                    {
                        p.rel.erase(req.source);
                        m_since[{blk, client(req.source)}] = permOf(blk, client(req.source));   // ReleaseAck: the window restarts
                    }
                    else if (req.ch == Channel::B) p.probe.erase(req.source);
                    else if (req.ch == Channel::D) p.gack.erase(req.sink);
                    if (p.empty()) m_pend.erase(blk);
                }
                // M-PERM transitions (catalogue §12), applied at the last beat of the carrier:
                if (ch == Channel::D && (op == 4 || op == 5))
                {
                    // Grant/GrantData denied LOW: toB -> B, toT -> T. Denied HIGH: unchanged
                    // (M-ERR-3, §4.4 "no effect on the permissions"; §9.3.7, §9.3.8 "d_param should be ignored").
                    if (!m.denied) for (uint64_t blk : blockSet(req.address, req.size))
                    {
                        char g = m.param == unsigned(Cap::toT) ? 'T' : 'B';
                        permOf(blk, client(req.source)) = g;
                        m_since[{blk, client(req.source)}] = g;   // the window of plan-M-PERM-1 restarts here
                    }
                }
                // ProbeAck/ProbeAckData XtoY -> Y is committed at the first beat's accept (acceptResponseFirst)
            }
            cv.resp_active = false;
        }
        // Release/ReleaseData XtoY -> Y (catalogue §12) is committed at the first beat's accept (acceptRequestFirst)
    }

    // ------------------------------------------------------------------ end of run
    void Monitor::finish(uint64_t end_cycle)
    {
        if (m_finished) return;
        m_finished = true;
        // M-BURST-4, §4.1 "the master has no choice but to repeat F-1 until it is accepted"; §5 rule 2: no lock held at end of run
        for (int c = 0; c < NUM_CHANNELS; c++)
        {
            ChanView &cv = m_cv[c];
            if (cv.lock)
                report("M-BURST-4", "§4.1, §5", static_cast<Channel>(c), cv.lock.get(), cv.next, end_cycle, "burst " +
                       std::string(opcodeName(cv.lock->channel, cv.lock->opcode)) + " src " + std::to_string(cv.lock->source) +
                       " in progress at end of run: " + std::to_string(cv.next) + " of " + std::to_string(cv.total) + " beats accepted");
        }
        // M-BURST-5, §4.1 Figure 7 "Message N must eventually be offered again": a response offered, rejected and retracted
        // (a stale rejected offer at end of run counts as retracted: it was not re-offered on the following cycle).
        for (int c = 0; c < NUM_CHANNELS; c++)
        {
            ChanView &cv = m_cv[c];
            if (cv.off_valid && !cv.off_accepted && cv.off_beat == 0 && cv.off_msg && isResponseOpcode(static_cast<Channel>(c), cv.off_msg->opcode) &&
                cv.off_cycle + 1 < end_cycle)
            {
                Out o; o.ch = static_cast<Channel>(c); o.opcode = cv.off_msg->opcode; o.source = cv.off_msg->source; o.sink = cv.off_msg->sink;
                o.address = cv.off_msg->address; o.size = cv.off_msg->size; o.msg_id = key(*cv.off_msg);
                m_retracted.emplace(key(*cv.off_msg), std::make_pair(o, cv.off_cycle));
            }
        }
        for (auto &kv : m_retracted)
        {
            const Out &o = kv.second.first;
            Msg tmp; tmp.channel = o.ch; tmp.opcode = o.opcode; tmp.size = o.size; tmp.source = o.source; tmp.sink = o.sink; tmp.address = o.address; tmp.id = o.msg_id;
            report("M-BURST-5", "§4.1 Figure 7", o.ch, &tmp, 0, end_cycle, "response " + std::string(opcodeName(o.ch, o.opcode)) + " src " +
                   std::to_string(o.source) + " retracted at cycle " + std::to_string(kv.second.second) + " and never accepted");
        }
        // M-RESP-7, §5 rule 3ii, §6.2 "receipt of request messages must result in the eventual receipt of a response message":
        // every accepted request answered, excluding those whose response was offered (M-BURST-5's) and
        // those whose own request burst is still in progress (M-BURST-4's: the sender, not the receiver,
        // is the one at fault, and a Put cannot be acknowledged before its effect, §6.5).
        auto unanswered = [&](const std::map<uint64_t, std::deque<Out>> &tab)
        {
            for (auto &kv : tab)
                for (auto &o : kv.second)
                {
                    if (o.resp_offered) continue;
                    const ChanView &rv = m_cv[static_cast<int>(o.ch)];
                    if (rv.lock && key(*rv.lock) == o.msg_id) continue;
                    Msg tmp; tmp.channel = o.ch; tmp.opcode = o.opcode; tmp.param = o.param; tmp.size = o.size; tmp.source = o.source;
                    tmp.sink = o.sink; tmp.address = o.address; tmp.id = o.msg_id;
                    report("M-RESP-7", "§5 rule 3ii, §6.2", o.ch, &tmp, 0, end_cycle, "request " + std::string(opcodeName(o.ch, o.opcode)) + " src " +
                           std::to_string(o.source) + " addr " + hex(o.address) + " accepted at cycle " + std::to_string(o.t_accept) + " never answered");
                }
        };
        unanswered(m_outA); unanswered(m_outB); unanswered(m_outC); unanswered(m_grants);
    }
}}
