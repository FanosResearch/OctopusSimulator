/*
 * File  :      Agent.cpp
 */
#include "tilelink/Agent.h"

namespace octopus { namespace tl
{
    const char *roleName(Role r) { return r == Role::Master ? "master" : "slave"; }

    void Agent::attachKernel(Kernel *k, int id, uint64_t seed)
    {
        m_kernel = k;
        m_id = id;
        // Plan §3.2: one seeded RNG per agent, derived from the kernel seed and the agent's
        // position, so a run is reproducible and agents are independent.
        std::seed_seq seq{uint32_t(seed), uint32_t(seed >> 32), uint32_t(id), 0x7a1eu};
        m_rng.seed(seq);
    }

    ProgressHints Agent::hints() const
    {
        // Plan §3.6: an originator is a master somewhere and a slave nowhere; adapters and
        // slaves do not count as masters in the default formula.
        ProgressHints h;
        bool master = false, slave = false;
        for (auto &lr : m_links) (lr.role == Role::Master ? master : slave) = true;
        h.is_master = master && !slave;
        return h;
    }

    std::unique_ptr<Agent> AgentRegistry::make(const std::string &type, const std::string &name, const Config &params) const
    {
        auto it = m_types.find(type);
        if (it == m_types.end())
        {
            std::string known;
            for (auto &t : types()) known += (known.empty() ? "" : ", ") + t;
            throw ConfigError("unknown agent type '" + type + "' for agent " + name + " (known: " + known + ")");
        }
        return it->second(name, params);
    }

    std::vector<std::string> AgentRegistry::types() const
    {
        std::vector<std::string> v;
        for (auto &kv : m_types) v.push_back(kv.first);
        return v;
    }

    AgentRegistry AgentRegistry::withBuiltins()
    {
        AgentRegistry r;
        r.add("NullMaster", [](const std::string &n, const Config &) { return std::unique_ptr<Agent>(new NullMaster(n)); });
        r.add("NullSlave",  [](const std::string &n, const Config &) { return std::unique_ptr<Agent>(new NullSlave(n)); });
        return r;
    }
}}
