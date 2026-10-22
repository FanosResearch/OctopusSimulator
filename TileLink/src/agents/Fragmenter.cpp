/*
 * File  :      Fragmenter.cpp
 * Fragmenter (plan §3.5): split Get/Put* above max_size, reassemble the responses.
 */
#include "tilelink/agents/Fragmenter.h"

namespace octopus { namespace tl
{
    Fragmenter::Fragmenter(const std::string &name, const Config &cfg) : TopologyAgent(name)
    {
        if (!cfg.has("max_size")) throw ConfigError("Fragmenter " + name + ": max_size(i) is required");
        int ms = cfg.get<int>("max_size");
        if (ms < 0 || ms > 15) throw ConfigError("Fragmenter " + name + ": max_size must be in 0..15 (log2 bytes)");
        m_max_size = unsigned(ms);
        m_max_unsplit = cfg.get<int>("max_unsplit", -1);
        m_deny_all = cfg.get<int>("deny_corrupts_all", 1) != 0;
        int d = cfg.get<int>("depth", 2);
        if (d <= 0) throw ConfigError("Fragmenter " + name + ": depth must be >= 1");
        m_depth = unsigned(d);
    }

    unsigned Fragmenter::indexBits() const
    {
        unsigned top = up()->params().maxSize();
        return top > m_max_size ? top - m_max_size : 0;
    }

    void Fragmenter::validate() const
    {
        requireOneEach("Fragmenter", true);
        const Link &u = *up(), &d = *down();
        if (m_max_size > d.params().maxSize())
            throw ConfigError("Fragmenter " + m_name + ": max_size " + std::to_string(m_max_size) + " exceeds the downstream link's largest size " +
                              std::to_string(d.params().maxSize()) + " (z=" + std::to_string(d.params().z) + ")");
        // Plan §3.5: an Atomic, an Intent or any TL-C message larger than max_size cannot be
        // split and is refused at elaboration. The configuration declares the largest such
        // message the masters issue (max_unsplit, default: the largest the link can carry).
        unsigned unsplit = m_max_unsplit >= 0 ? unsigned(m_max_unsplit) : u.params().maxSize();
        bool can_carry = u.level() != Level::UL;   // TL-UL has no atomics, Intents or TL-C messages
        if (can_carry && unsplit > m_max_size)
            throw ConfigError("Fragmenter " + m_name + ": the upstream link " + u.name() + " (TL-" + levelName(u.level()) + ") can carry an " +
                              (u.level() == Level::C ? "Acquire, Release, atomic or Intent" : "ArithmeticData, LogicalData or Intent") +
                              " of size up to " + std::to_string(unsplit) + " but max_size is " + std::to_string(m_max_size) +
                              ": such a message cannot be split (plan §3.5); raise max_size, or declare max_unsplit(i) <= " +
                              std::to_string(m_max_size) + " as the largest unsplittable message the masters issue");
        // §6.4: the fragment index goes in the high source bits, so downstream needs o + k.
        unsigned k = indexBits();
        if (u.params().o + k > d.params().o)
            throw ConfigError("Fragmenter " + m_name + ": the downstream link " + d.name() + " needs o >= " + std::to_string(u.params().o + k) +
                              " (upstream o=" + std::to_string(u.params().o) + " plus " + std::to_string(k) + " fragment-index bits) but has o=" +
                              std::to_string(d.params().o));
    }

    void Fragmenter::reset(uint64_t)
    {
        validate();
        m_ops.clear();
        m_a_cur = nullptr;
        m_d_pending.clear();
        m_d_pass = false;
        m_a = BeatQueue(); m_a.out = &down()->chan(Channel::A); m_a.depth = m_depth;
        m_d = BeatQueue(); m_d.out = &up()->chan(Channel::D); m_d.depth = m_depth;
        m_in[0] = &up()->chan(Channel::A);
        m_in[3] = &down()->chan(Channel::D);
        for (int c : {1, 2, 4})
        {
            Channel ch = static_cast<Channel>(c);
            m_pass[c] = BeatQueue();
            m_in[c] = nullptr;
            if (!up()->has(ch)) continue;
            m_pass[c].out = &outChannel(ch);
            m_pass[c].depth = m_depth;
            m_in[c] = &inChannel(ch);
        }
    }

    uint64_t Fragmenter::sourceMask() const
    {
        unsigned o = up()->params().o;
        return o >= 64 ? ~uint64_t(0) : (uint64_t(1) << o) - 1;
    }

    bool Fragmenter::splittable(const Msg &m) const
    {
        return m.channel == Channel::A && (m.opcode == unsigned(OpA::Get) || m.opcode == unsigned(OpA::PutFullData) ||
                                           m.opcode == unsigned(OpA::PutPartialData));
    }

    void Fragmenter::offer(uint64_t)
    {
        m_a.offer();
        m_d.offer();
        for (auto &q : m_pass) q.offer();
    }

    void Fragmenter::accept(uint64_t)
    {
        // A: from the stage's room (plan §3.2).
        m_in[0]->setReady(m_a.space());
        // D: a fragment's response is sunk into its operation's buffer unconditionally (the
        // buffer exists because the request was issued); a pass-through response needs room in
        // the D stage. Ready may look at the offered beat's source (§4.1).
        bool r = m_d.space();
        if (!r && m_in[3]->valid())
        {
            r = m_ops.count(m_in[3]->offered().msg->source & sourceMask()) > 0;
        }
        m_in[3]->setReady(r);
        for (int c : {1, 2, 4}) if (m_in[c]) m_in[c]->setReady(m_pass[c].space());
    }

    void Fragmenter::issueReady(Op &op, uint64_t last_byte_arrived)
    {
        // Fragments go out in address order, each as soon as the request beats covering its
        // bytes have arrived (for a Get, all at once).
        while (op.issued < op.nfrag)
        {
            Msg &f = *op.frags[op.issued];
            uint64_t end = f.address + f.bytes() - 1;
            if (f.hasData() && end > last_byte_arrived) break;
            m_a.pushAll(op.frags[op.issued]);
            op.issued++;
        }
    }

    void Fragmenter::onRequestBeat(const Beat &b)
    {
        const Msg &m = *b.msg;
        if (b.index == 0)
        {
            if (!splittable(m) || m.size <= m_max_size)
            {
                if (!splittable(m) && m.size > m_max_size)
                    fatal("Fragmenter " + m_name + ": " + m.describe() + " is larger than max_size " + std::to_string(m_max_size) +
                          " and cannot be split (plan §3.5); the configuration declared max_unsplit " +
                          std::to_string(m_max_unsplit >= 0 ? unsigned(m_max_unsplit) : up()->params().maxSize()));
                // Pass through unchanged: with fragment index 0 the downstream source equals
                // the upstream one, so the same Msg object travels on.
                m_a_cur = b.last() ? nullptr : b.msg;
                m_a.push(b.msg, 0);
                return;
            }
            if (m_ops.count(m.source))
                fatal("Fragmenter " + m_name + ": source " + std::to_string(m.source) + " reused while outstanding (" + m.describe() + ") (§6.4)");
            Op &op = m_ops[m.source];
            op.req = b.msg;
            op.nfrag = 1u << (m.size - m_max_size);
            unsigned o = up()->params().o;
            for (unsigned f = 0; f < op.nfrag; f++)
            {
                // Fragment f: size max_size at address + f*2^max_size, index n-1-f in the high
                // source bits (the last fragment carries index 0).
                MsgPtr fr = cloneMsg(m, m.w);
                fr->size = uint8_t(m_max_size);
                fr->address = m.address + (uint64_t(f) << m_max_size);
                fr->source = m.source | (uint64_t(op.nfrag - 1 - f) << o);
                fr->layout(m.w, m.data_mode);          // beats and the derived mask for the new size (§4.5)
                op.frags.push_back(fr);
            }
            if (!m.hasData()) { issueReady(op, 0); return; }
        }
        if (m_a_cur || (!splittable(m) || m.size <= m_max_size))
        {
            // a later beat of a pass-through burst
            m_a.push(b.msg, b.index);
            if (b.last()) m_a_cur = nullptr;
            return;
        }
        Op &op = m_ops[m.source];
        uint64_t first, last;
        if (beatRange(m, b.index, first, last))
        {
            // Copy this beat's bytes (mask, data) into the fragments they belong to; corrupt is
            // per beat (§4.4) and lands on the fragment beats these bytes fill.
            for (uint64_t x = first; x <= last; x++)
            {
                unsigned f = unsigned((x - m.address) >> m_max_size);
                copyByte(*op.frags[f], m, x);
                if (m.beatCorrupt(b.index)) op.frags[f]->corrupt[lanePos(*op.frags[f], x).beat] = 1;
            }
            issueReady(op, last);
        }
    }

    void Fragmenter::onResponseBeat(const Beat &b)
    {
        const Msg &r = *b.msg;
        unsigned o = up()->params().o;
        uint64_t src = r.source & sourceMask(), idx = o >= 64 ? 0 : r.source >> o;
        auto it = m_ops.find(src);
        if (it == m_ops.end())
        {
            if (idx != 0)
                fatal("Fragmenter " + m_name + ": response " + r.describe() + " carries fragment index " + std::to_string(idx) +
                      " but no fragmented operation with source " + std::to_string(src) + " is outstanding");
            // A pass-through response, source unchanged. While its burst is in progress the
            // reassembled responses wait (§4.1: bursts on one channel are not interleaved).
            m_d.push(b.msg, b.index);
            m_d_pass = !b.last();
            if (b.last()) { for (auto &q : m_d_pending) m_d.pushAll(q); m_d_pending.clear(); }
            return;
        }
        Op &op = it->second;
        if (idx >= op.nfrag)
            fatal("Fragmenter " + m_name + ": response " + r.describe() + " carries fragment index " + std::to_string(idx) +
                  " but the operation has " + std::to_string(op.nfrag) + " fragments");
        unsigned f = op.nfrag - 1 - unsigned(idx);
        const Msg &frag = *op.frags[f];
        if (!op.resp)
        {
            // The reassembled response: the request's size and source, the fragment's opcode.
            op.resp = cloneMsg(r, r.w);
            op.resp->size = op.req->size;
            op.resp->source = op.req->source;
            op.resp->denied = false;
            op.resp->layout(r.w, r.data_mode);
            // §4.5-style placement needs an address; D has none on the wire, so the copy
            // carries the request's address for the lane math only (the transcript prints it).
            op.resp->address = op.req->address;
        }
        op.denied = op.denied || r.denied;                   // plan §3.5: denied = OR of the fragments
        if (r.hasData())
        {
            // The fragment's response beat carries the bytes of the fragment's range, placed by
            // the fragment's address (§4.5; D has no address field, the request places it);
            // they go to their place in the full response, placed by the request's address.
            uint64_t fgeom = frag.address, rgeom = op.req->address, first, last;
            if (beatRange(fgeom, frag.bytes(), r.w, b.index, first, last))
                for (uint64_t x = first; x <= last; x++)
                {
                    copyByte(*op.resp, rgeom, r, fgeom, x);
                    if (r.beatCorrupt(b.index)) op.resp->corrupt[lanePos(rgeom, r.w, x).beat] = 1;   // corrupt per beat as returned
                }
        }
        if (!b.last()) return;
        op.answered++;
        if (op.answered < op.nfrag) return;
        // Every fragment answered: one response upstream, the ID released with it.
        op.resp->denied = op.denied;
        if (op.denied && m_deny_all && op.resp->hasData())
            for (auto &c : op.resp->corrupt) c = 1;           // §4.4: a denied data response corrupt on every beat
        if (m_d_pass) m_d_pending.push_back(op.resp);
        else m_d.pushAll(op.resp);
        m_ops.erase(it);
    }

    void Fragmenter::commit(uint64_t)
    {
        m_a.commit();
        m_d.commit();
        for (auto &q : m_pass) q.commit();
        const Beat &a = m_in[0]->accepted();
        if (a.msg) onRequestBeat(a);
        const Beat &d = m_in[3]->accepted();
        if (d.msg) onResponseBeat(d);
        for (int c : {1, 2, 4})
        {
            if (!m_in[c]) continue;
            const Beat &b = m_in[c]->accepted();
            if (!b.msg) continue;
            // Plan §3.5: a TL-C message is never split; one larger than max_size would overrun
            // the slave (§6.3), which the declared max_unsplit was supposed to exclude.
            if (b.index == 0 && b.msg->hasData() && b.msg->size > m_max_size)
                fatal("Fragmenter " + m_name + ": " + b.msg->describe() + " is larger than max_size " + std::to_string(m_max_size) +
                      " and cannot be split (plan §3.5)");
            m_pass[c].push(b.msg, b.index);
        }
    }

    bool Fragmenter::idle() const
    {
        if (!m_a.empty() || !m_d.empty() || !m_ops.empty() || m_a_cur || !m_d_pending.empty()) return false;
        for (auto &q : m_pass) if (!q.empty()) return false;
        return true;
    }

    ProgressHints Fragmenter::hints() const
    {
        // Plan §3.6: a split operation is answered after its last fragment's response; the
        // fragments of the largest request leave one after the other through the A stage.
        ProgressHints h;
        uint64_t nfrag = uint64_t(1) << indexBits();
        h.latency = 1 + nfrag * down()->params().beatsOf(true, m_max_size);
        h.queue_depth = m_depth;
        return h;
    }
}}
