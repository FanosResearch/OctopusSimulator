/*
 * File  :      Throttle.h
 * Throttle (plan §3.5, §3.2): a Buffer that withholds ready, and optionally valid, at random,
 * from the agent's seeded RNG (plan §3.2: all randomness from one seeded RNG per agent, so
 * the same seed gives the same schedule). It is the stress tool: withholding ready is legal
 * (§5.2, the refresh-waiting DDR is conforming because ready eventually rises); withholding
 * valid is legal only before a message's first beat has been accepted (§4.1: a rejected
 * first beat may be retracted; plan §3.6 M-BURST: a burst in progress is never abandoned), so
 * the Throttle never lowers valid on a beat of a message whose first beat has been accepted
 * downstream.
 *
 * Config keys (agent[k].*): p_ready(d) default 0.5, probability per cycle and channel that
 * ready is withheld although the queue has space; p_valid(d) default 0, probability per cycle
 * and channel that the FIRST beat of a not-yet-accepted message is not offered; depth(i)
 * default 2 and depth_A(i) .. depth_E(i) as for the Buffer.
 */
#ifndef _TL_AGENTS_THROTTLE_H
#define _TL_AGENTS_THROTTLE_H
#include "tilelink/agents/Pipe.h"

namespace octopus { namespace tl
{
    class Throttle : public TopologyAgent
    {
    public:
        Throttle(const std::string &name, const Config &cfg);
        void validate() const override;
        void reset(uint64_t reset_cycles) override;
        void offer(uint64_t cycle) override;
        void accept(uint64_t cycle) override;
        void commit(uint64_t cycle) override;
        bool idle() const override;
        ProgressHints hints() const override;

        // Counters, for tests and reports.
        uint64_t readyWithheld() const { return m_ready_withheld; }
        uint64_t validWithheld() const { return m_valid_withheld; }

    private:
        double draw();   // uniform in [0, 1) from the agent's RNG, deterministic per seed
        double    m_p_ready, m_p_valid;
        unsigned  m_depth[NUM_CHANNELS];
        BeatQueue m_q[NUM_CHANNELS];
        ChannelState *m_in[NUM_CHANNELS] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        uint64_t  m_ready_withheld = 0, m_valid_withheld = 0;
    };
}}
#endif /* _TL_AGENTS_THROTTLE_H */
