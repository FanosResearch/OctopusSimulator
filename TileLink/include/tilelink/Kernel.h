/*
 * File  :      Kernel.h
 * The kernel: owns the agents and links, runs the three-phase cycle of plan §3.2, handles
 * reset (§3.2.2), seeds one RNG per agent, records the transcript and the statistics.
 *
 * One cycle t (step()):
 *   0. every channel: valid and ready LOW (the burst lock survives);
 *   1. offer:  every agent, in insertion order, computes its offers from its state at the
 *              start of t;  during reset any valid HIGH is a violation (§3.2.2);
 *   2. accept: every agent sets ready; it may look at the beat offered on the same link;
 *   3. commit: every channel with valid && ready transfers its beat (transcript, stats, the
 *              message timestamps), then every agent commits.
 * No combinational pass-through: an offer at t can only carry what was committed at t-1, so
 * a beat accepted at t is re-offered by its receiver at t+1 at the earliest, and a response is
 * never in the request's own cycle (plan §8 decision 2). "valid must not depend on ready" and
 * the priority dependency rules of §4.1 hold by construction of the phase order.
 */
#ifndef _TL_KERNEL_H
#define _TL_KERNEL_H
#include "Agent.h"
#include "Config.h"
#include "Link.h"
#include "Stats.h"
#include "Transcript.h"
#include <memory>
#include <string>
#include <vector>

namespace octopus { namespace tl
{
    struct KernelOptions
    {
        uint64_t reset_cycles = 100;   // §3.2.2: valid LOW for at least 100 cycles; 0 inside Octopus
        uint64_t seed = 1;
        DataMode data_mode = DataMode::None;
    };

    // A system-level checker (plan §3.6: GraphCheck, Watchdog, later CoherenceCheck) hooked to
    // the kernel. It only observes: the agents, links and channels are read through the
    // kernel, never driven. onElaborate runs inside elaborate(), after the link and capability
    // checks and before the agents are reset; a ConfigError thrown there is fatal (exit 2).
    // onCycleEnd runs at the end of every step(), after the agents' commit, while the
    // channels still show the cycle's valid, ready and accepted beat.
    class Observer
    {
    public:
        virtual ~Observer() {}
        virtual const char *name() const = 0;
        virtual void onElaborate(Kernel &) {}
        virtual void onCycleEnd(Kernel &, uint64_t cycle) { (void)cycle; }
    };

    class Kernel
    {
    public:
        explicit Kernel(const KernelOptions &opts = KernelOptions());
        ~Kernel();

        // ---- construction (before the first step)
        Agent &add(std::unique_ptr<Agent> a);                 // evaluation order = insertion order
        Link  &connect(const std::string &name, const LinkParams &p, Agent &master, Agent &slave);
        // Plan §4: builds agents (`agent[k].type/name`, `agent[k].decoupled_groups`) and links
        // (`link[k].master/slave/w/a/z/o/i/level/name`) from a config; reads reset_cycles,
        // seed and data_mode if present; registers the checkers of plan §3.6 (`graph_check(i)`
        // default 1; `watchdog(i)` default 1 with `progress_bound(i)` and `watchdog_mode(s)`).
        void build(const Config &cfg, const AgentRegistry &reg);
        // Validates params and capabilities, then runs every observer's onElaborate; fatal on
        // the first error. run()/step() call it once.
        void elaborate();
        // Observers are called in registration order. The non-owning form is for an observer
        // the caller keeps (a test); the owning form for one built by build().
        void addObserver(Observer *o);
        void addObserver(std::unique_ptr<Observer> o);
        const std::vector<Observer *> &observers() const { return m_observers; }
        Observer *observer(const std::string &name) const;   // the first one with that name, or null

        // ---- running
        void     step();
        void     run(uint64_t cycles);
        uint64_t runUntilIdle(uint64_t max_cycles);           // returns the cycles stepped (compatibility: runToDrain(...).cycles)
        // How a bounded run ended (docs/TileLink_Review_Action_Plan.md problem 6): drained, or the cycle limit
        // reached with work left. Drained = reset complete, every agent idle (including work it will start
        // later), no channel valid or with a burst in progress; checked after every step, so a run that
        // drains on its last allowed cycle is Idle. A zero budget steps nothing and reports the state as it
        // is (elaborating first).
        struct RunResult
        {
            enum class Stop { Idle, CycleLimit };
            uint64_t cycles = 0;
            Stop     stop = Stop::CycleLimit;
            bool     drained() const { return stop == Stop::Idle; }
        };
        RunResult runToDrain(uint64_t max_cycles);
        bool     drained();                                    // the predicate above, now
        std::vector<std::string> busy();                       // diagnostics: the agents not idle, the links with traffic
        uint64_t cycle() const { return m_cycle; }
        bool     inReset() const { return m_cycle < m_opts.reset_cycles; }
        const KernelOptions &options() const { return m_opts; }

        // ---- outputs
        Transcript *transcript() const { return m_transcript.get(); }
        void enableTranscript(const std::string &path);
        void enableTranscript(std::ostream &out);
        Stats &stats() { return m_stats; }
        void   finishStats();                                  // copies the channel counters in
        void   writeStats(const std::string &path);            // finishStats + write

        // ---- lookup
        uint64_t newMsgId() { return ++m_next_id; }
        Agent *agent(const std::string &name) const;
        Link  *link(const std::string &name) const;
        const std::vector<std::unique_ptr<Agent>> &agents() const { return m_agents; }
        const std::vector<std::unique_ptr<Link>> &links() const { return m_links; }

    private:
        void checkReset();

        KernelOptions m_opts;
        std::vector<std::unique_ptr<Agent>> m_agents;
        std::vector<std::unique_ptr<Link>> m_links;
        std::vector<Observer *> m_observers;                  // call order
        std::vector<std::unique_ptr<Observer>> m_owned_observers;
        std::unique_ptr<Transcript> m_transcript;
        Stats m_stats;
        uint64_t m_cycle = 0, m_next_id = 0;
        bool m_elaborated = false;
    };
}}
#endif /* _TL_KERNEL_H */
