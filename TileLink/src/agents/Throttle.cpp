/*
 * File  :      Throttle.cpp
 * Throttle (plan §3.5): random ready/valid withholding at a link.
 */
#include "tilelink/agents/Throttle.h"
#include <cmath>

namespace octopus { namespace tl
{
    Throttle::Throttle(const std::string &name, const Config &cfg) : TopologyAgent(name)
    {
        m_p_ready = cfg.get<double>("p_ready", 0.5);
        m_p_valid = cfg.get<double>("p_valid", 0.0);
        if (m_p_ready < 0.0 || m_p_ready > 1.0) throw ConfigError("Throttle " + name + ": p_ready must be in [0, 1]");
        if (m_p_valid < 0.0 || m_p_valid > 1.0) throw ConfigError("Throttle " + name + ": p_valid must be in [0, 1]");
        int d = cfg.get<int>("depth", 2);
        for (int c = 0; c < NUM_CHANNELS; c++)
        {
            std::string key = std::string("depth_") + channelName(static_cast<Channel>(c));
            int dc = cfg.get<int>(key, d);
            if (dc <= 0) throw ConfigError("Throttle " + name + ": depth must be >= 1");
            m_depth[c] = unsigned(dc);
        }
    }

    void Throttle::validate() const { requireOneEach("Throttle", true); }

    void Throttle::reset(uint64_t)
    {
        validate();
        for (int c = 0; c < NUM_CHANNELS; c++)
        {
            Channel ch = static_cast<Channel>(c);
            if (!up()->has(ch)) continue;
            m_q[c].out = &outChannel(ch);
            m_q[c].depth = m_depth[c];
            m_q[c].q.clear();
            m_in[c] = &inChannel(ch);
        }
    }

    double Throttle::draw()
    {
        // 53 random bits to a double in [0, 1): the same on every platform for the same seed.
        return double(rng()() >> 11) * (1.0 / 9007199254740992.0);
    }

    void Throttle::offer(uint64_t)
    {
        for (int c = 0; c < NUM_CHANNELS; c++)
        {
            BeatQueue &q = m_q[c];
            if (!q.out) continue;
            // One draw per channel per cycle whether or not there is a beat, so the schedule
            // depends on the seed alone, not on the traffic.
            bool withhold = draw() < m_p_valid;
            if (q.empty()) continue;
            // §4.1 / plan §3.6 M-BURST: valid may be withheld only for the first beat of a
            // message not yet accepted downstream (beat 0 and no burst in progress there).
            // Once beat 0 is accepted the burst is in progress and every beat is presented.
            bool first_unaccepted = q.q.front().index == 0 && !q.out->inProgress();
            if (withhold && first_unaccepted) { m_valid_withheld++; continue; }
            q.offer();
        }
    }

    void Throttle::accept(uint64_t)
    {
        for (int c = 0; c < NUM_CHANNELS; c++)
        {
            if (!m_in[c]) continue;
            // §5.2: withholding ready for a while is legal (the DDR waiting for a refresh);
            // rule 3i holds because p_ready < 1 lets ready rise eventually. Each channel's
            // ready depends on its own queue and its own draw only (§4.1).
            bool withhold = draw() < m_p_ready;
            bool space = m_q[c].space();
            if (withhold && space && m_in[c]->valid()) m_ready_withheld++;
            m_in[c]->setReady(space && !withhold);
        }
    }

    void Throttle::commit(uint64_t)
    {
        for (int c = 0; c < NUM_CHANNELS; c++)
        {
            if (!m_in[c]) continue;
            m_q[c].commit();
            const Beat &b = m_in[c]->accepted();
            if (b.msg) m_q[c].push(b.msg, b.index);
        }
    }

    bool Throttle::idle() const
    {
        for (auto &q : m_q) if (!q.empty()) return false;
        return true;
    }

    ProgressHints Throttle::hints() const
    {
        // Plan §3.6: a withholding run is geometric, so no finite bound holds always; the
        // declared latency is the run length exceeded with probability below 1e-9 per beat
        // (p^n < 1e-9), ready and valid withholding added, plus the cycle through.
        auto run = [](double p) -> uint64_t
        {
            if (p <= 0.0) return 0;
            if (p >= 1.0) return uint64_t(1) << 40;   // never released: the watchdog must trip
            return uint64_t(std::ceil(std::log(1e-9) / std::log(p)));
        };
        ProgressHints h;
        h.latency = 1 + run(m_p_ready) + run(m_p_valid);
        h.queue_depth = 1;
        for (unsigned d : m_depth) if (d > h.queue_depth) h.queue_depth = d;
        return h;
    }
}}
