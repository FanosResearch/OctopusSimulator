/*
 * File  :      CacheMaster.h
 * CacheMaster (docs/TileLink_Plan.md §3.5): an L1 cache with a TL-C master port, executing the L1
 * table of the policy engine (plan §3.7; default tables/TLC_MESI_L1.csv, justified cell by cell in
 * docs/TileLink_TLC_Tables.md §2). Sets x ways with LRU replacement, block_bytes per line.
 *
 * The engine around a cell (TLC_Tables §1.2 and the T6 additions listed in the README):
 *   - per-channel receive queues for B and D, always ready (outside reset): a Stall cell parks the
 *     message in its queue and never withholds ready (§1.2 rule 2); each cycle the first message of
 *     each queue (D first, then B: priority) whose cell is not Stall is executed, so a parked Probe
 *     (a Release outstanding, the _r states) never blocks the D that unblocks it (plan §5.2);
 *   - the CPU side: a window of `outstanding` accesses in program order; an access is attempted
 *     only when no older access of the window is to the same block; a cell leaving a stable state
 *     on a CPU event parks the access on the line (§1.2 rule 3) and the Grant cell's Hit or Error
 *     answers it; one CPU cell executes per cycle;
 *   - a miss that needs a way when the set is full raises a Replacement on the LRU line whose
 *     Replacement cell is executable (S, E, M), and waits; an `Evict` access raises the Replacement
 *     of its block (a no-op when the line is absent or already being released);
 *   - a GrantData that is not denied fills the line before the cell's actions run (the data
 *     handler's fill); ProbeAckData and ReleaseData carry the line;
 *   - ID pools: A sources (Acquire) and C sources (Release, its own namespace, §6.4) from 2^o
 *     each, released when the Grant / ReleaseAck is received; a ProbeAck copies the Probe's
 *     source; a cell that needs a source waits until one is free;
 *   - every performed Load and Store (the Hit) is reported to CoherenceCheck::of(kernel()), if the
 *     kernel has one; an Error (denied Grant) completes the access as an access error.
 *
 * Keys (agent[k].*):
 *   table(s)         the L1 table; default TLC_MESI_L1.csv found by findTable() (TILELINK_TABLES
 *                    environment variable, then next to the executable, then the source tree)
 *   block_bytes(i) 64, sets(i) 64, ways(i) 4
 *   cpu(s)           random (default) | trace (implied by trace(s)) | script (pushOp, tests) | none
 *   outstanding(i) 4 accesses in the window
 *   random:  ops(i) 1000, addr_base(i) 0, addr_span(i) 4096, store_pct(i) 30, evict_pct(i) 0,
 *            access_bytes(i) 8, rate(i) 100 (% chance per cycle of a new access)
 *   trace:   trace(s) path (Octopus format, TraceMaster.h), timing(s) asap|trace, max_gap(i) 1000,
 *            max_ops(i) 0, access_bytes(i) 8
 *   debug(i) 0       print every executed cell (Transition::line) to stdout
 */
#ifndef _TL_AGENTS_CACHEMASTER_H
#define _TL_AGENTS_CACHEMASTER_H
#include "tilelink/Agent.h"
#include "tilelink/agents/TraceMaster.h"
#include "tilelink/coh/Policy.h"
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace octopus { namespace tl
{
    namespace coh { struct BlockData; }

    // The default table: <name> searched in $TILELINK_TABLES, <exe dir>/tables, <exe dir>/../tables,
    // <exe dir>/../../tables, <exe dir>/../TileLink/tables, ./tables, ./TileLink/tables, then the
    // source tree's TileLink/tables (from __FILE__). ConfigError listing the places when absent.
    std::string findTable(const std::string &name);

    class CacheMaster : public Agent
    {
    public:
        CacheMaster(const std::string &name, const Config &params);
        ~CacheMaster();

        Level capability(const Link &, Role) const override { return Level::C; }
        void reset(uint64_t reset_cycles) override;
        void offer(uint64_t cycle) override;
        void accept(uint64_t cycle) override;
        void commit(uint64_t cycle) override;
        bool idle() const override;
        // Plan §3.6: is_master; latency = a Probe answered after at most one ReleaseAck round trip,
        // which the manager may hold until a GrantAck: 4 hops, each bounded by the slowest
        // non-cache agent's declared latency.
        ProgressHints hints() const override;

        // ---- the CPU side
        void setCpu(std::unique_ptr<CpuSource> src);        // replaces the configured source
        void pushOp(const CpuOp &op);                       // cpu(s)=script: appended in program order
        const std::vector<CpuOp> &finished() const { return m_finished; }   // completed accesses (script mode keeps them)
        bool cpuDone() const;                               // source exhausted and window empty

        // ---- inspection (tests)
        const coh::L1Policy &policy() const { return *m_policy; }
        std::string lineState(uint64_t address) const;     // the table state name; "I" when absent
        bool lineValue(uint64_t address, unsigned bytes, std::vector<uint8_t> &out) const;   // Full data
        unsigned blockBytes() const { return m_bb; }
        std::function<void(const coh::Transition &)> onTransition;

        struct Counters
        {
            uint64_t loads = 0, stores = 0, errors = 0, evicts = 0, poisoned_loads = 0;
            uint64_t acquires = 0, releases = 0, probes = 0, grants = 0, denied = 0, replacements = 0;
            uint64_t stalls = 0;     // Stall decodes (parked messages and accesses, per attempt)
            uint64_t states_entered[64] = {0};   // per table state, transitions into it
        };
        const Counters &counters() const { return m_c; }

    private:
        struct Line;
        struct OutQ { std::deque<MsgPtr> q; unsigned beat = 0; };

        Link *link() const;
        uint64_t blockOf(uint64_t a) const { return a & ~uint64_t(m_bb - 1); }
        unsigned setOf(uint64_t block) const { return unsigned((block / m_bb) % m_sets); }
        unsigned waysUsed(unsigned set) const;
        int  stateOf(uint64_t block) const;
        Line *line(uint64_t block);
        // Can the cell run now (sources, a way)? `need_way` is set when only a way is missing.
        bool resources(int state, int event, bool line_present, bool &need_way) const;
        void execute(uint64_t block, int event, const MsgPtr &trigger, CpuOp *cpu, uint64_t cycle);
        void perform(Line &l, CpuOp &op, uint64_t cycle);
        CpuOp *findOp(uint64_t seq);
        bool raiseReplacement(unsigned set, uint64_t cycle);
        bool processD(uint64_t cycle);
        bool processB(uint64_t cycle);
        bool processCpu(uint64_t cycle);
        void emit(const MsgHeader &h, const Line *l, uint64_t cycle);
        void report(const coh::Transition &t);
        void retire(uint64_t cycle);

        std::unique_ptr<coh::L1Policy> m_policy;
        unsigned m_bb = 64, m_lg = 6, m_sets = 64, m_ways = 4, m_outstanding = 4;
        bool m_debug = false, m_keep = false;
        std::unique_ptr<CpuSource> m_cpu;
        std::deque<CpuOp> m_script;          // cpu(s)=script
        bool m_script_mode = false;
        std::deque<CpuOp> m_window;
        std::vector<CpuOp> m_finished;
        uint64_t m_seq = 0, m_last_issue = 0, m_store_counter = 0, m_use = 0;

        std::map<uint64_t, std::unique_ptr<Line>> m_lines;    // block -> line (absent = I)
        std::deque<MsgPtr> m_rxB, m_rxD;
        OutQ m_out[NUM_CHANNELS];
        std::vector<uint64_t> m_freeA, m_freeC;
        std::map<uint64_t, uint64_t> m_srcA, m_srcC;          // outstanding source -> block
        std::map<const Msg *, int> m_stall_seen;              // parked Probe -> state+1 it was reported in
        Counters m_c;
    };
}}
#endif /* _TL_AGENTS_CACHEMASTER_H */
