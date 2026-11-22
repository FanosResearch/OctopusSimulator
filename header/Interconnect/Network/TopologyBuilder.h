/*
 * File  :      TopologyBuilder.h
 * Topologies as plug-ins (docs/NoC_Topology_Plugin_Plan.md s3.4). A builder turns the
 * `interconnect.` keys into a Topology through the construction API of Topology.h. Builders
 * are registered explicitly in registerBuiltinTopologies(): the network code is linked from a
 * static library, where a self-registering object file nobody references would be dropped.
 *
 * Adding a topology: one builder class in src/Interconnect/Network/topologies/, one add() line
 * in Builtin.cpp, one target_sources entry in src/Interconnect/CMakeLists.txt, and its keys in
 * configuration/Interconnect/Network.csv.
 */
#ifndef _NET_TOPOLOGY_BUILDER_H
#define _NET_TOPOLOGY_BUILDER_H
#include "Topology.h"
#include "../../Configurable.h"
#include <memory>
#include <string>
#include <vector>

namespace octopus { namespace net
{
    // A read-only view of the network's keys (names without the `interconnect.` prefix).
    class KeyView
    {
    public:
        explicit KeyView(const Configurable::ParametersMap &m) : m_map(m) {}
        bool has(const std::string &k) const { return m_map.count(k) > 0; }
        int geti(const std::string &k) const;
        std::string gets(const std::string &k) const;
        std::vector<int> getvi(const std::string &k) const;
        // rows key[0], key[1], ... while present
        std::vector<std::vector<int>> rows(const std::string &key) const;
        // node_map[<id>](i) for every endpoint, in ascending endpoint id; a missing one is refused
        std::map<int, int> nodeMap(const std::vector<int> &endpoints) const;
    private:
        const Configurable::ParametersMap &m_map;
    };

    struct KeySpec { std::string name, type, dflt, meaning; };

    class TopologyBuilder
    {
    public:
        virtual ~TopologyBuilder() {}
        virtual std::string name() const = 0;                 // the value of `topology`
        virtual std::vector<KeySpec> keys() const = 0;        // the builder's own keys
        virtual Topology build(const KeyView &cfg, const std::vector<int> &endpoint_ids, int local_ports) const = 0;
    };

    class TopologyRegistry
    {
    public:
        static TopologyRegistry &builtin();                   // filled once by registerBuiltinTopologies
        void add(std::unique_ptr<TopologyBuilder> b);
        const TopologyBuilder *find(const std::string &name) const;
        std::vector<std::string> names() const;               // in registration order
        Topology create(const std::string &name, const KeyView &cfg, const std::vector<int> &endpoint_ids, int local_ports) const;
    private:
        std::vector<std::unique_ptr<TopologyBuilder>> m_builders;
    };

    void registerBuiltinTopologies(TopologyRegistry &reg);

    // The shipped generators, also used directly by the unit tests (Topology::grid2d etc.).
    Topology buildGrid2D(int kx, int ky, const std::map<int, int> &node_map, int local_ports, bool torus);
    Topology buildRing(int k, const std::map<int, int> &node_map, int local_ports);
    Topology buildLattice3D(int kx, int ky, int kz, const std::map<int, int> &node_map, int local_ports, bool torus);
    Topology buildButterfly(int k, int n, const std::map<int, int> &node_map, int local_ports);
    Topology buildFlattenedButterfly(const std::vector<int> &dims, const std::map<int, int> &node_map, int local_ports);
    Topology buildStar(const std::vector<int> &endpoint_ids, int local_ports);
    Topology buildCustom(const std::vector<std::vector<int>> &matrix, const std::vector<int> &endpoint_ids,
                         const std::vector<int> &router_ids, int local_ports);
}}
#endif /* _NET_TOPOLOGY_BUILDER_H */
