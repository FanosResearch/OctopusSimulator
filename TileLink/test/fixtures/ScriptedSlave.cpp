/*
 * File  :      ScriptedSlave.cpp
 * Test fixture: see ScriptedSlave.h.
 */
#include "ScriptedSlave.h"

namespace octopus { namespace tl { namespace fixture
{
    Link &ScriptedSlave::link() const
    {
        for (auto &lr : m_links) if (lr.role == Role::Slave) return *lr.link;
        fatal("ScriptedSlave " + m_name + " has no slave link");
    }

    MsgPtr ScriptedSlave::defaultResponse(const Msg &req)
    {
        std::vector<Response> r = responseOf(req.channel, req.opcode);
        if (r.empty()) return nullptr;
        MsgPtr m = makeMsg(*m_kernel, r[0].ch, r[0].opcode, req.size, link().params().w, req.address, req.source, req.data_mode);
        m->sink = req.sink;
        return m;
    }

    void ScriptedSlave::offer(uint64_t t) { doOffer(*m_kernel, link(), t, false); }
    void ScriptedSlave::accept(uint64_t t) { doAccept(*m_kernel, link(), t, false); }
    void ScriptedSlave::commit(uint64_t t)
    {
        size_t before = received.size();
        doCommit(link(), t, false);
        if (!autoRespond) return;
        for (size_t i = before; i < received.size(); i++)
        {
            const Beat &b = received[i].second;
            if (!b.last() || !b.msg->isRequest()) continue;
            MsgPtr r = respond ? respond(*b.msg) : defaultResponse(*b.msg);
            if (!r) continue;
            if (tweak) tweak(*r, *b.msg);
            send(t + (delay ? delay : 1), r->channel, r);
        }
    }
}}}
