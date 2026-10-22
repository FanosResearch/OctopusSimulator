/*
 * File  :      SourceShrinker.cpp
 * SourceShrinker (plan §3.5): a pool of downstream sources, A held while it is empty.
 */
#include "tilelink/agents/SourceShrinker.h"

namespace octopus { namespace tl
{
    SourceShrinker::SourceShrinker(const std::string &name, const Config &cfg) : TopologyAgent(name)
    {
        int p = cfg.get<int>("pool", 0);
        if (p < 0) throw ConfigError("SourceShrinker " + name + ": pool must be >= 1 (0 = the whole downstream range)");
        m_pool = uint64_t(p);
        int d = cfg.get<int>("depth", 2);
        if (d <= 0) throw ConfigError("SourceShrinker " + name + ": depth must be >= 1");
        m_depth = unsigned(d);
    }

    void SourceShrinker::validate() const
    {
        requireOneEach("SourceShrinker", true);
        const Link &u = *up(), &d = *down();
        // Plan §3.5: TL-UL/UH only; on a TL-C link the source routes Probes (Table 14).
        if (u.level() == Level::C)
            throw ConfigError("SourceShrinker " + m_name + ": link " + u.name() + " is TL-C; a remapped source would break the routing of "
                              "B-channel Probes to the master's cache (Table 14), so the shrinker is refused on TL-C links (plan §3.5)");
        uint64_t pool = m_pool ? m_pool : d.params().numSources();
        if (pool > d.params().numSources())
            throw ConfigError("SourceShrinker " + m_name + ": pool " + std::to_string(pool) + " exceeds the downstream link's 2^o = " +
                              std::to_string(d.params().numSources()) + " sources");
    }

    void SourceShrinker::reset(uint64_t)
    {
        validate();
        if (m_pool == 0) m_pool = down()->params().numSources();
        m_free.clear();
        for (uint64_t s = 0; s < m_pool; s++) m_free.push_back(s);
        m_up_of.clear();
        m_a = BeatQueue(); m_a.out = &down()->chan(Channel::A); m_a.depth = m_depth;
        m_d = BeatQueue(); m_d.out = &up()->chan(Channel::D); m_d.depth = m_depth;
        m_a_in = &up()->chan(Channel::A);
        m_d_in = &down()->chan(Channel::D);
        m_a_clone = m_d_clone = nullptr;
        m_blocked = 0;
    }

    void SourceShrinker::offer(uint64_t) { m_a.offer(); m_d.offer(); }

    void SourceShrinker::accept(uint64_t)
    {
        // A: a new message (beat 0) needs a free downstream source; without one, ready LOW
        // (plan §3.5). Later beats of a message in progress have theirs already. Ready may
        // look at the offered beat (§4.1).
        bool r = m_a.space();
        if (r && m_a_in->valid() && m_a_in->offered().index == 0 && m_free.empty()) { r = false; m_blocked++; }
        m_a_in->setReady(r);
        // D: from the stage's room only; never from A (§4.1 priority).
        m_d_in->setReady(m_d.space());
    }

    void SourceShrinker::commit(uint64_t)
    {
        m_a.commit();
        m_d.commit();
        const Beat &a = m_a_in->accepted();
        if (a.msg)
        {
            if (a.index == 0)
            {
                uint64_t id = m_free.front();
                m_free.pop_front();
                m_up_of[id] = a.msg->source;
                m_a_clone = cloneMsg(*a.msg, a.msg->w);
                m_a_clone->source = id;                        // §6.4: remapped into the pool
            }
            copyBeat(*m_a_clone, a.index, *a.msg, a.index);
            m_a.push(m_a_clone, a.index);
            if (a.last()) m_a_clone = nullptr;
        }
        const Beat &d = m_d_in->accepted();
        if (d.msg)
        {
            auto it = m_up_of.find(d.msg->source);
            if (d.index == 0)
            {
                if (it == m_up_of.end())
                    fatal("SourceShrinker " + m_name + ": response " + d.msg->describe() + " for a downstream source that is not outstanding (§6.4)");
                m_d_clone = cloneMsg(*d.msg, d.msg->w);
                m_d_clone->source = it->second;                // the master's own source back (Table 14)
            }
            copyBeat(*m_d_clone, d.index, *d.msg, d.index);
            m_d.push(m_d_clone, d.index);
            if (d.last())
            {
                // §6.4: outstanding up to and including the cycle the response is received;
                // the downstream source is free again from the next cycle.
                m_free.push_back(d.msg->source);
                m_up_of.erase(d.msg->source);
                m_d_clone = nullptr;
            }
        }
    }

    bool SourceShrinker::idle() const { return m_a.empty() && m_d.empty() && m_up_of.empty() && !m_a_clone && !m_d_clone; }

    ProgressHints SourceShrinker::hints() const
    {
        // Plan §3.6: one cycle through; a request blocked on an empty pool waits for the
        // slave's own answers (rule 3ii), which the slave's latency in the formula covers.
        ProgressHints h;
        h.latency = 1;
        h.queue_depth = m_depth;
        return h;
    }
}}
