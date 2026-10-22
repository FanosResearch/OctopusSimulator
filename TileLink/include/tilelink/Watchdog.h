/*
 * File  :      Watchdog.h
 * The per-message forward-progress watchdog of plan §3.6, a diagnostic for §5's rules 2 and
 * 3. Every presented beat, every burst in progress and every received request carries its
 * own timer; a timer that reaches `progress_bound` trips:
 *   (a) a beat presented (valid HIGH, control and data unchanged since it was first offered,
 *       §5.1 "present") and rejected for `bound` cycles          -> rule 3i, the receiver;
 *   (b) a burst in progress (§5.1) whose sender has neither had a beat accepted nor held the
 *       next beat stable for `bound` cycles                      -> rule 2, the sender;
 *   (c) a request received (first beat accepted) and not answered (first beat of a matching
 *       response accepted on the same link: D by source and opcode namespace, C by address,
 *       E by sink, plan §1.4) for `bound` cycles                 -> rule 3ii, the receiver.
 * "Presented" is operational: the spec's definition looks into the future (a beat retracted
 * later was never presented), the watchdog counts from the cycle the beat was first offered
 * in its current form and resets when the sender lowers valid or changes the beat. A sender
 * that alternates valid therefore never accumulates a 3i timer, and its burst trips rule 2.
 *
 * The bound is a declaration of the configuration (progress_bound(i)), or the default
 * formula of plan §3.6 computed from Agent::hints(): (slowest latency + deepest queue +
 * largest burst in beats) x number of masters. A trip means "longer than the declared bound",
 * not "non-conforming": the dump prints the bound and its formula, every unanswered request
 * and every offered beat, so that the reader can apply rule 3's exemption. There is no global
 * "no beat anywhere" timer: it would miss one starved request behind busy links. The
 * watchdog never recovers: fatal mode exits 2 after the dump; report mode counts and goes on.
 */
#ifndef _TL_WATCHDOG_H
#define _TL_WATCHDOG_H
#include "Kernel.h"
#include <functional>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace octopus { namespace tl
{
    enum class WatchdogMode : uint8_t { Fatal = 0, Report = 1 };
    // §5: the rule a trip points at, in the order ties are broken (2, 3i, 3ii).
    enum class ProgressRule : uint8_t { Rule2 = 0, Rule3i = 1, Rule3ii = 2 };
    const char *ruleName(ProgressRule r);   // "2", "3i", "3ii"

    struct WatchdogOptions
    {
        uint64_t     bound = 0;                 // 0: the default formula at elaboration
        WatchdogMode mode  = WatchdogMode::Fatal;
        std::ostream *info = nullptr;           // the bound is printed here at elaboration (null: not printed)
        std::ostream *log  = nullptr;           // report mode: one line per trip (null: std::cerr)
    };

    // One trip: the timer, the rule it points at and the agent the rule binds.
    struct Trip
    {
        ProgressRule rule;
        uint64_t     cycle;
        Link        *link;
        Channel      channel;
        Agent       *agent;       // 3i and 3ii: the receiver; 2: the sender
        MsgPtr       msg;
        uint64_t     since;       // the cycle the timer started
        uint64_t     timer;       // cycle - since
        uint64_t     bound;       // the bound in force
        unsigned     beat = 0;    // 3i: the beat index; 2: beats accepted so far
        std::string  text;        // what the timer measured
        // "WATCHDOG rule 3ii link=L1 channel=A agent=F cycle=116 bound=16 msg=Get src=0
        //  addr=0x100 size=3 since=100 timer=16 <text>"
        std::string  line() const;
    };

    class Watchdog : public Observer
    {
    public:
        explicit Watchdog(const WatchdogOptions &o = WatchdogOptions());
        const char *name() const override { return "Watchdog"; }
        void onElaborate(Kernel &k) override;
        void onCycleEnd(Kernel &k, uint64_t cycle) override;

        uint64_t           bound() const { return m_bound; }
        const std::string &formula() const { return m_formula; }   // how the bound was obtained
        // Plan §3.6: (max latency + max queue_depth over the agents' hints + the largest burst
        // any link can carry) x the number of agents whose hints say is_master (at least 1).
        static uint64_t defaultBound(const Kernel &k, std::string *formula = nullptr);

        // Report mode: every trip so far, and a callback per trip (the dump is the full text).
        const std::vector<Trip> &trips() const { return m_trips; }
        std::function<void(const Trip &, const std::string &dump)> onTrip;

        // The live state, for tests: unanswered requests and presented beats right now.
        size_t unanswered() const { return m_pending.size(); }
        size_t presented() const;
        // The full diagnostic text for a set of trips at `cycle` (the first is the verdict).
        std::string dump(const Kernel &k, uint64_t cycle, const std::vector<Trip> &past) const;

    private:
        // Per channel: the beat offered at the previous cycle end (to tell "unchanged"), the
        // current presentation and the burst in progress.
        struct ChanTimers
        {
            int      link_index = 0;
            bool     prev_valid = false;
            MsgPtr   prev_msg;
            unsigned prev_index = 0;
            MsgHeader            prev_header;
            std::vector<uint8_t> prev_mask, prev_data;
            bool                 prev_corrupt = false;
            uint64_t presented_since = NEVER;   // (a)
            bool     tripped_3i = false;
            MsgPtr   burst;                     // (b) the in-progress message
            unsigned burst_accepted = 0;
            uint64_t last_progress = NEVER;
            bool     tripped_2 = false;
        };
        // (c) a received request waiting for its response on the same link.
        struct Pending
        {
            Link    *link;
            Channel  channel;
            MsgPtr   msg;
            uint64_t received;
            bool     tripped = false;
        };
        bool sameBeat(const ChanTimers &t, const Beat &b) const;
        void remember(ChanTimers &t, const Beat &b);
        bool answers(const Pending &p, const Msg &r, Channel rc) const;

        WatchdogOptions m_opts;
        uint64_t        m_bound = 0;
        std::string     m_formula;
        std::map<const ChannelState *, ChanTimers> m_chan;
        std::vector<Pending> m_pending;   // oldest first
        std::vector<Trip>    m_trips;
    };

    // "Get src=0 addr=0x100 size=3": the header fields that identify a message in a dump.
    std::string headerText(const MsgHeader &h);
}}
#endif /* _TL_WATCHDOG_H */
