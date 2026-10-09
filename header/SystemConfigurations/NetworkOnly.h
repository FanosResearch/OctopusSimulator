/*
 * File  :      NetworkOnly.h
 * A system with a Network and TrafficGenerator endpoints and no caches: the network-only
 * harness of docs/NoC_Architecture_Spec.md §18 (tests S4-*).
 */
#ifndef _NETWORK_ONLY_SYSTEM_H
#define _NETWORK_ONLY_SYSTEM_H
#include "Configurable.h"
#include "Network.h"
#include "TrafficGenerator.h"
#include <string>
#include <vector>

namespace octopus
{
    class NetworkOnly : public Configurable
    {
    public:
        NetworkOnly(std::vector<std::string> cl_params,
                    std::string config_path = std::string(CONFIGURATION_PATH) + SYSTEM_CONFIGURATIONS,
                    std::string config_name = "NetworkOnly");
        ~NetworkOnly();
    private:
        net::Network *m_net = nullptr;
        std::vector<net::TrafficGenerator *> m_gens;
    };
}
#endif
