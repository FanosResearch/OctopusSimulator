/*
 * File  :      ValueCheck.cpp
 * ValueCheck (plan §3.6): the admissible-set rule.
 */
#include "tilelink/ValueCheck.h"
#include "tilelink/agents/Pipe.h"
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
        const Msg &m = *op.req;
        for (uint64_t x = m.address; x < m.address + m.bytes(); x++)
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
        for (uint64_t x = first; x <= last; x++)
        {
            LanePos p = lanePos(m, x);
            if (!m.mask[size_t(p.beat) * m.w + p.lane]) continue;       // PutPartialData: masked bytes only
            if (is_atomic) addWrite(*o, x, 0, true);                      // value known at its ack
            else addWrite(*o, x, laneValue(m, x), corrupt);
        }
    }

    // Plan §1.3: the atomic result from the old value and the operand, on n-byte little-endian
    // integers (n <= 8), signed for MIN/MAX/ADD, unsigned for MINU/MAXU.
    static uint64_t arith(unsigned param, uint64_t old, uint64_t operand, unsigned n)
    {
        unsigned bits = n * 8;
        uint64_t mask = bits >= 64 ? ~uint64_t(0) : (uint64_t(1) << bits) - 1;
        auto sext = [&](uint64_t v) -> int64_t
        {
            if (bits >= 64) return int64_t(v);
            uint64_t sign = uint64_t(1) << (bits - 1);
            return int64_t((v & mask) ^ sign) - int64_t(sign);
        };
        switch (static_cast<Arith>(param))
        {
            case Arith::MIN:  return (sext(old) < sext(operand) ? old : operand) & mask;
            case Arith::MAX:  return (sext(old) > sext(operand) ? old : operand) & mask;
            case Arith::MINU: return ((old & mask) < (operand & mask) ? old : operand) & mask;
            case Arith::MAXU: return ((old & mask) > (operand & mask) ? old : operand) & mask;
            case Arith::ADD:  return (old + operand) & mask;
        }
        return old & mask;
    }
    static uint64_t logic(unsigned param, uint64_t old, uint64_t operand)
    {
        switch (static_cast<Logic>(param))
        {
            case Logic::XOR:  return old ^ operand;
            case Logic::OR:   return old | operand;
            case Logic::AND:  return old & operand;
            case Logic::SWAP: return operand;
        }
        return old;
    }

    void ValueCheck::finishAtomic(Op &op, uint64_t t)
    {
        const Msg &m = *op.req;
        bool logical = m.opcode == unsigned(OpA::LogicalData);
        bool stamp = m.data_mode == DataMode::Stamp;
        // Group size: the operation's 2^size bytes up to 8 (an arithmetic atomic wider than 8
        // bytes is applied per 8-byte lane); logical ops are per byte. In Stamp mode a byte's
        // value is its group's 64-bit stamp, so every group is one 64-bit integer.
        uint64_t n = m.bytes();
        uint64_t group = (logical && !stamp) ? 1 : (stamp ? 8 : (n < 8 ? n : 8));
        for (uint64_t g = m.address; g < m.address + n; g += group)
        {
            uint64_t old = 0, operand = 0;
            bool unknown = false;
            for (uint64_t x = g; x < g + group && x < m.address + n; x++)
            {
                LanePos p = lanePos(m, x);
                if (!m.mask[size_t(p.beat) * m.w + p.lane] || op.old_unknown.count(x) || !op.old.count(x)) { unknown = true; break; }
                if (stamp) { old = op.old[x]; operand = laneValue(m, x); }
                else
                {
                    unsigned shift = unsigned((x - g) * 8);
                    old |= op.old[x] << shift;
                    operand |= laneValue(m, x) << shift;
                }
            }
            uint64_t nv = unknown ? 0 : (logical ? logic(m.param, old, operand) : arith(m.param, old, operand, unsigned(stamp ? 8 : group)));
            for (uint64_t x = g; x < g + group && x < m.address + n; x++)
            {
                auto bit = m_bytes.find(x);
                if (bit == m_bytes.end()) continue;
                for (auto &r : bit->second.writes)
                    if (r.id == op.wid)
                    {
                        r.unknown = unknown;
                        r.value = unknown ? 0 : (stamp ? nv : (nv >> ((x - g) * 8)) & 0xff);
                    }
            }
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
