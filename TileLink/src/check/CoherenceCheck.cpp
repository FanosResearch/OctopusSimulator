/*
 * File  :      CoherenceCheck.cpp
 * The TL-C system checker (plan §3.6 CoherenceCheck, §6 TL-FUZZ-C, TL-UNCACHED). Two invariants:
 *
 * C-SWMR (§9.1 Table 29: only a Tip writes; at most one Tip, and a Tip with no Branches when it
 * writes) from the permissions observed on the masters' own links. Per (block, link) the checker
 * keeps N, B or T, N at reset, changed only by accepted beats (§5.1 "received" = first beat accepted):
 *   - Grant / GrantData, denied LOW, first beat accepted by the master: the permission becomes the cap
 *     (toB -> B, toT -> T; §9.3.7, §9.3.8). The block is the block of the Acquire it answers, matched by
 *     source on the link (Table 14: D routes by source; D has no address). Then at most one link holds
 *     T and, if one does, no other holds B: C-SWMR.
 *   - Grant / GrantData, denied HIGH: nothing changes (§4.4 "no effect on the permissions", §9.3.7
 *     "d_param should be ignored"; the M-ERR-3 exemption of the monitor).
 *   - ProbeAck / ProbeAckData / Release / ReleaseData, first beat accepted by the manager: the permission
 *     is lowered to the param's target (TtoB, BtoB -> B; TtoN, BtoN, NtoN -> N; TtoT -> T; §9.1.4 Table
 *     31). Lowered, never raised: a Report that names a permission above the tracked one is the
 *     monitor's M-PERM-2, and taking it at its word here would hide a real conflict.
 * C-GRANT, the manager's view (plan §3.6 "on the manager's view when it issues a Grant (all ProbeAcks
 * in)"; §9.2 "a slave should not issue a Grant or GrantData if there is a pending ProbeAck"): when a
 * Grant with denied LOW is offered (every offer, reported once per message), no other link may hold a
 * permission the cap excludes, as last reported: a toT Grant needs every other link at N, a toB Grant
 * needs no other link at T. The second half is the same argument for a Branch (a Tip with a Branch
 * beside it is the SWMR violation C-SWMR would see one cycle later). The check runs on the master's
 * link, so behind an Xbar it sees the Grant when the Xbar forwards it, after the manager issued it: a
 * conservative view of the manager's decision (it cannot report a legal Grant).
 * Masters are identified by link. A link carrying several masters (an Xbar's output) cannot say which
 * of them a Grant or ProbeAck concerns, so the checker attaches only to links whose master is a leaf
 * (ConfigError otherwise); plan §3.6 places it on the masters' own links.
 *
 * C-VALUE (§6.5 "a slave responds only once the effect is visible everywhere"; plan §8 decision 8):
 * the agents report every performed load and store (CacheMaster at the hit, the manager at an uncached
 * Put/Atomic/Get); per byte a load must return the value of the latest store reported before it. Under
 * SWMR the global order of accesses to a byte is their cycle order, and within a cycle the order in
 * which the agents report (the kernel's commit order), so the checker uses report order: agents report
 * in non-decreasing cycle order by construction of the cycle (plan §3.2). A byte never stored admits any
 * value unless setInitial gave it one. One report per offending load (the first mismatching byte, and
 * how many bytes differ).
 */
#include "tilelink/CoherenceCheck.h"
#include <cstdio>
#include <sstream>

namespace octopus { namespace tl
{
    static std::string hex(uint64_t v)
    {
        char buf[32];
        std::snprintf(buf, sizeof buf, "0x%llx", (unsigned long long)v);
        return buf;
    }

    // N < B < T (§9.1 Figure 25: None, Branch, Trunk/Tip; a master's own copy is never a Trunk).
    static int rank(char p) { return p == 'T' ? 2 : p == 'B' ? 1 : 0; }
    // §9.1.4 Table 31: the target of a Prune or Report param (TtoB 0, TtoN 1, BtoN 2, TtoT 3, BtoB 4,
    // NtoN 5); 0 for a param outside the table (M-PARAM-8 is the monitor's).
    static char pruneTarget(unsigned param)
    {
        switch (param)
        {
            case 0: case 4: return 'B';
            case 3:         return 'T';
            case 1: case 2: case 5: return 'N';
            default:        return 0;
        }
    }
    static uint64_t msgKey(const Msg &m) { return m.id ? m.id : uint64_t(reinterpret_cast<uintptr_t>(&m)); }

    std::string CoherenceViolation::line() const
    {
        return rule + ": cycle=" + std::to_string(cycle) + " block=" + hex(block) + " addr=" + hex(address) + ": " + detail;
    }

    CoherenceCheck::CoherenceCheck(unsigned block_bytes) : m_block(block_bytes)
    {
        if (block_bytes == 0 || (block_bytes & (block_bytes - 1)) != 0)
            throw ConfigError("CoherenceCheck: block_bytes must be a power of two, got " + std::to_string(block_bytes));
    }

    CoherenceCheck *CoherenceCheck::of(Kernel &k)
    {
        return dynamic_cast<CoherenceCheck *>(k.observer("CoherenceCheck"));
    }

    void CoherenceCheck::violate(const CoherenceViolation &v)
    {
        m_c.violations++;
        if (onViolation) onViolation(v);
        else fatal("coherence " + v.line());   // "TileLink: coherence C-SWMR: ...", exit 2
    }

    std::string CoherenceCheck::agentName(int id) const
    {
        if (m_kernel && id >= 0 && size_t(id) < m_kernel->agents().size()) return m_kernel->agents()[size_t(id)]->name();
        return "agent " + std::to_string(id);
    }

    // ------------------------------------------------------------------ attaching
    void CoherenceCheck::checkLeaf(const Link &l) const
    {
        // Plan §3.6: the checker sits on the masters' own links. A master that is also a slave
        // somewhere (an Xbar, a Buffer, a cache hierarchy level) forwards for several masters: the
        // link cannot attribute a permission to one of them (they are told apart only by source
        // ranges, which the adapters remap, §6.4).
        if (l.level() != Level::C)
            throw ConfigError("CoherenceCheck: link " + l.name() + " is TL-" + levelName(l.level()) + ", not TL-C (no permissions to track)");
        const Agent *m = l.master();
        if (!m) throw ConfigError("CoherenceCheck: link " + l.name() + " has no master");
        for (auto &lr : m->links())
            if (lr.role == Role::Slave)
                throw ConfigError("CoherenceCheck: link " + l.name() + ": its master " + m->name() + " is a slave on link " + lr.link->name() +
                                  ", so the link may carry several masters; attach the checker on the masters' own links, upstream of any Xbar");
    }

    void CoherenceCheck::attach(Link &l)
    {
        for (auto *a : m_links) if (a == &l) return;
        checkLeaf(l);
        m_links.push_back(&l);
        if (!m_kernel) m_kernel = l.master()->kernel();   // agent names in the messages
        // Chained after the hooks already there (Monitor, ValueCheck): both keep observing.
        ChannelState &a = l.chan(Channel::A), &c = l.chan(Channel::C), &d = l.chan(Channel::D);
        auto pa = a.onAccept, pc = c.onAccept, pd = d.onAccept, pdo = d.onOffer;
        a.onAccept = [this, pa](const ChannelState &cs, const Beat &b, uint64_t t) { if (pa) pa(cs, b, t); onA(cs, b, t); };
        c.onAccept = [this, pc](const ChannelState &cs, const Beat &b, uint64_t t) { if (pc) pc(cs, b, t); onC(cs, b, t); };
        d.onAccept = [this, pd](const ChannelState &cs, const Beat &b, uint64_t t) { if (pd) pd(cs, b, t); onDAccept(cs, b, t); };
        d.onOffer  = [this, pdo](const ChannelState &cs, const Beat &b, uint64_t t) { if (pdo) pdo(cs, b, t); onDOffer(cs, b, t); };
    }

    unsigned CoherenceCheck::attachAll(Kernel &k)
    {
        unsigned n = 0;
        for (auto &l : k.links())
        {
            if (l->level() != Level::C || !l->master()) continue;
            bool leaf = true;
            for (auto &lr : l->master()->links()) if (lr.role == Role::Slave) leaf = false;
            if (!leaf) continue;
            size_t before = m_links.size();
            attach(*l);
            if (m_links.size() != before) n++;
        }
        return n;
    }

    void CoherenceCheck::onElaborate(Kernel &k)
    {
        m_kernel = &k;
        if (m_links.empty() && auto_attach) attachAll(k);
        for (auto *l : m_links) checkLeaf(*l);   // the system is complete now: the leaf rule again
    }

    // ------------------------------------------------------------------ permissions
    char &CoherenceCheck::permRef(uint64_t block, const std::string &link)
    {
        auto &row = m_perm[block];
        auto it = row.find(link);
        if (it == row.end()) it = row.emplace(link, 'N').first;   // N at reset
        return it->second;
    }

    void CoherenceCheck::accessPerformed(const Link &l, uint64_t addr, bool store, uint64_t cycle)
    {
        // C-PERM, Table 29: Get needs Branch, Tip or Trunk; Put and Atomics need a Tip with no branches.
        char p = permission(l.name(), addr);
        store ? m_c.stores++ : m_c.loads++;
        if (store ? p != 'T' : p == 'N')
        {
            CoherenceViolation v;
            v.rule = "C-PERM"; v.cycle = cycle; v.block = blockOf(addr); v.address = addr;
            v.detail = std::string(store ? "store" : "load") + " served from the copy on link " + l.name() + " with permission " + p +
                       (store ? " (needs T)" : " (needs B or T)");
            violate(v);
        }
    }

    char CoherenceCheck::permission(const std::string &link, uint64_t block) const
    {
        auto it = m_perm.find(blockOf(block));
        if (it == m_perm.end()) return 'N';
        auto jt = it->second.find(link);
        return jt == it->second.end() ? 'N' : jt->second;
    }

    void CoherenceCheck::onA(const ChannelState &c, const Beat &b, uint64_t)
    {
        // The Acquire a later Grant answers (Table 14: the Grant copies a_source; it has no address).
        const Msg &m = *b.msg;
        if (b.index != 0) return;
        if (m.opcode == unsigned(OpA::AcquireBlock) || m.opcode == unsigned(OpA::AcquirePerm))
            m_acq[Key(c.link(), m.source)] = blockOf(m.address);
    }

    void CoherenceCheck::onC(const ChannelState &c, const Beat &b, uint64_t)
    {
        // §9.3.5, §9.3.6, §9.3.10, §9.3.11: ProbeAck[Data] and Release[Data] lower the sender's
        // permission to the param's target, from the cycle the manager receives the first beat.
        const Msg &m = *b.msg;
        if (b.index != 0) return;
        const bool pack = m.opcode == unsigned(OpC::ProbeAck) || m.opcode == unsigned(OpC::ProbeAckData);
        const bool rel = m.opcode == unsigned(OpC::Release) || m.opcode == unsigned(OpC::ReleaseData);
        if (!pack && !rel) return;
        if (pack) m_c.probe_acks++; else m_c.releases++;
        char to = pruneTarget(m.param);
        if (!to) return;
        const std::string &ln = c.link()->name();
        uint64_t last = m.address + m.bytes() - 1;
        for (uint64_t blk = blockOf(m.address); blk <= blockOf(last); blk += m_block)
        {
            char &p = permRef(blk, ln);
            if (rank(to) < rank(p)) p = to;
            if (blk + m_block < blk) break;   // address-space wrap
        }
    }

    void CoherenceCheck::onDOffer(const ChannelState &c, const Beat &b, uint64_t t)
    {
        // C-GRANT, §9.2 "a slave should not issue a Grant or GrantData if there is a pending
        // ProbeAck"; plan §3.6 "the manager's view when it issues a Grant (all ProbeAcks in)".
        const Msg &m = *b.msg;
        if (b.index != 0 || m.denied) return;   // a denied Grant grants nothing (§9.3.7)
        if (m.opcode != unsigned(OpD::Grant) && m.opcode != unsigned(OpD::GrantData)) return;
        if (m.param != unsigned(Cap::toT) && m.param != unsigned(Cap::toB)) return;   // M-PARAM-9's
        auto it = m_acq.find(Key(c.link(), m.source));
        if (it == m_acq.end()) return;          // no Acquire seen on this link (M-RESP-2's)
        if (m_grant_reported.count(msgKey(m))) return;
        const uint64_t blk = it->second;
        const char cap = m.param == unsigned(Cap::toT) ? 'T' : 'B';
        const std::string &self = c.link()->name();
        auto row = m_perm.find(blk);
        if (row == m_perm.end()) return;
        for (auto &kv : row->second)
        {
            if (kv.first == self) continue;
            bool bad = cap == 'T' ? kv.second != 'N' : kv.second == 'T';
            if (!bad) continue;
            m_grant_reported[msgKey(m)] = true;
            CoherenceViolation v;
            v.rule = "C-GRANT"; v.cycle = t; v.block = blk; v.address = blk;
            v.detail = std::string(opcodeName(Channel::D, m.opcode)) + " to" + cap + " offered to link " + self + " (src " + std::to_string(m.source) +
                       ") while link " + kv.first + " still holds " + kv.second + " as last reported (a ProbeAck not yet received, §9.2)";
            violate(v);
            return;
        }
    }

    void CoherenceCheck::onDAccept(const ChannelState &c, const Beat &b, uint64_t t)
    {
        // §9.3.7, §9.3.8: a Grant[Data] with denied LOW gives the master its cap; denied HIGH changes
        // nothing (§4.4). Then C-SWMR over every link that holds the block.
        const Msg &m = *b.msg;
        if (b.index != 0) return;
        if (m.opcode != unsigned(OpD::Grant) && m.opcode != unsigned(OpD::GrantData)) return;
        m_c.grants++;
        m_grant_reported.erase(msgKey(m));
        auto it = m_acq.find(Key(c.link(), m.source));
        if (it == m_acq.end()) return;
        const uint64_t blk = it->second;
        m_acq.erase(it);
        if (m.denied) return;
        if (m.param != unsigned(Cap::toT) && m.param != unsigned(Cap::toB)) return;
        const char cap = m.param == unsigned(Cap::toT) ? 'T' : 'B';
        const std::string &self = c.link()->name();
        permRef(blk, self) = cap;
        for (auto &kv : m_perm[blk])
        {
            if (kv.first == self) continue;
            bool bad = cap == 'T' ? kv.second != 'N' : kv.second == 'T';
            if (!bad) continue;
            CoherenceViolation v;
            v.rule = "C-SWMR"; v.cycle = t; v.block = blk; v.address = blk;
            v.detail = "link " + self + " granted " + cap + " (" + opcodeName(Channel::D, m.opcode) + " src " + std::to_string(m.source) +
                       ") while link " + kv.first + " holds " + kv.second + ": at most one Tip, and no Branch beside a Tip (§9.1 Table 29)";
            violate(v);
            return;
        }
    }

    // ------------------------------------------------------------------ the data-value invariant
    void CoherenceCheck::setInitial(uint64_t addr, const uint8_t *bytes, unsigned n)
    {
        for (unsigned i = 0; i < n; i++)
        {
            m_mem[addr + i] = bytes[i];
            m_known[addr + i] = true;
            Writer w; w.initial = true;
            m_writer[addr + i] = w;
        }
    }

    void CoherenceCheck::storePerformed(unsigned agent, uint64_t addr, const uint8_t *bytes, unsigned n, uint64_t cycle)
    {
        m_c.stores++;
        for (unsigned i = 0; i < n; i++)
        {
            m_mem[addr + i] = bytes[i];
            m_known[addr + i] = true;
            Writer w; w.agent = int(agent); w.cycle = cycle;
            m_writer[addr + i] = w;
        }
    }

    void CoherenceCheck::loadPerformed(unsigned agent, uint64_t addr, const uint8_t *bytes, unsigned n, uint64_t cycle)
    {
        // C-VALUE, §6.5: a load returns the most recent store to each byte (plan §3.6, §8 decision 8).
        m_c.loads++;
        unsigned bad = 0;
        uint64_t first = 0;
        for (unsigned i = 0; i < n; i++)
        {
            auto it = m_known.find(addr + i);
            if (it == m_known.end() || !it->second) continue;   // never stored: any value
            m_c.bytes_checked++;
            if (m_mem[addr + i] != bytes[i]) { if (!bad) first = addr + i; bad++; }
        }
        if (!bad) return;
        const Writer &w = m_writer[first];
        CoherenceViolation v;
        v.rule = "C-VALUE"; v.cycle = cycle; v.block = blockOf(first); v.address = first;
        std::ostringstream o;
        o << agentName(int(agent)) << " load of " << n << " byte(s) at " << hex(addr) << " returned " << hex(bytes[first - addr]) << " at byte " << hex(first)
          << ", the latest store wrote " << hex(m_mem[first]) << " ("
          << (w.initial ? std::string("initial image") : agentName(w.agent) + " at cycle " + std::to_string(w.cycle)) << "); " << bad << " byte(s) differ (§6.5)";
        v.detail = o.str();
        violate(v);
    }
}}
