/*
 * File  :      TraceMaster.h
 * TraceMaster (docs/TileLink_Plan.md §3.5): replays an Octopus trace file, one file per core
 * (`trace_C<n>.trc.shared`), parsed exactly as Octopus's CPU::readSampleFromWorkload does
 * (src/CPU.cpp): one access per line, `<hex address> <int> <R|W> <cycle>`, scanned with
 * "%llx %*d %c %lld" (the second field is ignored, any type other than 'R' is a write).
 *
 * Two modes, never mixed in one test (plan §3.5):
 *   - uncached: the TraceMaster agent itself, a TL-UL/UH master on its own link; each Load is a
 *     Get and each Store a PutFullData of `access_bytes` (default 8) at the address aligned down to
 *     that size (system 2);
 *   - cpu: the trace feeds a CacheMaster's CPU side (system 4). The design choice: the CacheMaster
 *     owns the reader (CacheMaster `trace(s)` key), because the CPU side is not a TileLink link and
 *     a configuration can only connect agents by links. `TraceReader` below is that reader; the
 *     TraceMaster agent with `mode(s)=cpu` is refused with a message pointing to CacheMaster.trace.
 *
 * Timing (`timing(s)`, both modes): `asap` (default) issues each access as soon as the window
 * allows; `trace` keeps the trace's spacing: access k is not issued before access k-1's issue +
 * (cycle_k - cycle_{k-1}), the gap capped at `max_gap(i)` (default 1000), as Octopus's CPU waits the
 * compute time between two samples.
 *
 * TraceMaster keys (agent[k].*): trace(s) (required), mode(s) uncached, access_bytes(i) 8,
 * timing(s) asap, max_gap(i) 1000, max_ops(i) 0 (0 = the whole file), outstanding(i) 1 (requests in
 * flight; each has its own source), level(s) UH.
 */
#ifndef _TL_AGENTS_TRACEMASTER_H
#define _TL_AGENTS_TRACEMASTER_H
#include "tilelink/Agent.h"
#include <cstdint>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace octopus { namespace tl
{
    // One CPU-side access (plan §3.5: the CacheMaster's CPU side).
    struct CpuOp
    {
        enum Kind : uint8_t { Load = 0, Store = 1, Evict = 2 };
        Kind     kind = Load;
        uint64_t addr = 0;
        unsigned bytes = 8;            // a power of two, the access aligned to it, inside one block
        uint64_t not_before = 0;       // earliest cycle it may be attempted
        uint64_t seq = 0;              // program order (set by the CacheMaster)
        // results
        bool     done = false, error = false;
        uint64_t t_issue = ~uint64_t(0), t_done = ~uint64_t(0);
        std::vector<uint8_t> value;    // Full data: the bytes loaded or stored
    };

    // The CPU side of a CacheMaster: a stream of accesses in program order.
    class CpuSource
    {
    public:
        virtual ~CpuSource() {}
        // The next access, or false when none is available at `cycle` (exhausted() tells whether
        // one ever will be). `last_issue` is the cycle the previous access was issued (timing).
        virtual bool next(CpuOp &op, uint64_t cycle, uint64_t last_issue, std::mt19937_64 &rng) = 0;
        virtual bool exhausted() const = 0;
    };

    // Octopus's trace format (src/CPU.cpp), read lazily.
    class TraceReader : public CpuSource
    {
    public:
        // Throws ConfigError if the file cannot be opened.
        TraceReader(const std::string &path, unsigned access_bytes, bool trace_timing, uint64_t max_gap, uint64_t max_ops);
        bool next(CpuOp &op, uint64_t cycle, uint64_t last_issue, std::mt19937_64 &rng) override;
        bool exhausted() const override { return m_eof && !m_have; }
        uint64_t lines() const { return m_lines; }
        const std::string &path() const { return m_path; }

        // One line of the format: false if it does not scan (as sscanf would leave the fields).
        static bool parseLine(const std::string &line, uint64_t &addr, bool &store, uint64_t &cycle);

    private:
        void fetch();
        std::string   m_path;
        std::ifstream m_f;
        unsigned m_bytes;
        bool     m_timing;
        uint64_t m_max_gap, m_max_ops, m_lines = 0;
        bool     m_eof = false, m_have = false;
        CpuOp    m_op;
        uint64_t m_cycle = 0, m_prev_cycle = 0;
        bool     m_first = true;
    };

    // The uncached mode: Loads and Stores as Get / PutFullData on a TL-UL/UH link.
    class TraceMaster : public Agent
    {
    public:
        TraceMaster(const std::string &name, const Config &params);

        Level capability(const Link &, Role) const override { return m_level; }
        void reset(uint64_t reset_cycles) override;
        void offer(uint64_t cycle) override;
        void accept(uint64_t cycle) override;
        void commit(uint64_t cycle) override;
        bool idle() const override;
        ProgressHints hints() const override;

        uint64_t issued() const { return m_issued; }
        uint64_t answered() const { return m_answered; }
        uint64_t denied() const { return m_denied; }

    private:
        Link *link() const;
        Level m_level = Level::UH;
        std::unique_ptr<TraceReader> m_reader;
        unsigned m_bytes = 8, m_outstanding_max = 1;
        std::vector<uint64_t> m_free;
        CpuOp    m_next;                  // the next access, read ahead (its issue time may lie ahead)
        bool     m_have_next = false;
        MsgPtr   m_cur;                   // the request being offered
        unsigned m_beat = 0;
        uint64_t m_last_issue = 0, m_issued = 0, m_answered = 0, m_denied = 0, m_outstanding = 0, m_counter = 0;
    };
}}
#endif /* _TL_AGENTS_TRACEMASTER_H */
