/*
 * File  :      Lattice.cpp
 * Builders of the generated lattices: grid2d, torus2d, ring (spec §5.1).
 * Port order per router: grid/torus east, west, north, south (absent directions skipped on a
 * grid); ring clockwise, counter-clockwise. These orders are part of every report and must not
 * change (docs/NoC_Topology_Plugin_Plan.md s3.2).
 */
#include "../../../../header/Interconnect/Network/TopologyBuilder.h"

namespace octopus { namespace net
{
    // attach a node_map onto a generated topology
    static void attachMap(Topology &t, const std::map<int, int> &node_map)
    {
        for (auto &kv : node_map)
        {
            if (kv.second < 0 || kv.second >= t.routers())
                throw ConfigError("node_map[" + std::to_string(kv.first) + "] = " + std::to_string(kv.second) +
                                  " is not a router of this topology (routers 0.." + std::to_string(t.routers() - 1) + ")");
            t.attachAt(kv.second, kv.first);
        }
    }

    Topology buildGrid2D(int kx, int ky, const std::map<int, int> &node_map, int local_ports, bool torus)
    {
        if (kx < 1 || ky < 1) throw ConfigError("dims must be >= 1,1");
        if (torus && (kx < 3 || ky < 3)) throw ConfigError("torus2d needs dims >= 3,3 (a wrap link must differ from the direct link)");
        Topology t; t.setKindName(torus ? "torus2d" : "grid2d");
        for (int y = 0; y < ky; y++) for (int x = 0; x < kx; x++) t.addRouter(y * kx + x);
        Lattice l; l.dims = {kx, ky}; l.wrap = {torus, torus}; t.setLattice(l);
        auto at = [&](int x, int y) { return y * kx + x; };
        for (int y = 0; y < ky; y++) for (int x = 0; x < kx; x++)
        {
            int r = at(x, y);
            if (x + 1 < kx) t.addPort(r, at(x + 1, y), "east", 0, +1); else if (torus) t.addPort(r, at(0, y), "east(wrap)", 0, +1);
            if (x - 1 >= 0) t.addPort(r, at(x - 1, y), "west", 0, -1); else if (torus) t.addPort(r, at(kx - 1, y), "west(wrap)", 0, -1);
            if (y - 1 >= 0) t.addPort(r, at(x, y - 1), "north", 1, -1); else if (torus) t.addPort(r, at(x, ky - 1), "north(wrap)", 1, -1);
            if (y + 1 < ky) t.addPort(r, at(x, y + 1), "south", 1, +1); else if (torus) t.addPort(r, at(x, 0), "south(wrap)", 1, +1);
        }
        attachMap(t, node_map);
        t.finalize(local_ports);
        return t;
    }

    Topology buildRing(int k, const std::map<int, int> &node_map, int local_ports)
    {
        if (k < 3) throw ConfigError("ring needs k >= 3");
        Topology t; t.setKindName("ring");
        for (int r = 0; r < k; r++) t.addRouter(r);
        Lattice l; l.dims = {k}; l.wrap = {true}; t.setLattice(l);
        for (int r = 0; r < k; r++)
        {
            t.addPort(r, (r + 1) % k, "clockwise", 0, +1);
            t.addPort(r, (r + k - 1) % k, "counter-clockwise", 0, -1);
        }
        attachMap(t, node_map);
        t.finalize(local_ports);
        return t;
    }

    // 3-D mesh / torus: routers x fastest, then y, then z; ports east, west, north, south (as on
    // a grid), then up (z - 1) and down (z + 1), absent directions skipped on a mesh
    Topology buildLattice3D(int kx, int ky, int kz, const std::map<int, int> &node_map, int local_ports, bool torus)
    {
        std::string name = torus ? "torus3d" : "mesh3d";
        if (kx < 1 || ky < 1 || kz < 1) throw ConfigError("dims must be >= 1,1,1");
        if (torus && (kx < 3 || ky < 3 || kz < 3)) throw ConfigError("torus3d needs dims >= 3,3,3 (a wrap link must differ from the direct link)");
        Topology t; t.setKindName(name);
        for (int r = 0; r < kx * ky * kz; r++) t.addRouter(r);
        Lattice l; l.dims = {kx, ky, kz}; l.wrap = {torus, torus, torus}; t.setLattice(l);
        struct Dir { int dim, dir; const char *name; };
        const Dir dirs[6] = {{0, +1, "east"}, {0, -1, "west"}, {1, -1, "north"}, {1, +1, "south"}, {2, -1, "up"}, {2, +1, "down"}};
        for (int r = 0; r < t.routers(); r++)
        {
            auto c = l.coord(r);
            for (const Dir &d : dirs)
            {
                auto n = c; n[d.dim] += d.dir;
                bool wrap = n[d.dim] < 0 || n[d.dim] >= l.dims[d.dim];
                if (wrap && !torus) continue;
                n[d.dim] = (n[d.dim] + l.dims[d.dim]) % l.dims[d.dim];
                t.addPort(r, l.index(n), std::string(d.name) + (wrap ? "(wrap)" : ""), d.dim, d.dir);
            }
        }
        attachMap(t, node_map);
        t.finalize(local_ports);
        return t;
    }

    namespace
    {
        class Lattice3DBuilder : public TopologyBuilder
        {
            bool m_torus;
        public:
            explicit Lattice3DBuilder(bool torus) : m_torus(torus) {}
            std::string name() const override { return m_torus ? "torus3d" : "mesh3d"; }
            std::vector<KeySpec> keys() const override
            {
                return {{"dims", "vi", "0", "kx,ky,kz"}, {"node_map[<id>]", "i", "none", "the router of an endpoint"}};
            }
            Topology build(const KeyView &cfg, const std::vector<int> &endpoints, int local_ports) const override
            {
                std::vector<int> dims = cfg.getvi("dims");
                std::map<int, int> node_map = cfg.nodeMap(endpoints);
                if (dims.size() < 3) throw ConfigError("dims needs kx,ky,kz for " + name());
                return buildLattice3D(dims[0], dims[1], dims[2], node_map, local_ports, m_torus);
            }
        };
        class GridBuilder : public TopologyBuilder
        {
            bool m_torus;
        public:
            explicit GridBuilder(bool torus) : m_torus(torus) {}
            std::string name() const override { return m_torus ? "torus2d" : "grid2d"; }
            std::vector<KeySpec> keys() const override
            {
                return {{"dims", "vi", "0", "kx,ky"}, {"node_map[<id>]", "i", "none", "the router of an endpoint"}};
            }
            Topology build(const KeyView &cfg, const std::vector<int> &endpoints, int local_ports) const override
            {
                std::vector<int> dims = cfg.getvi("dims");
                std::map<int, int> node_map = cfg.nodeMap(endpoints);
                if (dims.size() < 2) throw ConfigError("dims needs kx,ky for " + name());
                return buildGrid2D(dims[0], dims[1], node_map, local_ports, m_torus);
            }
        };
        class RingBuilder : public TopologyBuilder
        {
        public:
            std::string name() const override { return "ring"; }
            std::vector<KeySpec> keys() const override
            {
                return {{"dims", "vi", "0", "k"}, {"node_map[<id>]", "i", "none", "the router of an endpoint"}};
            }
            Topology build(const KeyView &cfg, const std::vector<int> &endpoints, int local_ports) const override
            {
                std::vector<int> dims = cfg.getvi("dims");
                std::map<int, int> node_map = cfg.nodeMap(endpoints);
                return buildRing(dims.empty() ? 0 : dims[0], node_map, local_ports);
            }
        };
    }
    std::unique_ptr<TopologyBuilder> makeGrid2DBuilder() { return std::make_unique<GridBuilder>(false); }
    std::unique_ptr<TopologyBuilder> makeTorus2DBuilder() { return std::make_unique<GridBuilder>(true); }
    std::unique_ptr<TopologyBuilder> makeRingBuilder() { return std::make_unique<RingBuilder>(); }
    std::unique_ptr<TopologyBuilder> makeMesh3DBuilder() { return std::make_unique<Lattice3DBuilder>(false); }
    std::unique_ptr<TopologyBuilder> makeTorus3DBuilder() { return std::make_unique<Lattice3DBuilder>(true); }
}}
