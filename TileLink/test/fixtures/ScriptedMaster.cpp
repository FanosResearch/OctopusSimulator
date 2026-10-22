/*
 * File  :      ScriptedMaster.cpp
 * Test fixture: see ScriptedMaster.h.
 */
#include "ScriptedMaster.h"

namespace octopus { namespace tl { namespace fixture
{
    MsgPtr makeMsg(Kernel &k, Channel ch, unsigned opcode, unsigned size, unsigned w, uint64_t address,
                   uint64_t source, DataMode mode, unsigned param)
    {
        MsgPtr m = std::make_shared<Msg>();
        m->channel = ch; m->opcode = uint8_t(opcode); m->size = uint8_t(size); m->param = uint8_t(param);
        m->address = address; m->source = source;
        m->layout(w, mode);                       // derives the mask from size and address (§4.5)
        m->id = k.newMsgId();
        return m;
    }

    void setMask(Msg &m, unsigned beat, uint64_t bits)
    {
        for (unsigned l = 0; l < m.w; l++) m.beatMask(beat)[l] = (l < 64 && ((bits >> l) & 1)) ? 1 : 0;
    }

    static unsigned firstLane(const Msg &m) { return m.bytes() >= m.w ? 0 : unsigned(m.address % m.w); }
    static unsigned activeBytes(const Msg &m) { return unsigned(m.bytes() >= m.w ? m.w : m.bytes()); }

    void setData(Msg &m, unsigned beat, uint64_t value)
    {
        uint8_t *d = m.beatData(beat);
        if (!d) return;
        unsigned f = firstLane(m), n = activeBytes(m);
        for (unsigned i = 0; i < n && i < 8; i++) d[f + i] = uint8_t(value >> (8 * i));
    }
    uint64_t getData(const Msg &m, unsigned beat)
    {
        const uint8_t *d = m.beatData(beat);
        if (!d) return 0;
        uint64_t v = 0;
        unsigned f = firstLane(m), n = activeBytes(m);
        for (unsigned i = 0; i < n && i < 8; i++) v |= uint64_t(d[f + i]) << (8 * i);
        return v;
    }

    static bool sentBy(Channel ch, bool master) { return masterSends(ch) == master; }

    void ScriptEngine::doOffer(Kernel &, Link &l, uint64_t t, bool master)
    {
        bool used[NUM_CHANNELS] = {false, false, false, false, false};
        for (auto &o : offers)
        {
            if (o.cycle != t || !sentBy(o.ch, master)) continue;
            int c = static_cast<int>(o.ch);
            if (used[c]) continue;
            used[c] = true;
            if (o.before) o.before(*o.msg);
            if (!l.has(o.ch)) { if (missingChannel) missingChannel(o.ch, *o.msg, o.beat, t); continue; }
            l.chan(o.ch).offer(o.msg, o.beat);
        }
        for (auto &a : autos)
        {
            int c = static_cast<int>(a.ch);
            if (a.done || used[c] || !sentBy(a.ch, master)) continue;
            used[c] = true;                       // the adaptive messages of a channel go in order
            if (a.start > t) continue;
            if (!l.has(a.ch)) { if (missingChannel) missingChannel(a.ch, *a.msg, a.next, t); continue; }
            l.chan(a.ch).offer(a.msg, a.next);
        }
    }

    void ScriptEngine::doAccept(Kernel &k, Link &l, uint64_t t, bool master)
    {
        l.forEachChannel([&](ChannelState &c)
        {
            if (sentBy(c.id(), master)) return;   // a receive channel of this agent
            bool r = !not_ready[static_cast<int>(c.id())].count(t);
            if (k.inReset() && !ready_in_reset) r = false;
            c.setReady(r);
        });
    }

    void ScriptEngine::doCommit(Link &l, uint64_t t, bool master)
    {
        l.forEachChannel([&](ChannelState &c)
        {
            const Beat &b = c.accepted();
            if (!b.msg) return;
            if (!sentBy(c.id(), master)) { received.push_back({t, b}); return; }
            for (auto &a : autos)
                if (!a.done && a.msg == b.msg && a.ch == c.id())
                {
                    a.next = b.index + 1;
                    if (b.last()) a.done = true;
                    break;
                }
        });
    }

    Link &ScriptedMaster::link() const
    {
        for (auto &lr : m_links) if (lr.role == Role::Master) return *lr.link;
        fatal("ScriptedMaster " + m_name + " has no master link");
    }
    void ScriptedMaster::offer(uint64_t t) { doOffer(*m_kernel, link(), t, true); }
    void ScriptedMaster::accept(uint64_t t) { doAccept(*m_kernel, link(), t, true); }
    void ScriptedMaster::commit(uint64_t t) { doCommit(link(), t, true); }
}}}
