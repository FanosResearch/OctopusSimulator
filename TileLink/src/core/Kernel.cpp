/*
 * File  :      Kernel.cpp
 * The three-phase cycle (plan §3.2), elaboration (plan §3.4), reset (§3.2.2) and the system
 * builder (plan §4).
 */
#include "tilelink/Kernel.h"

namespace octopus { namespace tl
{
    Kernel::Kernel(const KernelOptions &opts) : m_opts(opts) {}
    Kernel::~Kernel() {}

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
                add(reg.make(a.get<std::string>("type"), name, a));
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
        m_cycle++;
    }

    void Kernel::run(uint64_t cycles)
    {
        for (uint64_t i = 0; i < cycles; i++) step();
    }

    uint64_t Kernel::runUntilIdle(uint64_t max_cycles)
    {
        uint64_t n = 0;
        while (n < max_cycles)
        {
            step();
            n++;
            if (inReset()) continue;
            bool idle = true;
            for (auto &l : m_links) l->forEachChannel([&](ChannelState &c) { if (c.valid() || c.inProgress()) idle = false; });
            for (auto &a : m_agents) if (!a->idle()) idle = false;
            if (idle) break;
        }
        return n;
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
