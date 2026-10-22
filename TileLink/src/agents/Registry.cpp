/*
 * File  :      Registry.cpp
 * The full agent registry for tlsim (plan §4).
 */
#include "tilelink/agents/Registry.h"
#include "tilelink/agents/CacheMaster.h"
#include "tilelink/agents/CoherentManager.h"
#include "tilelink/agents/ErrorSlave.h"
#include "tilelink/agents/FuzzMaster.h"
#include "tilelink/agents/RamSlave.h"
#include "tilelink/agents/TraceMaster.h"
#if defined(__has_include)
#  if __has_include("tilelink/agents/RegistryTopology.h")
#    include "tilelink/agents/RegistryTopology.h"
#    define TL_HAVE_TOPOLOGY_AGENTS 1
#  endif
#endif

namespace octopus { namespace tl
{
    void registerT2Agents(AgentRegistry &reg)
    {
        reg.add("RamSlave",   [](const std::string &n, const Config &p) { return std::unique_ptr<Agent>(new RamSlave(n, p)); });
        reg.add("ErrorSlave", [](const std::string &n, const Config &p) { return std::unique_ptr<Agent>(new ErrorSlave(n, p)); });
        reg.add("FuzzMaster", [](const std::string &n, const Config &p) { return std::unique_ptr<Agent>(new FuzzMaster(n, p)); });
        // T6, TL-C (plan §3.5)
        reg.add("CacheMaster",     [](const std::string &n, const Config &p) { return std::unique_ptr<Agent>(new CacheMaster(n, p)); });
        reg.add("CoherentManager", [](const std::string &n, const Config &p) { return std::unique_ptr<Agent>(new CoherentManager(n, p)); });
        reg.add("TraceMaster",     [](const std::string &n, const Config &p) { return std::unique_ptr<Agent>(new TraceMaster(n, p)); });
    }

    AgentRegistry withAllAgents()
    {
        AgentRegistry r = AgentRegistry::withBuiltins();
        registerT2Agents(r);
#ifdef TL_HAVE_TOPOLOGY_AGENTS
        registerTopologyAgents(r);
#endif
        return r;
    }
}}
