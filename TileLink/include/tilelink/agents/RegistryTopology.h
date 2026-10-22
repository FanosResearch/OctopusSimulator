/*
 * File  :      RegistryTopology.h
 * Registers the topology agents of plan §3.5 by type name, for `agent[k].type(s)`:
 * "Xbar", "Buffer", "WidthWidget", "Fragmenter", "SourceShrinker", "Throttle".
 */
#ifndef _TL_AGENTS_REGISTRY_TOPOLOGY_H
#define _TL_AGENTS_REGISTRY_TOPOLOGY_H
#include "tilelink/Agent.h"

namespace octopus { namespace tl
{
    void registerTopologyAgents(AgentRegistry &reg);
}}
#endif /* _TL_AGENTS_REGISTRY_TOPOLOGY_H */
