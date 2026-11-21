/*
 * File  :      Graph.cpp
 * Builders of the explicit graphs: custom (a connection matrix) and star (spec §5.1).
 */
#include "../../../../header/Interconnect/Network/TopologyBuilder.h"
#include <algorithm>
#include <set>

namespace octopus { namespace net
{
    Topology buildStar(const std::vector<int> &endpoint_ids, int local_ports)
    {
        Topology t; t.setKindName("star");
        t.addRouter(0);
        for (int e : endpoint_ids) t.attachAt(0, e);
        t.finalize(local_ports);
        return t;
    }

    // `custom`: rows "node id, neighbour, neighbour, ...". Every endpoint node gets an implicit
    // router of its own; the other nodes are explicit routers. Port order: a node's link ports
    // follow the order its own row names its neighbours (docs/NoC_Topology_Plugin_Plan.md s3.0);
    // a neighbour named twice gets one port.
    Topology buildCustom(const std::vector<std::vector<int>> &matrix, const std::vector<int> &endpoint_ids,
                         const std::vector<int> &router_ids, int local_ports)
    {
        Topology t; t.setKindName("custom");
        std::set<int> endpoints(endpoint_ids.begin(), endpoint_ids.end());
        std::map<int, std::vector<int>> adj;
        for (auto &row : matrix)
        {
            if (row.empty()) throw ConfigError("connection_matrix row is empty");
            if (adj.count(row[0])) throw ConfigError("connection_matrix lists node " + std::to_string(row[0]) + " twice");
            adj[row[0]] = std::vector<int>(row.begin() + 1, row.end());
        }
        for (auto &kv : adj)
            for (int n : kv.second)
            {
                if (!adj.count(n)) throw ConfigError("connection_matrix: node " + std::to_string(kv.first) + " lists " + std::to_string(n) + " which has no row");
                auto &back = adj[n];
                if (std::find(back.begin(), back.end(), kv.first) == back.end())
                    throw ConfigError("connection_matrix is not symmetric: " + std::to_string(kv.first) + " lists " + std::to_string(n) +
                                      " but " + std::to_string(n) + " does not list " + std::to_string(kv.first));
            }
        std::set<int> explicit_routers(router_ids.begin(), router_ids.end());
        for (auto &kv : adj)
        {
            if (!endpoints.count(kv.first) && !explicit_routers.empty() && !explicit_routers.count(kv.first))
                throw ConfigError("connection_matrix node " + std::to_string(kv.first) + " is neither an endpoint (component_ids) nor a router (router_ids)");
            t.addRouter(kv.first);
        }
        for (int e : endpoint_ids)
            if (!adj.count(e)) throw ConfigError("endpoint " + std::to_string(e) + " (component_ids) has no connection_matrix row");
        for (auto &kv : adj)
        {
            int a = t.routerIndex(kv.first);
            std::set<int> named;
            for (int n : kv.second)
                if (named.insert(n).second) t.addPort(a, t.routerIndex(n), "to " + std::to_string(n));
        }
        for (int e : endpoint_ids) { t.attachAt(t.routerIndex(e), e); t.setImplicit(t.routerIndex(e), true); }
        t.finalize(local_ports);
        return t;
    }

    namespace
    {
        class CustomBuilder : public TopologyBuilder
        {
        public:
            std::string name() const override { return "custom"; }
            std::vector<KeySpec> keys() const override
            {
                return {{"connection_matrix[i]", "vi", "from Mesh.csv", "node id, then its neighbours"},
                        {"router_ids", "vi", "-1", "the explicit routers; -1: every node that is not an endpoint"}};
            }
            Topology build(const KeyView &cfg, const std::vector<int> &endpoints, int local_ports) const override
            {
                std::vector<std::vector<int>> rows = cfg.rows("connection_matrix");
                if (rows.empty()) throw ConfigError("topology custom needs connection_matrix[i] rows");
                std::vector<int> rids;
                if (cfg.has("router_ids")) { rids = cfg.getvi("router_ids"); if (!rids.empty() && rids[0] < 0) rids.clear(); }
                return buildCustom(rows, endpoints, rids, local_ports);
            }
        };
        class StarBuilder : public TopologyBuilder
        {
        public:
            std::string name() const override { return "star"; }
            std::vector<KeySpec> keys() const override { return {}; }
            Topology build(const KeyView &, const std::vector<int> &endpoints, int local_ports) const override
            {
                return buildStar(endpoints, local_ports);
            }
        };
    }
    std::unique_ptr<TopologyBuilder> makeCustomBuilder() { return std::make_unique<CustomBuilder>(); }
    std::unique_ptr<TopologyBuilder> makeStarBuilder() { return std::make_unique<StarBuilder>(); }
}}
