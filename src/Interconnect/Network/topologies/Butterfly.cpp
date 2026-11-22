/*
 * File  :      Butterfly.cpp
 * Builders of the butterfly family (spec s5.1):
 *  - butterfly: a k-ary n-fly, unidirectional, as in BookSim's KNFly. n stages of k^(n-1)
 *    routers with k inputs and k outputs each; one-way links between stages; an endpoint
 *    injects at stage 0 and ejects at stage n-1 (split attachment).
 *  - flattened_butterfly: the butterfly with each row of routers merged into one router (Kim,
 *    Dally and Abts, ISCA 2007). Routers form an array; each router links two-way to every
 *    other router of its row in each dimension.
 */
#include "../../../../header/Interconnect/Network/TopologyBuilder.h"
#include <set>

namespace octopus { namespace net
{
    // k-ary n-fly. Router (stage s, row a) has id s * k^(n-1) + a. Output j of (s, a) leads to
    // (s + 1, a with digit n-2-s set to j) and enters there at input v = digit n-2-s of a. Port
    // order: inputs in0..in(k-1) (stages 1..n-1), then outputs out0..out(k-1) (stages
    // 0..n-2), then the local ports. node_map gives each endpoint its terminal t in [0, k^n):
    // it injects at (0, t / k) and ejects at (n-1, t / k). Destination-tag routing picks output
    // digit n-1-s of t at stage s; the path is unique, so Table computes exactly that route.
    Topology buildButterfly(int k, int n, const std::map<int, int> &node_map, int local_ports)
    {
        if (k < 2 || n < 1) throw ConfigError("butterfly needs dims k,n with k >= 2 and n >= 1");
        long per_stage = 1, terminals = 1;
        for (int i = 0; i < n; i++) { terminals *= k; if (i < n - 1) per_stage *= k; if (terminals > (1L << 20)) throw ConfigError("butterfly is too large (k^n > 2^20 terminals)"); }
        int S = (int)per_stage;
        Topology t; t.setKindName("butterfly");
        for (int s = 0; s < n; s++) for (int a = 0; a < S; a++) { int r = t.addRouter(s * S + a); t.setAttr(r, "stage", s); }
        std::vector<int> pw(n, 1);                         // pw[i] = k^i
        for (int i = 1; i < n; i++) pw[i] = pw[i - 1] * k;
        for (int s = 0; s + 1 < n; s++)
        {
            int pos = n - 2 - s;                           // the digit stage s sets
            for (int a = 0; a < S; a++)
            {
                int v = (a / pw[pos]) % k, base = a - v * pw[pos];
                for (int j = 0; j < k; j++)
                    t.addOneWay(s * S + a, (s + 1) * S + base + j * pw[pos], "out" + std::to_string(j), "in" + std::to_string(v));
            }
        }
        std::set<int> used;
        for (auto &kv : node_map)
        {
            int term = kv.second;
            if (term < 0 || term >= (int)terminals)
                throw ConfigError("node_map[" + std::to_string(kv.first) + "] = " + std::to_string(term) + " is not a terminal of this butterfly (0.." + std::to_string(terminals - 1) + ")");
            if (!used.insert(term).second)
                throw ConfigError("node_map[" + std::to_string(kv.first) + "] = " + std::to_string(term) + ": terminal already taken by another endpoint");
            t.attachSplit(term / k, (n - 1) * S + term / k, kv.first);
        }
        t.finalize(local_ports);
        return t;
    }

    // k0 x k1 x ... routers, dimension 0 fastest (as a lattice). Router r links to every router
    // that differs from it in exactly one coordinate. Port order: dimension 0 first, then 1, ...;
    // within a dimension by ascending coordinate. Names x<c>, y<c>, z<c>, then d<i>.<c>.
    Topology buildFlattenedButterfly(const std::vector<int> &dims, const std::map<int, int> &node_map, int local_ports)
    {
        if (dims.empty()) throw ConfigError("flattened_butterfly needs dims k0[,k1,...]");
        long R = 1;
        for (int k : dims) { if (k < 1) throw ConfigError("flattened_butterfly dims must be >= 1"); R *= k; if (R > 4096) throw ConfigError("flattened_butterfly is too large (more than 4096 routers)"); }
        Topology t; t.setKindName("flattened_butterfly");
        int D = (int)dims.size();
        Lattice shape; shape.dims = dims; shape.wrap.assign(D, false);   // index arithmetic only; not published as a lattice
        for (int r = 0; r < (int)R; r++)
        {
            t.addRouter(r);
            auto c = shape.coord(r);
            t.setAttr(r, "flat_rank", D);
            for (int d = 0; d < D; d++) t.setAttr(r, "flat_c" + std::to_string(d), c[d]);
        }
        static const char *nm[3] = {"x", "y", "z"};
        for (int r = 0; r < (int)R; r++)
        {
            auto c = shape.coord(r);
            for (int d = 0; d < D; d++) for (int v = 0; v < dims[d]; v++)
            {
                if (v == c[d]) continue;
                auto n = c; n[d] = v;
                t.addPort(r, shape.index(n), (d < 3 ? std::string(nm[d]) : "d" + std::to_string(d) + ".") + std::to_string(v));
            }
        }
        for (auto &kv : node_map)
        {
            if (kv.second < 0 || kv.second >= (int)R)
                throw ConfigError("node_map[" + std::to_string(kv.first) + "] = " + std::to_string(kv.second) +
                                  " is not a router of this topology (routers 0.." + std::to_string(R - 1) + ")");
            t.attachAt(kv.second, kv.first);
        }
        t.finalize(local_ports);
        return t;
    }

    namespace
    {
        class ButterflyBuilder : public TopologyBuilder
        {
        public:
            std::string name() const override { return "butterfly"; }
            std::vector<KeySpec> keys() const override
            {
                return {{"dims", "vi", "0", "k,n: radix and number of stages"}, {"node_map[<id>]", "i", "none", "the terminal of an endpoint, 0..k^n-1"}};
            }
            Topology build(const KeyView &cfg, const std::vector<int> &endpoints, int local_ports) const override
            {
                std::vector<int> dims = cfg.getvi("dims");
                if (dims.size() != 2) throw ConfigError("dims needs k,n for butterfly");
                return buildButterfly(dims[0], dims[1], cfg.nodeMap(endpoints), local_ports);
            }
        };
        class FlattenedButterflyBuilder : public TopologyBuilder
        {
        public:
            std::string name() const override { return "flattened_butterfly"; }
            std::vector<KeySpec> keys() const override
            {
                return {{"dims", "vi", "0", "routers per dimension k0,k1,..."}, {"node_map[<id>]", "i", "none", "the router of an endpoint"}};
            }
            Topology build(const KeyView &cfg, const std::vector<int> &endpoints, int local_ports) const override
            {
                return buildFlattenedButterfly(cfg.getvi("dims"), cfg.nodeMap(endpoints), local_ports);
            }
        };
    }
    std::unique_ptr<TopologyBuilder> makeButterflyBuilder() { return std::make_unique<ButterflyBuilder>(); }
    std::unique_ptr<TopologyBuilder> makeFlattenedButterflyBuilder() { return std::make_unique<FlattenedButterflyBuilder>(); }
}}
