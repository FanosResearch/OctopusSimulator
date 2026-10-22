/*
 * File  :      Monitor.h
 * The per-link conformance monitor (plan §3.6; docs/TileLink_Monitor_Rules.md, "the catalogue").
 * One Monitor per link, attached through the ChannelState hooks (onOffer, onAccept) and installed
 * on onViolation so that the core's burst-lock and reset reports are routed through the monitor
 * (report mode counts them instead of exiting). Every violation carries the rule id, the spec §,
 * the link, the cycle, the channel, the message header and the catalogue's message text:
 *
 *   <rule-id> <spec §> link=<name> cycle=<n> ch=<A..E> msg=<opcode>[param=<p> size=<s> src=<o>
 *   sink=<i> addr=<hex> beat=<k>]: <text>
 *
 * Modes: fatal (exit 2 on the first violation, through tl::fatal) and report (count per rule
 * id, continue). The counters are readable by tests; summary() is the end-of-run line
 * "monitor <link>: N violations (<rule>: n, ...)".
 *
 * Rule table (catalogue id -> status in this step, T2). "impl" = implemented here; "T2" = exercised
 * by TL-NEG/TL-WAVE in this step; "T6" = implemented from the same per-block state but exercised
 * only by the TL-C mutants of T6.
 *
 *   M-RESET-1 impl T2    M-RESET-2 impl (checkResetCycles, applied by tlsim at elaboration) T2
 *   M-LEVEL-1 impl T2    M-LEVEL-2 impl T2    M-LEVEL-3 impl T6    M-LEVEL-4 impl T2    M-LEVEL-5 impl T2
 *   M-PARAM-1 impl T2    M-PARAM-2 impl T2    M-PARAM-3 impl T2    M-PARAM-4 impl T2 (A) / T6 (B)
 *   M-PARAM-5 impl T2    M-PARAM-6 impl T6    M-PARAM-7 impl T6    M-PARAM-8 impl T6
 *   M-PARAM-9 impl T6    M-PARAM-10 impl T6   M-PARAM-11 impl T6
 *   M-ALIGN-1 impl T2    M-ALIGN-2 impl T2    M-ALIGN-3 impl T2    M-ALIGN-4 impl T2
 *   M-ALIGN-5 impl T2    M-ALIGN-6 impl T2    M-ALIGN-7 impl T6
 *   M-MASK-1 impl T2     M-MASK-2 impl T2     M-MASK-3 impl T2
 *   M-RSV-1 impl T2      M-RSV-2 impl T6
 *   M-ERR-1 impl T2      M-ERR-2 impl T2      M-ERR-3 impl (state rule, no report of its own) T6
 *   M-BURST-1 impl T2    M-BURST-2 impl T2    M-BURST-3 impl T2    M-BURST-4 impl T2    M-BURST-5 impl T2
 *   M-RESP-1 impl T2     M-RESP-2 impl T2     M-RESP-3 impl T6     M-RESP-4 impl T6
 *   M-RESP-5 impl T6     M-RESP-6 impl T2     M-RESP-7 impl T2
 *   M-ID-1 impl T2       M-ID-2 impl T6       M-ID-3 impl T6       M-ID-4 impl T6
 *   M-CONC-1..11 impl T6
 *   M-PERM-1..3 impl T6
 *
 * Evaluation order within one beat (catalogue §0.2): M-RESET, M-LEVEL, M-PARAM, M-ALIGN, M-MASK,
 * M-RSV, M-ERR, M-BURST at offer; M-PARAM-11, M-RESP, M-ID, M-CONC, M-PERM at the first beat's
 * acceptance; M-BURST-4, M-BURST-5, M-RESP-7 at finish().
 *
 * The monitor also pairs requests with responses and feeds Stats::recordOperation with the
 * request-first-beat-offered to response-last-beat-accepted latency (the pairing lives only here).
 *
 * Lifetime: the monitor installs hooks on the link's channels and removes them in its destructor;
 * destroy monitors before the kernel that owns the link.
 */
#ifndef _TL_MONITOR_H
#define _TL_MONITOR_H
#include "Kernel.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace octopus { namespace tl
{
    // One region of a slave's address map (catalogue §0.1, M-ALIGN-5).
    struct AddrRegion
    {
        uint64_t base = 0, size = 0;
        bool contains(uint64_t address, uint64_t bytes) const
        {
            return address >= base && bytes <= size && address - base <= size - bytes;
        }
    };

    // What a slave declares for the monitor of a link it serves (catalogue §0.1: the slave's
    // max_size and its address regions). Implemented by RamSlave and ErrorSlave; Monitor::attachAll
    // discovers it with dynamic_cast, so the core Agent class needs no change.
    class SlaveLimits
    {
    public:
        virtual ~SlaveLimits() {}
        virtual int declaredMaxSize(const Link &) const { return -1; }                      // -1 = the link's 2^z - 1
        virtual std::vector<AddrRegion> declaredRegions(const Link &) const { return {}; }   // empty = no map
    };

    struct MonitorOptions
    {
        enum Mode { Fatal = 0, Report = 1 };
        Mode     mode = Fatal;
        unsigned block_bytes = 64;               // M-CONC, M-PERM
        bool     allow_acquire_under_acquire = false;   // M-CONC-1
        bool     c_ordered = true;               // M-CONC-11
        int      max_size = -1;                  // the slave's declared max_size; -1 = link maxSize
        std::vector<AddrRegion> regions;         // the slave's address map; empty = M-ALIGN-5 skipped
        bool     print = true;                   // print every violation to `out`
        std::ostream *out = &std::cerr;
        static bool parseMode(const std::string &s, Mode &m);
        static const char *modeName(Mode m);
    };

    struct Violation
    {
        std::string rule;       // "M-BURST-3"
        std::string section;    // "§4.1"
        std::string link;
        uint64_t    cycle = 0;
        Channel     ch = Channel::A;
        std::string msg;        // "Get[param=0 size=3 src=0 sink=0 addr=0x100 beat=0]"
        std::string text;       // the catalogue's message
        std::string line() const;
    };

    class Monitor
    {
    public:
        Monitor(Kernel &k, Link &l, const MonitorOptions &o = MonitorOptions());
        ~Monitor();

        // End-of-run rules (M-BURST-4, M-BURST-5, M-RESP-7); end_cycle = kernel.cycle() after
        // the run. Idempotent.
        void finish(uint64_t end_cycle);

        uint64_t violations() const { return m_log.size(); }
        uint64_t count(const std::string &rule) const;
        const std::map<std::string, uint64_t> &byRule() const { return m_counts; }
        const std::vector<Violation> &log() const { return m_log; }
        std::string summary() const;   // "monitor L0: 2 violations (M-ALIGN-1: 1, M-RESP-7: 1)"
        const Link &link() const { return m_l; }
        const MonitorOptions &options() const { return m_o; }

        // One monitor per link of the kernel; max_size and regions come from the slave when it
        // implements SlaveLimits (unless `o` sets them). Destroy before the kernel.
        static std::vector<std::unique_ptr<Monitor>> attachAll(Kernel &k, const MonitorOptions &o = MonitorOptions());
        static void finishAll(std::vector<std::unique_ptr<Monitor>> &ms, uint64_t end_cycle);
        static uint64_t totalViolations(const std::vector<std::unique_ptr<Monitor>> &ms);

        // Permission the monitor tracks for a block (TL-C, M-PERM): 'N', 'B' or 'T'.
        char perm(uint64_t address) const;

        // M-LEVEL-4 (§3.4-§3.7, Table 1): a beat driven on B, C or E of a TL-UL/UH link. The link
        // has no ChannelState for it, so no hook can see it; an agent that models that wire (the
        // test fixture ScriptedMaster) reports the offer here. Runtime backstop of the
        // elaboration capability check.
        void offerOnMissingChannel(Channel ch, const Msg &m, unsigned beat, uint64_t cycle);

        // M-RESET-2 (§3.2.2, plan §3.2): standalone, reset_cycles >= 100. Returns "" when legal,
        // else the violation line; the driver refuses the configuration at elaboration (exit 2).
        static std::string checkResetCycles(uint64_t reset_cycles, bool standalone);

    private:
        // ---- per-link state (catalogue §0.3)
        struct Out                 // an outstanding request (outA, outB, outC, grants)
        {
            Channel  ch = Channel::A;
            uint8_t  opcode = 0, param = 0, size = 0;
            uint64_t source = 0, sink = 0, address = 0;
            bool     denied = false;
            uint64_t msg_id = 0;
            uint64_t t_accept = 0, t_offer = 0;
            bool     resp_offered = false;   // a response beat 0 was offered (M-BURST-5 vs M-RESP-7)
        };
        struct Pend                // per-block pending sets (TL-C, M-CONC)
        {
            std::map<uint64_t, uint8_t> acq, acc, rel, probe;   // source -> opcode
            std::map<uint64_t, uint8_t> gack;                   // sink -> opcode
            bool empty() const { return acq.empty() && acc.empty() && rel.empty() && probe.empty() && gack.empty(); }
        };
        struct ChanView
        {
            // the burst lock as the monitor sees it (M-BURST)
            MsgPtr    lock;
            MsgHeader lock_hdr;
            unsigned  next = 0, total = 0;
            // the last offer on the channel (M-BURST-5 retraction tracking)
            MsgPtr    off_msg;
            unsigned  off_beat = 0;
            uint64_t  off_cycle = 0;
            bool      off_valid = false, off_accepted = false;
            // the response burst in progress: which outstanding entry it completes
            bool      resp_active = false;
            Out       resp_entry;
            int       resp_table = -1;   // 0 outA, 1 outC, 2 outB, 3 grants
        };

        // ---- hooks
        void onOffer(const ChannelState &c, const Beat &b, uint64_t cycle);
        void onAccept(const ChannelState &c, const Beat &b, uint64_t cycle);
        void onCoreViolation(const ChannelState &c, const std::string &what, uint64_t cycle);

        // ---- rule groups
        void checkStatic(Channel ch, const Msg &m, unsigned beat, uint64_t cycle);   // offer: LEVEL..ERR
        void checkBurstOffer(Channel ch, const Msg &m, unsigned beat, uint64_t cycle);
        void acceptRequestFirst(Channel ch, const Msg &m, uint64_t cycle);
        void acceptResponseFirst(Channel ch, const Msg &m, uint64_t cycle);
        void acceptLast(Channel ch, const Msg &m, uint64_t cycle);
        void trackRetraction(Channel ch, const MsgPtr &m, unsigned beat, uint64_t cycle);

        // ---- helpers
        bool report(const std::string &rule, const std::string &section, Channel ch, const Msg *m,
                    unsigned beat, uint64_t cycle, const std::string &text);
        std::string msgText(const Msg *m, unsigned beat) const;
        std::vector<uint64_t> blockSet(uint64_t address, unsigned size) const;
        Pend &pend(uint64_t block) { return m_pend[block]; }
        char &permOf(uint64_t block);
        static bool isResponseOpcode(Channel ch, unsigned op);
        // A message's identity for the per-message tables: Msg::id, or its address if unset.
        static uint64_t key(const Msg &m) { return m.id ? m.id : uint64_t(reinterpret_cast<uintptr_t>(&m)); }
        Out *findA(uint64_t source);
        Out *findC(uint64_t source);
        Out *findGrant(uint64_t sink);
        void eraseEntry(int table, const Out &e);

        Kernel        &m_k;
        Link          &m_l;
        MonitorOptions m_o;
        LinkParams     m_p;
        unsigned       m_max_size;
        ChanView       m_cv[NUM_CHANNELS];
        std::map<uint64_t, std::deque<Out>> m_outA, m_outC, m_grants;   // source / source / sink
        std::map<uint64_t, std::deque<Out>> m_outB;                     // address -> entries (source inside)
        std::map<uint64_t, std::pair<Out, uint64_t>> m_retracted;       // msg key -> (what, cycle retracted)
        std::map<uint64_t, uint64_t> m_first_offer;                     // msg key -> cycle of beat 0 offer here
        std::map<uint64_t, char> m_perm;                                // block -> N/B/T
        std::map<uint64_t, Pend> m_pend;
        std::set<std::tuple<std::string, uint64_t, unsigned, int>> m_reported;   // (rule, msg key, beat, ch)
        // the hooks found on the channels at attach time (another observer's, e.g. ValueCheck):
        // called first, restored by the destructor
        std::function<void(const ChannelState &, const Beat &, uint64_t)> m_prev_offer[NUM_CHANNELS], m_prev_accept[NUM_CHANNELS];
        std::function<void(const ChannelState &, const std::string &, uint64_t)> m_prev_violation[NUM_CHANNELS];
        std::vector<Violation> m_log;
        std::map<std::string, uint64_t> m_counts;
        bool m_finished = false;
    };
}}
#endif /* _TL_MONITOR_H */
