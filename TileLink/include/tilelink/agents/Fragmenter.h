/*
 * File  :      Fragmenter.h
 * Fragmenter (plan §3.5): between one upstream and one downstream link of the same width and
 * level, it splits a Get, PutFullData or PutPartialData larger than `max_size` into
 * 2^(size - max_size) operations of size max_size, in address order (§6.3: an adapter may
 * fragment operations to fit the slave). Nothing else is split: an ArithmeticData, a
 * LogicalData, an Intent or a TL-C message larger than max_size is refused at elaboration
 * (a split atomic would lose atomicity; a split Acquire its meaning), which the configuration
 * declares through `max_unsplit`. Fragment f of n gets the request's source plus the index
 * n-1-f in the high source bits (the last fragment has index 0), so the downstream link needs
 * o + k source bits, k = (largest upstream size) - max_size; elaboration checks that. The
 * responses, which may return in any order (§6.5), are reassembled into one response in
 * address order: `denied` is the OR of the fragments' `denied`, `corrupt` is per beat as
 * returned. The upstream source stays outstanding until the reassembled response has left, so
 * the ID is held until the last fragment's response. Known semantic gap, recorded in plan
 * §3.5: a fragment denied after earlier fragments were written leaves a partial effect.
 *
 * Config keys (agent[k].*):
 *   max_size(i)            the largest size (log2 bytes) the slave accepts; required
 *   max_unsplit(i)         the largest size of an unsplittable message (atomic, Intent, TL-C)
 *                          the masters issue; default = the upstream link's 2^z - 1. Must be
 *                          <= max_size (ConfigError otherwise); a larger one at run time is fatal
 *   deny_corrupts_all(i)   default 1: a denied reassembled data response is marked corrupt on
 *                          every beat, as §4.4 requires of any denied data response. 0 keeps
 *                          the fragments' corrupt bits as returned (not conforming; tests only)
 *   depth(i)               output stage depth (default 2)
 */
#ifndef _TL_AGENTS_FRAGMENTER_H
#define _TL_AGENTS_FRAGMENTER_H
#include "tilelink/agents/Pipe.h"
#include <map>

namespace octopus { namespace tl
{
    class Fragmenter : public TopologyAgent
    {
    public:
        Fragmenter(const std::string &name, const Config &cfg);
        void validate() const override;
        void reset(uint64_t reset_cycles) override;
        void offer(uint64_t cycle) override;
        void accept(uint64_t cycle) override;
        void commit(uint64_t cycle) override;
        bool idle() const override;
        ProgressHints hints() const override;

        unsigned maxSize() const { return m_max_size; }
        unsigned indexBits() const;                 // k: the extra source bits downstream
        uint64_t fragmentsOutstanding() const { return m_ops.size(); }

    private:
        // One fragmented operation: the request, its fragments and the response being rebuilt.
        struct Op
        {
            MsgPtr   req;                 // the upstream request
            unsigned nfrag = 0;
            std::vector<MsgPtr> frags;    // in address order; frags[f] issued when its bytes arrived
            unsigned issued = 0;          // fragments whose beats went to the A stage
            MsgPtr   resp;                // the reassembled response (created on the first fragment response)
            unsigned answered = 0;        // fragments whose response has fully arrived
            bool     denied = false;
        };
        uint64_t sourceMask() const;                // the upstream o low bits
        bool splittable(const Msg &m) const;        // Get, PutFullData, PutPartialData
        void onRequestBeat(const Beat &b);
        void onResponseBeat(const Beat &b);
        void issueReady(Op &op, uint64_t last_byte_arrived);

        unsigned m_max_size, m_depth;
        int      m_max_unsplit;                      // -1 = the upstream link's maximum
        bool     m_deny_all;
        std::map<uint64_t, Op> m_ops;                // by upstream source
        BeatQueue m_a, m_d;                          // A down, D up
        BeatQueue m_pass[NUM_CHANNELS];              // B, C, E on a TL-C link: forwarded unchanged
        ChannelState *m_in[NUM_CHANNELS] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        MsgPtr   m_a_cur;                            // the unsplit A message in progress (pass-through)
        bool     m_d_pass = false;                   // a pass-through D burst is in progress
        std::deque<MsgPtr> m_d_pending;              // reassembled responses waiting for it
    };
}}
#endif /* _TL_AGENTS_FRAGMENTER_H */
