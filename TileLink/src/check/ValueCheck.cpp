/*
 * File  :      ValueCheck.cpp
 * ValueCheck (plan §3.6): the admissible-set rule.
 */
#include "tilelink/ValueCheck.h"
#include "tilelink/agents/Pipe.h"
#include "tilelink/Atomic.h"
#include <map>
#include <algorithm>
#include <sstream>

namespace octopus { namespace tl
{
    std::string ValueViolation::describe() const
    {
        std::ostringstream o;
        o << rule << " cycle " << cycle << " link " << link << " address 0x" << std::hex << address << " got 0x" << got << " admissible {";
        for (size_t i = 0; i < admissible.size(); i++) o << (i ? ", " : "") << "0x" << admissible[i];
        o << "}" << std::dec << " (" << request << ")";
        return o.str();
    }

    void ValueCheck::attach(Link &l)
    {
        // Chained after the hook already there (the Monitor's), so both observe.
        ChannelState &a = l.chan(Channel::A), &d = l.chan(Channel::D);
        auto prev_a = a.onAccept, prev_d = d.onAccept;
        a.onAccept = [this, prev_a](const ChannelState &c, const Beat &b, uint64_t t) { if (prev_a) prev_a(c, b, t); onA(c, b, t); };
        d.onAccept = [this, prev_d](const ChannelState &c, const Beat &b, uint64_t t) { if (prev_d) prev_d(c, b, t); onD(c, b, t); };
    }

    ValueCheck::Admissible ValueCheck::current(uint64_t x) const
    {
        Admissible s;
        auto it = m_bytes.find(x);
        if (it == m_bytes.end()) { s.any = true; return s; }
        s.any = it->second.initial;
        for (auto &w : it->second.writes) { if (w.unknown) s.any = true; else s.values.insert(w.value); }
        return s;
    }

    bool ValueCheck::admissible(uint64_t address, std::vector<uint64_t> &values) const
    {
        Admissible s = current(address);
        values.assign(s.values.begin(), s.values.end());
        return !s.any;
    }

    // The value a response beat carries for byte x of the request (D has no address: the
    // request's geometry places the lanes, §4.5).
    static bool respValue(const Msg &resp, const Msg &req, uint64_t x, uint64_t &v)
    {
        LanePos p = lanePos(req, x);
        if (resp.data_mode == DataMode::Full)
        {
            size_t i = size_t(p.beat) * resp.w + p.lane;
            if (i >= resp.data.size()) return false;
            v = resp.data[i];
            return true;
        }
        if (resp.data_mode == DataMode::Stamp)
        {
            size_t i = size_t(p.beat) * resp.stampsPerBeat() + (resp.w >= 8 ? p.lane / 8 : 0);
            if (i >= resp.stamp.size()) return false;
            v = resp.stamp[i];
            return true;
        }
        return false;
    }

    void ValueCheck::addWrite(Op &op, uint64_t x, uint64_t value, bool unknown)
    {
        WriteRec r;
        r.id = op.wid; r.issue = op.issue; r.value = value; r.unknown = unknown;
        m_bytes[x].writes.push_back(r);
        admit(op, x, value, unknown);
    }

    void ValueCheck::admit(Op &op, uint64_t x, uint64_t value, bool unknown)
    {
        // Plan §3.6: a read (or atomic) outstanding now may be served after this write; its
        // admissible set grows by the write's value.
        for (auto &kv : m_ops)
        {
            Op &o = kv.second;
            if (&o == &op || o.snap.empty()) continue;
            auto it = o.snap.find(x);
            if (it == o.snap.end()) continue;
            if (unknown) it->second.any = true; else it->second.values.insert(value);
        }
    }

    void ValueCheck::ackWrite(Op &op, uint64_t t, bool denied)
    {
        for (uint64_t x : op.written)
        {
            auto bit = m_bytes.find(x);
            if (bit == m_bytes.end()) continue;
            ByteState &bs = bit->second;
            auto rit = std::find_if(bs.writes.begin(), bs.writes.end(), [&](const WriteRec &r) { return r.id == op.wid; });
            if (rit == bs.writes.end()) continue;   // not a masked byte of this write
            if (denied) { bs.writes.erase(rit); continue; }   // plan §3.6: a denied write adds nothing
            rit->ack = t;
            uint64_t issue = rit->issue, id = rit->id;
            // §6.5: a write acknowledged before this one was issued is ordered before it and
            // is now overwritten; the writes whose intervals overlapped keep their values.
            // With no overlap the set collapses to this value.
            bs.writes.erase(std::remove_if(bs.writes.begin(), bs.writes.end(),
                                           [&](const WriteRec &r) { return r.id != id && r.ack != NEVER && r.ack < issue; }),
                            bs.writes.end());
            bs.initial = false;
        }
    }

    void ValueCheck::checkByte(const Op &op, uint64_t x, uint64_t got, uint64_t t, const char *rule)
    {
        m_c.bytes_checked++;
        auto it = op.snap.find(x);
        if (it == op.snap.end() || it->second.any) return;
        if (it->second.values.count(got)) return;
        m_c.violations++;
        ValueViolation v;
        v.rule = rule; v.cycle = t; v.link = op.link->name(); v.address = x; v.got = got;
        v.admissible.assign(it->second.values.begin(), it->second.values.end());
        v.request = op.req->describe();
        if (onViolation) onViolation(v);
        else fatal(m_name + ": " + v.describe());
    }

    void ValueCheck::onA(const ChannelState &c, const Beat &b, uint64_t t)
    {
        const Msg &m = *b.msg;
        if (m.data_mode == DataMode::None) return;
        unsigned op = m.opcode;
        bool is_get = op == unsigned(OpA::Get);
        bool is_put = op == unsigned(OpA::PutFullData) || op == unsigned(OpA::PutPartialData);
        bool is_atomic = op == unsigned(OpA::ArithmeticData) || op == unsigned(OpA::LogicalData);
        if (!is_get && !is_put && !is_atomic) return;   // Intent and TL-C: not value-carrying here (TL-C is T6)
        Key key{c.link(), m.source};
        Op *o;
        if (b.index == 0)
        {
            if (m_ops.count(key))
                fatal(m_name + ": source " + std::to_string(m.source) + " reused on " + c.link()->name() + " while outstanding (" + m.describe() + ") (§6.4)");
            o = &m_ops[key];
            o->link = c.link(); o->req = b.msg; o->issue = t; o->wid = ++m_next_wid;
            if (is_get) m_c.reads++; else if (is_put) m_c.writes++; else m_c.atomics++;
            if (is_get || is_atomic)
            {
                // The set as it stands at the read's issue (before the atomic's own entry).
                for (uint64_t x = m.address; x < m.address + m.bytes(); x++) o->snap[x] = current(x);
            }
        }
        else
        {
            auto it = m_ops.find(key);
            if (it == m_ops.end()) return;
            o = &it->second;
        }
        if (is_get) return;
        // A write or atomic beat: the masked bytes of this beat enter the model.
        uint64_t first, last;
        if (!beatRange(m, b.index, first, last)) return;
        bool corrupt = m.beatCorrupt(b.index);
        if (is_atomic && corrupt) o->operand_corrupt = true;
        const bool stamp = m.data_mode == DataMode::Stamp;
        for (uint64_t x = first; x <= last; x++)
        {
            LanePos p = lanePos(m, x);
            if (!m.mask[size_t(p.beat) * m.w + p.lane]) continue;       // PutPartialData: masked bytes only
            uint64_t v = is_atomic ? 0 : laneValue(m, x);
            bool unknown = is_atomic || corrupt;                         // an atomic's value is known at its ack
            // Full: the byte itself. Stamp: every byte of x's group takes the stamp (RamSlave replaces the
            // group); a later byte of the same group in address order overrides an earlier one, as there
            uint64_t g0 = stamp ? (x & ~uint64_t(7)) : x, g1 = stamp ? g0 + 8 : x + 1;
            for (uint64_t y = g0; y < g1; y++)
            {
                if (o->written.insert(y).second) { addWrite(*o, y, v, unknown); continue; }
                for (auto &r : m_bytes[y].writes)
                    if (r.id == o->wid) { r.value = v; r.unknown = unknown; }
                admit(*o, y, v, unknown);   // the reads outstanding meanwhile may see the overriding value too
            }
        }
    }

    void ValueCheck::finishAtomic(Op &op, uint64_t t)
    {
        // docs/TileLink_Review_Action_Plan.md problem 4: the same operation the agents execute.
        const Msg &m = *op.req;
        const bool logical = m.opcode == unsigned(OpA::LogicalData);
        const bool stamp = m.data_mode == DataMode::Stamp;
        const uint64_t a0 = m.address, n = m.bytes();
        auto set = [&](uint64_t x, uint64_t v, bool unknown) {
            auto bit = m_bytes.find(x);
            if (bit == m_bytes.end()) return;
            for (auto &r : bit->second.writes) if (r.id == op.wid) { r.unknown = unknown; r.value = unknown ? 0 : v; }
        };
        auto known = [&](uint64_t x) {
            LanePos p = lanePos(m, x);
            return m.mask[size_t(p.beat) * m.w + p.lane] && !op.old_unknown.count(x) && op.old.count(x);
        };
        if (stamp)
        {
            // Stamp: a stamp names a write, it is not a number. The atomic installs the operand's stamp for
            // every group it touches (the last byte of the group in address order, as RamSlave); the old
            // stamp it returned is checked in onD. No arithmetic on stamps.
            std::map<uint64_t, uint64_t> group_stamp;
            for (uint64_t x = a0; x < a0 + n; x++) if (known(x)) group_stamp[x & ~uint64_t(7)] = laneValue(m, x);
            for (uint64_t y : op.written)
            {
                auto g = group_stamp.find(y & ~uint64_t(7));
                set(y, g == group_stamp.end() ? 0 : g->second, g == group_stamp.end() || op.operand_corrupt);
            }
            ackWrite(op, t, false);
            return;
        }
        // Full: one operation on the whole 2^size-byte operand (Atomic.h). Logical operations are per byte, so
        // a byte whose old value is unknown leaves only its own result unknown; arithmetic depends on every
        // byte (a carry, a sign), so any unknown or corrupt input leaves the whole result unknown.
        std::vector<uint8_t> old(size_t(n), 0), opnd(size_t(n), 0);
        bool all = !op.operand_corrupt;
        for (uint64_t x = a0; x < a0 + n; x++)
        {
            size_t k = size_t(x - a0);
            if (known(x)) old[k] = uint8_t(op.old[x]); else all = false;
            opnd[k] = uint8_t(laneValue(m, x));
        }
        std::vector<uint8_t> res = applyAtomic(m.opcode, m.param, old, opnd);
        for (uint64_t x = a0; x < a0 + n; x++)
        {
            bool unknown = res.empty() || op.operand_corrupt || (logical ? !known(x) : !all);
            set(x, unknown ? 0 : res[size_t(x - a0)], unknown);
        }
        ackWrite(op, t, false);
    }

    void ValueCheck::onD(const ChannelState &c, const Beat &b, uint64_t t)
    {
        const Msg &r = *b.msg;
        if (r.data_mode == DataMode::None) return;
        if (r.opcode != unsigned(OpD::AccessAck) && r.opcode != unsigned(OpD::AccessAckData)) return;
        auto it = m_ops.find(Key{c.link(), r.source});
        if (it == m_ops.end()) return;   // a request this checker did not see (attached later, or an Intent)
        Op &op = it->second;
        const Msg &m = *op.req;
        bool is_get = m.opcode == unsigned(OpA::Get);
        bool is_atomic = m.opcode == unsigned(OpA::ArithmeticData) || m.opcode == unsigned(OpA::LogicalData);
        if (b.index == 0 && r.denied) m_c.denied++;
        if (!is_get && !is_atomic)
        {
            // A Put's AccessAck: the write is acknowledged (one beat).
            m_c.writes_acked++;
            ackWrite(op, t, r.denied);
            m_ops.erase(it);
            return;
        }
        // AccessAckData: check the returned bytes of this beat against the read's set.
        bool skip = r.denied || r.beatCorrupt(b.index);
        uint64_t first, last;
        // The response's beats in the request's geometry (D has no address field, §4.5).
        if (beatRange(m.address, m.bytes(), r.w, b.index, first, last))
        {
            for (uint64_t x = first; x <= last; x++)
            {
                LanePos p = lanePos(m, x);
                if (is_atomic && !m.mask[size_t(p.beat) * m.w + p.lane]) continue;
                uint64_t got = 0;
                if (skip || !respValue(r, m, x, got))
                {
                    m_c.bytes_skipped++;
                    if (is_atomic) op.old_unknown.insert(x);
                    continue;
                }
                checkByte(op, x, got, t, is_get ? "V-READ" : "V-ATOMIC");
                if (is_atomic) op.old[x] = got;
            }
        }
        if (!b.last()) return;
        if (is_atomic)
        {
            if (r.denied) ackWrite(op, t, true);   // adds nothing
            else finishAtomic(op, t);
            m_c.writes_acked++;
        }
        m_ops.erase(it);
    }
}}
