/*
 * File  :      RamSlave.h
 * The TL-UL/UH memory slave (plan §3.5): Get, PutFullData, PutPartialData, ArithmeticData (MIN,
 * MAX, MINU, MAXU, ADD, Table 23), LogicalData (XOR, OR, AND, SWAP, Table 25), Intent -> HintAck
 * (complete for a slave with no caches inward of it: a prefetch hint may be ignored, §8, and a CBO
 * has nothing to clean or invalidate). Bursts in both directions. Data modes (plan §3.3): full (a
 * sparse byte memory by 4 KB page, atomics computed), stamp (one 64-bit value per 8-byte unit: a
 * write stores the request's stamps, a read returns the stored ones, an atomic returns the old
 * stamps and stores the request's), none (sizes only).
 *
 * Timing: a request is accepted beat by beat (a_ready HIGH while the queue has room and no
 * refresh is in progress); it executes when its last beat is accepted (§6.5: the effect is
 * visible before the response) and its response is offered `latency` cycles later (minimum 1,
 * plan §3.2), plus a uniform random jitter in [0, latency_jitter]. With fifo=1 responses leave in
 * request order (§6.3 FIFO domain); otherwise the response whose service time came first goes
 * first. A burst response holds the D channel until its last beat is accepted.
 *
 * Parameters (agent[k].<key>):
 *   latency(i) 1          service latency from the last accepted beat to the first response beat
 *   latency_jitter(i) 0   extra random latency in [0, jitter], from the agent's seeded rng
 *   latencies(vi)         per-request latency list: the k-th accepted request takes latencies[k]
 *                         (then `latency` once the list is used up); Figure 8's "3, 1"
 *   queue_depth(i) 8      requests held (accepted, not fully answered); ready LOW when full
 *   fifo(i) 0             responses in request order
 *   max_size(i)           largest size accepted (declared to the monitor, M-ALIGN-3); default 2^z-1
 *   addr_base(i), addr_size(i)   the region served (declared to the monitor, M-ALIGN-5); default: none
 *   deny(vi)              pairs base,size of denied ranges: the response is denied, data beats corrupt (§4.4), no effect
 *   refresh_period(i) 0, refresh_cycles(i) 0   every `period` cycles, a_ready and d_valid LOW for `cycles` (§5.2 example 2)
 *   level(s) UH           the capability declared for elaboration (UL or UH)
 * A corrupt write beat (§4.4) stores its data and poisons the 8-byte groups it wrote; a read of a
 * poisoned group returns its beat corrupt, until a clean write. Atomics compute on the 2^size-byte
 * little-endian integer (plan §1.3; ValueCheck uses the same reading): MIN/MAX signed, MINU/MAXU
 * unsigned, ADD modulo 2^(8*2^size); XOR/OR/AND/SWAP per byte. In stamp mode an atomic returns
 * the old stamps and stores the operand's.
 */
#ifndef _TL_AGENTS_RAMSLAVE_H
#define _TL_AGENTS_RAMSLAVE_H
#include "tilelink/Agent.h"
#include "tilelink/Monitor.h"
#include <list>
#include <map>
#include <vector>

namespace octopus { namespace tl
{
    class RamSlave : public Agent, public SlaveLimits
    {
    public:
        RamSlave(const std::string &name, const Config &params);

        Level capability(const Link &, Role) const override { return m_level; }
        void offer(uint64_t cycle) override;
        void accept(uint64_t cycle) override;
        void commit(uint64_t cycle) override;
        bool idle() const override { return m_queue.empty(); }
        // Plan §3.6: the watchdog's default bound. Latency = the slowest service time (latency +
        // jitter, the latency list's maximum, a refresh window); queue = the response beats that
        // can be ahead of one request on D (queue_depth x the largest response burst).
        ProgressHints hints() const override;

        int declaredMaxSize(const Link &l) const override;
        std::vector<AddrRegion> declaredRegions(const Link &) const override;

        // ---- the memory, for tests
        uint8_t  readByte(uint64_t address) const;
        void     writeByte(uint64_t address, uint8_t v);
        uint64_t readWord(uint64_t address, unsigned bytes) const;   // little-endian, bytes <= 8
        uint64_t readStamp(uint64_t address) const;                   // stamp mode, its 8-byte group

        // ---- counters
        uint64_t requests() const { return m_requests; }
        uint64_t responses() const { return m_responses; }
        uint64_t deniedCount() const { return m_denied; }

        // Settable after construction (tests): same meaning as the parameters.
        unsigned latency = 1, latency_jitter = 0, queue_depth = 8, refresh_period = 0, refresh_cycles = 0;
        bool fifo = false;
        bool deny_all = false;                // ErrorSlave
        std::vector<unsigned> latencies;
        std::vector<AddrRegion> deny;
        int max_size = -1;
        AddrRegion region;                    // size 0 = none declared

    protected:
        struct Pending
        {
            MsgPtr   req;
            MsgPtr   resp;                    // built when the request's last beat is accepted
            uint64_t ready_at = NEVER;        // first cycle the response may be offered
            unsigned beat = 0;                // next response beat to offer
            uint64_t order = 0;
            std::vector<uint8_t>  wdata, wmask, wcorrupt;   // the request's beats as accepted
            std::vector<uint64_t> wstamp;
        };
        Link *link() const;
        bool  refreshing(uint64_t cycle) const;
        bool  isDenied(uint64_t address, unsigned size) const;
        void  execute(Pending &p, uint64_t cycle);    // at the last accepted beat (§6.5)
        void  readBytes(uint64_t address, uint8_t *out, size_t n) const;

        Level m_level = Level::UH;
        std::map<uint64_t, std::vector<uint8_t>> m_pages;         // page number -> 4096 bytes
        std::map<uint64_t, uint64_t> m_stamps;                    // 8-byte group -> stamp
        std::map<uint64_t, bool> m_poison;                        // 8-byte group written with corrupt data
        std::list<Pending> m_queue;                               // accepted, not fully answered
        std::list<Pending>::iterator m_cur;                       // the response on D (m_queue.end(): none)
        std::list<Pending>::iterator m_in;                        // the request burst being accepted
        bool     m_have_cur = false, m_have_in = false;
        uint64_t m_requests = 0, m_responses = 0, m_denied = 0, m_order = 0;
    };
}}
#endif /* _TL_AGENTS_RAMSLAVE_H */
