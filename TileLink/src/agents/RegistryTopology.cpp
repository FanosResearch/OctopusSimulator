/*
 * File  :      RegistryTopology.cpp
 */
#include "tilelink/agents/RegistryTopology.h"
#include "tilelink/agents/Buffer.h"
#include "tilelink/agents/Fragmenter.h"
#include "tilelink/agents/SourceShrinker.h"
#include "tilelink/agents/Throttle.h"
#include "tilelink/agents/WidthWidget.h"
#include "tilelink/agents/Xbar.h"

namespace octopus { namespace tl
{
    void registerTopologyAgents(AgentRegistry &reg)
    {
        reg.add("Xbar",           [](const std::string &n, const Config &c) { return std::unique_ptr<Agent>(new Xbar(n, c)); });
        reg.add("Buffer",         [](const std::string &n, const Config &c) { return std::unique_ptr<Agent>(new Buffer(n, c)); });
        reg.add("WidthWidget",    [](const std::string &n, const Config &c) { return std::unique_ptr<Agent>(new WidthWidget(n, c)); });
        reg.add("Fragmenter",     [](const std::string &n, const Config &c) { return std::unique_ptr<Agent>(new Fragmenter(n, c)); });
        reg.add("SourceShrinker", [](const std::string &n, const Config &c) { return std::unique_ptr<Agent>(new SourceShrinker(n, c)); });
        reg.add("Throttle",       [](const std::string &n, const Config &c) { return std::unique_ptr<Agent>(new Throttle(n, c)); });
    }
}}
