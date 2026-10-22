/*
 * File  :      FuzzMaster.h
 * Random legal traffic at TL-UL or TL-UH (plan §3.5): Get, PutFullData, PutPartialData and, at
 * TL-UH, ArithmeticData, LogicalData and Intent, with bursts. Deterministic from the agent's seeded
 * rng (plan §3.2). Every message it emits is legal for the monitor: opcode at the level (M-LEVEL),
 * legal param (M-PARAM), aligned and within the slave's max_size and address region (M-ALIGN),
 * derived mask, PutPartialData a random subset of the active lanes (M-MASK), corrupt LOW (M-RSV),
 * one burst at a time (M-BURST), a source unique among its outstanding requests (M-ID-1, from a
 * pool of min(2^o, sources) identifiers, released the cycle after the response's last beat).
 * Before a message's first beat is accepted it may, with probability retract_pct per cycle, lower
 * valid for that cycle or replace the message by a new one (§4.1, Figure 6 message H); never once
 * a burst is in progress.
 *
 * Parameters (agent[k].<key>):
 *   level(s)              UL or UH; traffic is limited to min(level, the link's level). Default: the link's
 *   ops(i) 1000           requests in total, then idle
 *   w_get(i) 4, w_put(i) 3, w_putpartial(i) 1, w_arith(i) 1, w_logic(i) 1, w_intent(i) 1   op mix weights
 *   addr_base(i), addr_span(i)   the address set; default the slave's declared region, else [0, 4096)
 *   align(i) 1            minimum address alignment in bytes (addresses are multiples of max(2^size, align))
 *   size_min(i) 0, size_max(i)   size range; default max: TL-UL log2(w), TL-UH min(2^z - 1, the slave's max_size, 6)
 *   atomic_size_max(i) 3  largest atomic size
 *   sources(i)            the source pool (at most 2^o); default 2^o, capped at 1024
 *   rate(i) 70            percent chance per idle cycle of starting a new request
 *   fence_every(i) 0      after every N requests, wait until all are answered (a fence, §6.5)
 *   retract_pct(i) 0      percent chance per cycle of retracting/replacing a not-yet-accepted message
 *   d_ready_pct(i) 100    percent chance per cycle of d_ready HIGH
 * Data (plan §3.3): full = random bytes; stamp = a write counter per 8-byte group; none = sizes only.
 */
#ifndef _TL_AGENTS_FUZZMASTER_H
#define _TL_AGENTS_FUZZMASTER_H
#include "tilelink/Agent.h"
#include <vector>

namespace octopus { namespace tl
{
    class FuzzMaster : public Agent
    {
    public:
        FuzzMaster(const std::string &name, const Config &params);

        Level capability(const Link &, Role) const override { return m_level_set ? m_level : Level::UH; }
        void reset(uint64_t reset_cycles) override;
        void offer(uint64_t cycle) override;
        void accept(uint64_t cycle) override;
        void commit(uint64_t cycle) override;
        bool idle() const override { return m_issued >= m_ops && m_outstanding == 0 && !m_cur; }
        ProgressHints hints() const override;

        // ---- counters (tests)
        uint64_t issued() const { return m_issued; }            // requests whose last beat was accepted
        uint64_t answered() const { return m_answered; }        // responses whose last beat was accepted
        uint64_t outstanding() const { return m_outstanding; }
        uint64_t deniedResponses() const { return m_denied; }
        uint64_t deniedDataBeats() const { return m_denied_beats; }          // beats of denied data responses
        uint64_t deniedCorruptBeats() const { return m_denied_corrupt; }     // ... of which corrupt (§4.4: all)
        uint64_t retractions() const { return m_retractions; }
        uint64_t replacements() const { return m_replacements; }
        uint64_t opsIssued(unsigned opcode) const { return opcode < 8 ? m_by_op[opcode] : 0; }

    private:
        Link  *link() const;
        MsgPtr generate();
        unsigned pickOpcode();
        void   releaseSource(uint64_t s);

        Level    m_level = Level::UH, m_traffic = Level::UH;
        bool     m_level_set = false;
        uint64_t m_ops = 1000;
        unsigned m_w_get = 4, m_w_put = 3, m_w_pp = 1, m_w_arith = 1, m_w_logic = 1, m_w_intent = 1;
        uint64_t m_addr_base = 0, m_addr_span = 0;
        bool     m_addr_set = false;
        uint64_t m_align = 1;
        int      m_size_min = 0, m_size_max = -1, m_atomic_max = 3;
        uint64_t m_sources_param = 0;
        unsigned m_rate = 70, m_fence = 0, m_retract = 0, m_dready = 100;

        std::vector<uint64_t> m_free;          // free sources
        MsgPtr   m_cur;                        // the request being offered
        unsigned m_beat = 0;
        bool     m_started = false;            // its first beat was accepted (in progress, §5.1)
        uint64_t m_issued = 0, m_answered = 0, m_outstanding = 0, m_denied = 0, m_denied_beats = 0, m_denied_corrupt = 0;
        uint64_t m_retractions = 0, m_replacements = 0, m_stamp = 0;
        uint64_t m_by_op[8] = {0};
    };
}}
#endif /* _TL_AGENTS_FUZZMASTER_H */
