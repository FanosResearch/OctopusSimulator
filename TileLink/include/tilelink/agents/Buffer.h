/*
 * File  :      Buffer.h
 * Buffer (plan §3.5): per-channel queues between one upstream and one downstream link of the
 * same width and level. Depth per channel, pipe behaviour: a beat accepted at t is offered on
 * the other link at t+1 (plan §3.2), and a queue of depth d holds d beats. Every channel the
 * links have is buffered (A, C, E downward; B, D upward), each with its own queue, so a
 * high-priority channel never waits for a low-priority one (§4.1, §5).
 *
 * Config keys (agent[k].*): depth(i) default 2 (per channel), depth_A(i) .. depth_E(i)
 * overrides. Depth 0 is refused: plan §3.2 says it means "no buffer", so connect the link
 * directly instead of instantiating one.
 */
#ifndef _TL_AGENTS_BUFFER_H
#define _TL_AGENTS_BUFFER_H
#include "tilelink/agents/Pipe.h"

namespace octopus { namespace tl
{
    class Buffer : public TopologyAgent
    {
    public:
        Buffer(const std::string &name, const Config &cfg);
        void validate() const override;
        void reset(uint64_t reset_cycles) override;
        void offer(uint64_t cycle) override;
        void accept(uint64_t cycle) override;
        void commit(uint64_t cycle) override;
        bool idle() const override;
        ProgressHints hints() const override;
        unsigned depth(Channel ch) const { return m_depth[static_cast<int>(ch)]; }

    private:
        unsigned  m_depth[NUM_CHANNELS];
        BeatQueue m_q[NUM_CHANNELS];     // q[ch].out = the channel this agent sends on
        ChannelState *m_in[NUM_CHANNELS] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    };
}}
#endif /* _TL_AGENTS_BUFFER_H */
