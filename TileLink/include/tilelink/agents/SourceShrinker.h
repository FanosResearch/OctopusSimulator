/*
 * File  :      SourceShrinker.h
 * SourceShrinker (plan §3.5): fewer source IDs downstream. Between one upstream link with
 * o_up source bits and one downstream link with o_down bits (TL-UL/UH only, same width), it
 * maps each outstanding upstream source to one of a pool of at most 2^o_down downstream
 * sources and maps the response back (§6.4: the mapping an agent performs on identifiers is
 * implementation defined; a D response copies the request's source, so the mapping is
 * undone on D). When the pool is empty it holds the A channel with ready LOW: legal under
 * rule 3 of §5, because the pool drains by the slave's own rule 3ii (every received request
 * is eventually answered). A TL-C link is refused at elaboration: after an Acquire completes,
 * the master's source is what routes B-channel Probes back to its cache (Table 14), and a
 * remapped source would break them.
 *
 * Config keys (agent[k].*): pool(i) the number of downstream sources used (default and
 * maximum 2^o_down); depth(i) output stage depth (default 2).
 */
#ifndef _TL_AGENTS_SOURCESHRINKER_H
#define _TL_AGENTS_SOURCESHRINKER_H
#include "tilelink/agents/Pipe.h"
#include <map>

namespace octopus { namespace tl
{
    class SourceShrinker : public TopologyAgent
    {
    public:
        SourceShrinker(const std::string &name, const Config &cfg);
        void validate() const override;
        void reset(uint64_t reset_cycles) override;
        void offer(uint64_t cycle) override;
        void accept(uint64_t cycle) override;
        void commit(uint64_t cycle) override;
        bool idle() const override;
        ProgressHints hints() const override;

        uint64_t poolSize() const { return m_pool; }
        size_t   poolFree() const { return m_free.size(); }
        uint64_t blockedCycles() const { return m_blocked; }   // cycles A was held for an empty pool

    private:
        uint64_t  m_pool;
        unsigned  m_depth;
        std::deque<uint64_t> m_free;                  // free downstream sources
        std::map<uint64_t, uint64_t> m_up_of;         // downstream source -> upstream source
        BeatQueue m_a, m_d;
        ChannelState *m_a_in = nullptr, *m_d_in = nullptr;
        MsgPtr    m_a_clone, m_d_clone;               // the copies of the messages in progress
        uint64_t  m_blocked = 0;
    };
}}
#endif /* _TL_AGENTS_SOURCESHRINKER_H */
