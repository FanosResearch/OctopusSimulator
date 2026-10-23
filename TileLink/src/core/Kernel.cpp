/*
 * File  :      Kernel.cpp
 * The three-phase cycle (plan §3.2), elaboration (plan §3.4), reset (§3.2.2) and the system
 * builder (plan §4).
 */
#include "tilelink/Kernel.h"
#include "tilelink/GraphCheck.h"
#include "tilelink/Watchdog.h"
#include "tilelink/CoherenceCheck.h"
#include <iostream>

namespace octopus { namespace tl
{
    Kernel::Kernel(const KernelOptions &opts) : m_opts(opts) {}
    Kernel::~Kernel() {}

    void Kernel::addObserver(Observer *o)
    {
        if (m_elaborated) fatal(std::string("observer ") + o->name() + " added after elaboration");
        m_observers.push_back(o);
    }
    void Kernel::addObserver(std::unique_ptr<Observer> o)
    {
        addObserver(o.get());
        m_owned_observers.push_back(std::move(o));
    }
    Observer *Kernel::observer(const std::string &name) const
    {
        for (auto *o : m_observers) if (name == o->name()) return o;
        return nullptr;
    }

    // "L1+L2;L3" -> {{L1, L2}, {L3}}; spaces ignored, empty groups dropped.
    static std::vector<std::vector<std::string>> parseGroups(const std::string &s)
    {
        std::vector<std::vector<std::string>> groups;
        std::vector<std::string> cur;
        std::string tok;
        auto flushTok = [&]() { if (!tok.empty()) cur.push_back(tok); tok.clear(); };
        auto flushGroup = [&]() { flushTok(); if (!cur.empty()) groups.push_back(cur); cur.clear(); };
        for (char c : s)
        {
            if (c == ' ' || c == '\t') continue;
            if (c == '+') flushTok();
            else if (c == ';') flushGroup();
            else tok += c;
        }
        flushGroup();
        return groups;
    }

    Agent &Kernel::add(std::unique_ptr<Agent> a)
    {
        if (m_elaborated) fatal("agent " + a->name() + " added after elaboration");
        if (agent(a->name())) fatal("duplicate agent name " + a->name());
        a->attachKernel(this, int(m_agents.size()), m_opts.seed);
        m_agents.push_back(std::move(a));
        return *m_agents.back();
    }

    Link &Kernel::connect(const std::string &name, const LinkParams &p, Agent &master, Agent &slave)
    {
        if (m_elaborated) fatal("link " + name + " added after elaboration");
        if (link(name)) fatal("duplicate link name " + name);
        m_links.emplace_back(new Link(name, p, &master, &slave));
        Link &l = *m_links.back();
        master.attachLink(&l, Role::Master);
        slave.attachLink(&l, Role::Slave);
        return l;
    }

    Agent *Kernel::agent(const std::string &name) const
    {
        for (auto &a : m_agents) if (a->name() == name) return a.get();
        return nullptr;
    }
    Link *Kernel::link(const std::string &name) const
    {
        for (auto &l : m_links) if (l->name() == name) return l.get();
        return nullptr;
    }

    void Kernel::build(const Config &cfg, const AgentRegistry &reg)
    {
        try
        {
            if (cfg.has("reset_cycles")) m_opts.reset_cycles = uint64_t(cfg.get<int>("reset_cycles"));
            if (cfg.has("seed")) m_opts.seed = uint64_t(cfg.get<int>("seed"));
            if (cfg.has("data_mode") && !parseDataMode(cfg.get<std::string>("data_mode"), m_opts.data_mode))
                throw ConfigError("data_mode must be none, stamp or full, got " + cfg.get<std::string>("data_mode"));
            for (int k : cfg.instances("agent"))
            {
                Config a = cfg.sub("agent", k);
                std::string name = a.get<std::string>("name", "agent" + std::to_string(k));
                Agent &ag = add(reg.make(a.get<std::string>("type"), name, a));
                // §5.1: the links this agent drives as independent agents (GraphCheck).
                if (a.has("decoupled_groups")) ag.setDecoupledGroups(parseGroups(a.get<std::string>("decoupled_groups")));
            }
            for (int k : cfg.instances("link"))
            {
                Config l = cfg.sub("link", k);
                std::string name = l.get<std::string>("name", "link" + std::to_string(k));
                LinkParams p;
                p.w = unsigned(l.get<int>("w", int(p.w)));
                p.a = unsigned(l.get<int>("a", int(p.a)));
                p.z = unsigned(l.get<int>("z", int(p.z)));
                p.o = unsigned(l.get<int>("o", int(p.o)));
                p.i = unsigned(l.get<int>("i", int(p.i)));
                std::string level = l.get<std::string>("level", "UL");
                if (!parseLevel(level, p.level)) throw ConfigError("link " + name + ": level must be UL, UH or C, got " + level);
                Agent *m = agent(l.get<std::string>("master"));
                Agent *s = agent(l.get<std::string>("slave"));
                if (!m) throw ConfigError("link " + name + ": unknown master agent " + l.get<std::string>("master"));
                if (!s) throw ConfigError("link " + name + ": unknown slave agent " + l.get<std::string>("slave"));
                if (m == s) throw ConfigError("link " + name + ": master and slave are the same agent");
                connect(name, p, *m, *s);
            }
            // Plan §3.6, the system-level checkers. graph_check(i): rule 1 at elaboration
            // (default 1). watchdog(i) (default 1): the per-message timers, with
            // progress_bound(i) (absent: the default formula; 0: watchdog off) and
            // watchdog_mode(s) fatal|report.
            if (cfg.get<int>("graph_check", 1) != 0) addObserver(std::unique_ptr<Observer>(new GraphCheck()));
            int bound = cfg.get<int>("progress_bound", -1);
            if (cfg.get<int>("watchdog", 1) != 0 && bound != 0)
            {
                WatchdogOptions wo;
                if (bound < 0) wo.bound = 0;   // computed from the agents' hints at elaboration
                else wo.bound = uint64_t(bound);
                std::string mode = cfg.get<std::string>("watchdog_mode", "fatal");
                if (mode == "report") wo.mode = WatchdogMode::Report;
                else if (mode != "fatal") throw ConfigError("watchdog_mode must be fatal or report, got " + mode);
                wo.info = &std::cout;
                addObserver(std::unique_ptr<Observer>(new Watchdog(wo)));
            }
            // coherence_check(i) (default 1): SWMR and the data-value invariant on every TL-C link
            // whose master is a leaf (plan §3.6); block_bytes(i) its block size. A system with no
            // TL-C link attaches nothing.
            if (cfg.get<int>("coherence_check", 1) != 0)
                addObserver(std::unique_ptr<Observer>(new CoherenceCheck(unsigned(cfg.get<int>("block_bytes", 64)))));
        }
        catch (ConfigError &e) { fatal(e.what()); }
    }

    void Kernel::elaborate()
    {
        if (m_elaborated) return;
        try
        {
            for (auto &l : m_links)
            {
                std::string err = l->params().validate();
                if (!err.empty()) throw ConfigError("link " + l->name() + ": " + err);
                // Plan §3.4: each end's declared capability against the link's level.
                auto check = [&](Agent *ag, Role r)
                {
                    Level cap = ag->capability(*l, r);
                    if (static_cast<int>(cap) < static_cast<int>(l->level()))
                        throw ConfigError("link " + l->name() + ": " + ag->name() + " (" + roleName(r) + ") supports TL-" +
                                          levelName(cap) + " but the link is TL-" + levelName(l->level()));
                };
                check(l->master(), Role::Master);
                check(l->slave(), Role::Slave);
            }
            // Plan §3.6: the elaboration-time checkers (GraphCheck's rule 1; the watchdog's
            // bound), in registration order; a refused configuration is fatal.
            for (auto *o : m_observers) o->onElaborate(*this);
        }
        catch (ConfigError &e) { fatal(e.what()); }
        for (auto &a : m_agents) a->reset(m_opts.reset_cycles);
        m_elaborated = true;
    }

    void Kernel::enableTranscript(const std::string &path) { m_transcript.reset(new Transcript(path)); }
    void Kernel::enableTranscript(std::ostream &out) { m_transcript.reset(new Transcript(out)); }

    void Kernel::checkReset()
    {
        // §3.2.2: a_valid, c_valid, e_valid (master) and b_valid, d_valid (slave) LOW while reset
        // is asserted. Reported through the channel's violation hook.
        for (auto &l : m_links)
            l->forEachChannel([&](ChannelState &c)
            {
                if (c.valid())
                {
                    std::string what = "valid HIGH during reset (cycle " + std::to_string(m_cycle) + " < reset_cycles " +
                                       std::to_string(m_opts.reset_cycles) + ") (§3.2.2)";
                    if (c.onViolation) c.onViolation(c, what, m_cycle);
                    else fatal(c.name() + " cycle " + std::to_string(m_cycle) + ": " + what);
                }
            });
    }

    void Kernel::step()
    {
        if (!m_elaborated) elaborate();
        const uint64_t t = m_cycle;
        // 0. the wires start the cycle LOW
        for (auto &l : m_links) l->forEachChannel([&](ChannelState &c) { c.beginCycle(t); });
        // 1. offer: senders, from their state at the start of t
        for (auto &a : m_agents) a->offer(t);
        if (inReset()) checkReset();
        // 2. accept: receivers, may look at the offered beat of the same link (§4.1)
        for (auto &a : m_agents) a->accept(t);
        // 3. commit: valid && ready transfers; then the agents update
        for (auto &l : m_links)
            l->forEachChannel([&](ChannelState &c)
            {
                if (!c.commit(t)) return;
                const Beat &b = c.accepted();
                if (m_transcript) m_transcript->record(t, l->name(), c.id(), *b.msg, b.index);
                if (b.last()) m_stats.recordMessage(l->name(), c.id(), b.msg->opcode, c.lastLatency());
            });
        for (auto &a : m_agents) a->commit(t);
        // 4. the observers see the cycle as it ended: valid, ready and accepted() still hold
        for (auto *o : m_observers) o->onCycleEnd(*this, t);
        m_cycle++;
    }

    void Kernel::run(uint64_t cycles)
    {
        for (uint64_t i = 0; i < cycles; i++) step();
    }

    uint64_t Kernel::runUntilIdle(uint64_t max_cycles) { return runToDrain(max_cycles).cycles; }

    bool Kernel::drained()
    {
        if (!m_elaborated) elaborate();
        if (inReset()) return false;
        for (auto &l : m_links)
        {
            bool busy = false;
            l->forEachChannel([&](ChannelState &c) { if (c.valid() || c.inProgress()) busy = true; });
            if (busy) return false;
        }
        for (auto &a : m_agents) if (!a->idle()) return false;
        return true;
    }

    std::vector<std::string> Kernel::busy()
    {
        std::vector<std::string> v;
        if (inReset()) v.push_back("reset (cycle " + std::to_string(m_cycle) + " of " + std::to_string(m_opts.reset_cycles) + ")");
        for (auto &a : m_agents) if (!a->idle()) v.push_back("agent " + a->name());
        for (auto &l : m_links)
            l->forEachChannel([&](ChannelState &c) {
                if (c.valid() || c.inProgress()) v.push_back("link " + l->name() + " " + channelName(c.id()) + (c.inProgress() ? " (burst in progress)" : ""));
            });
        return v;
    }

    Kernel::RunResult Kernel::runToDrain(uint64_t max_cycles)
    {
        RunResult r;
        if (max_cycles == 0)
        {
            r.stop = drained() ? RunResult::Stop::Idle : RunResult::Stop::CycleLimit;
            return r;
        }
        while (r.cycles < max_cycles)
        {
            step();
            r.cycles++;
            if (drained()) { r.stop = RunResult::Stop::Idle; return r; }
        }
        r.stop = RunResult::Stop::CycleLimit;
        return r;
    }

    void Kernel::finishStats()
    {
        for (auto &l : m_links) l->forEachChannel([&](ChannelState &c) { m_stats.setChannel(l->name(), c.id(), c.stats()); });
    }

    void Kernel::writeStats(const std::string &path)
    {
        finishStats();
        m_stats.write(path);
    }
}}
