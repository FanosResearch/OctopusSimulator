/*
 * File  :      Registry.h
 * Every agent type tlsim can build by name (plan §4, `agent[k].type(s)`): the builtins
 * (NullMaster, NullSlave), the T2 agents (RamSlave, ErrorSlave, FuzzMaster) and, when the library
 * has them, the topology agents of RegistryTopology.h (Xbar, Buffer, WidthWidget, Fragmenter,
 * SourceShrinker, Throttle).
 */
#ifndef _TL_AGENTS_REGISTRY_H
#define _TL_AGENTS_REGISTRY_H
#include "tilelink/Agent.h"

namespace octopus { namespace tl
{
    // RamSlave, ErrorSlave, FuzzMaster.
    void registerT2Agents(AgentRegistry &reg);
    // AgentRegistry::withBuiltins() + registerT2Agents + registerTopologyAgents (if present).
    AgentRegistry withAllAgents();
}}
#endif /* _TL_AGENTS_REGISTRY_H */
