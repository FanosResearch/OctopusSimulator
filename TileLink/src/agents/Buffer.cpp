/*
 * File  :      Buffer.cpp
 * Buffer (plan §3.5, §3.2).
 */
#include "tilelink/agents/Buffer.h"

namespace octopus { namespace tl
{
    Buffer::Buffer(const std::string &name, const Config &cfg) : TopologyAgent(name)
    {
        int d = cfg.get<int>("depth", 2);
        for (int c = 0; c < NUM_CHANNELS; c++)
        {
            std::string key = std::string("depth_") + channelName(static_cast<Channel>(c));
            int dc = cfg.get<int>(key, d);
            if (dc <= 0)
                throw ConfigError("Buffer " + name + ": " + (cfg.has(key) ? key : std::string("depth")) + " must be >= 1 (depth 0 means no buffer: connect the link directly, plan §3.2)");
            m_depth[c] = unsigned(dc);
        }
    }

    void Buffer::validate() const { requireOneEach("Buffer", true); }

    void Buffer::reset(uint64_t)
    {
        validate();
        for (int c = 0; c < NUM_CHANNELS; c++)
        {
            Channel ch = static_cast<Channel>(c);
            if (!up()->has(ch)) continue;
            // §4: A, C, E flow master to slave (downward here); B, D slave to master.
            m_q[c].out = &outChannel(ch);
            m_q[c].depth = m_depth[c];
            m_q[c].q.clear();
            m_in[c] = &inChannel(ch);
        }
    }

    void Buffer::offer(uint64_t) { for (auto &q : m_q) q.offer(); }

    void Buffer::accept(uint64_t)
    {
        // Ready from the queue's state at the start of the cycle (plan §3.2): never from the
        // downstream ready, and each channel's ready from its own queue only (§4.1).
        for (int c = 0; c < NUM_CHANNELS; c++) if (m_in[c]) m_in[c]->setReady(m_q[c].space());
    }

    void Buffer::commit(uint64_t)
    {
        for (int c = 0; c < NUM_CHANNELS; c++)
        {
            if (!m_in[c]) continue;
            m_q[c].commit();                                  // the front beat left
            const Beat &b = m_in[c]->accepted();
            if (b.msg) m_q[c].push(b.msg, b.index);           // the same Msg object travels on (plan §3.3)
        }
    }

    bool Buffer::idle() const
    {
        for (auto &q : m_q) if (!q.empty()) return false;
        return true;
    }

    ProgressHints Buffer::hints() const
    {
        // Plan §3.6 watchdog formula: one cycle through, the deepest channel queue ahead.
        ProgressHints h;
        h.latency = 1;
        h.queue_depth = 1;
        for (unsigned d : m_depth) if (d > h.queue_depth) h.queue_depth = d;
        return h;
    }
}}
