/*
 * File  :      Routing.cpp
 * Specification: docs/NoC_Architecture_Spec.md §10, §14.1, §14.3.
 */
#include "../../../header/Interconnect/Network/Routing.h"
#include <algorithm>
#include <deque>
#include <functional>
#include <set>
#include <sstream>

namespace octopus { namespace net
{
    // ------------------------------------------------------------------ helpers
    static int portTowards(const Topology &t, int r, int neighbour)
    {
        for (int p = 0; p < t.linkPorts(r); p++) if (t.link(r, p).router == neighbour) return p;
        return -1;
    }
    static int localPortFor(const Topology &t, int r, int dst_endpoint)
    {
        return t.routerOf(dst_endpoint) == r ? t.localPort(dst_endpoint) : -1;
    }
    static uint32_t allVCs(int n) { return n >= 32 ? ~0u : ((1u << n) - 1); }

    std::vector<std::pair<int, int>> RoutingAlgorithm::path(const Topology &t, int src, int dst, int cls) const
    {
        std::vector<std::pair<int, int>> hops;
        int r = src, in_port = -1;
        for (int guard = 0; guard < 4 * (t.routers() + 2); guard++)
        {
            RouteContext c; c.router = r; c.in_port = in_port; c.dst_router = dst; c.cls = cls; c.src_router = src;
            Route rt = nextHop(c);
            if (r == dst)                       // arrived: the last hop is the local port (or a virtual one
            {                                   // at index linkPorts(r) for a router without endpoints)
                hops.push_back({r, rt.out_port >= t.linkPorts(r) ? rt.out_port : t.linkPorts(r)});
                return hops;
            }
            if (rt.out_port < 0 || rt.out_port >= t.ports(r))
                throw ConfigError(name() + ": router " + std::to_string(t.routerId(r)) + " has no port " + std::to_string(rt.out_port) +
                                  " for destination router " + std::to_string(t.routerId(dst)));
            hops.push_back({r, rt.out_port});
            if (t.isLocalPort(r, rt.out_port)) return hops;
            auto f = t.link(r, rt.out_port); in_port = f.port; r = f.router;
        }
        throw ConfigError(name() + ": no path from router " + std::to_string(t.routerId(src)) + " to " + std::to_string(t.routerId(dst)));
    }

    // ------------------------------------------------------------------ Table
    class TableRouting : public RoutingAlgorithm
    {
        const Topology &m_t;
        std::vector<std::vector<int>> m_next;    // [router][dst_router] -> out port (link port or first local port)
        int m_escape_vc, m_vcs;
    public:
        TableRouting(const Topology &t, const RoutingParams &p) : m_t(t), m_escape_vc(p.escape_vc), m_vcs(p.vcs_per_class)
        {
            int R = t.routers();
            m_next.assign(R, std::vector<int>(R, -1));
            // BFS from every destination over reversed links: next hop toward dst is the
            // neighbour with distance-1, ties by lowest port index
            for (int d = 0; d < R; d++)
            {
                std::vector<int> dist(R, -1); std::deque<int> q; dist[d] = 0; q.push_back(d);
                while (!q.empty())
                {
                    int r = q.front(); q.pop_front();
                    for (int p = 0; p < t.linkPorts(r); p++)
                    {
                        int n = t.link(r, p).router;
                        if (dist[n] < 0) { dist[n] = dist[r] + 1; q.push_back(n); }
                    }
                }
                for (int r = 0; r < R; r++)
                {
                    if (r == d) { m_next[r][d] = t.linkPorts(r); continue; }     // a local port; refined per endpoint in nextHop
                    if (dist[r] < 0) throw ConfigError("Table: router " + std::to_string(t.routerId(d)) + " is unreachable from router " + std::to_string(t.routerId(r)));
                    for (int p = 0; p < t.linkPorts(r); p++)
                        if (dist[t.link(r, p).router] == dist[r] - 1) { m_next[r][d] = p; break; }
                }
            }
            // CSV overrides: dest id, port, dest id, port, ...
            for (auto &kv : p.routing_table)
            {
                int r = t.routerIndex(kv.first);
                if (r < 0) throw ConfigError("routing_table[" + std::to_string(kv.first) + "]: not a router id");
                if (kv.second.size() % 2) throw ConfigError("routing_table[" + std::to_string(kv.first) + "]: expects dest,port pairs");
                for (size_t i = 0; i + 1 < kv.second.size(); i += 2)
                {
                    int d = t.routerIndex(kv.second[i]), port = kv.second[i + 1];
                    if (d < 0) throw ConfigError("routing_table[" + std::to_string(kv.first) + "]: destination " + std::to_string(kv.second[i]) + " is not a router id");
                    if (port < 0 || port >= t.ports(r))
                        throw ConfigError("routing_table[" + std::to_string(kv.first) + "]: router " + std::to_string(kv.first) + " has no port " + std::to_string(port));
                    m_next[r][d] = port;
                }
            }
        }
        Route nextHop(const RouteContext &c) const override
        {
            Route rt;
            if (c.router == c.dst_router && c.packet) rt.out_port = localPortFor(m_t, c.router, c.packet->dst_endpoint);
            if (rt.out_port < 0) rt.out_port = m_next[c.router][c.dst_router];
            rt.vc_mask = (m_escape_vc >= 0) ? (allVCs(m_vcs) & ~(1u << m_escape_vc)) : allVCs(m_vcs);
            if (m_escape_vc >= 0 && c.in_vc == m_escape_vc) rt.vc_mask = 1u << m_escape_vc;   // stay on the escape VC
            return rt;
        }
        Ordering ordering() const override { return Ordering::PAIR_PER_CLASS; }
        bool deadlockFreeOn(const Topology &t) const override { std::string c; return channelDependencyAcyclic(t, *this, m_vcs, &c); }
        std::string name() const override { return "Table"; }
    };

    // ------------------------------------------------------------------ XY (grid and torus)
    class XYRouting : public RoutingAlgorithm
    {
    protected:
        const Topology &m_t; int m_vcs; bool m_torus;
        // signed step in a dimension: +1, -1 or 0; on a torus the shorter way, and `wrap` says
        // whether that way crosses the wrap link
        int stepIn(int from, int to, int k, bool &wrap) const
        {
            wrap = false;
            if (from == to) return 0;
            int fwd = (to - from + k) % k, back = (from - to + k) % k;
            if (!m_torus) return to > from ? 1 : -1;
            if (fwd <= back) { wrap = (from + fwd) >= k; return 1; }
            wrap = (from - back) < 0; return -1;
        }
        int portForStep(int r, int dx, int dy) const
        {
            auto c = m_t.coord(r);
            int tx = c.first + dx, ty = c.second + dy;
            if (m_torus) { tx = (tx + m_t.dimX()) % m_t.dimX(); ty = (ty + m_t.dimY()) % m_t.dimY(); }
            int n = ty * m_t.dimX() + tx;
            return portTowards(m_t, r, n);
        }
    public:
        XYRouting(const Topology &t, const RoutingParams &p) : m_t(t), m_vcs(p.vcs_per_class), m_torus(t.kind() == Topology::Kind::Torus2D)
        {
            if (t.kind() != Topology::Kind::Grid2D && t.kind() != Topology::Kind::Torus2D)
                throw ConfigError("XY routing needs a grid2d or torus2d topology");
            if (m_torus && m_vcs < 2) throw ConfigError("torus2d with XY needs vcs_per_class >= 2 (dateline rule)");
        }
        Route nextHop(const RouteContext &c) const override
        {
            Route rt; rt.vc_mask = allVCs(m_vcs);
            auto here = m_t.coord(c.router), dst = m_t.coord(c.dst_router), src = m_t.coord(c.src_router);
            bool wrapx = false, wrapy = false;
            int sx = stepIn(here.first, dst.first, m_t.dimX(), wrapx);
            int sy = stepIn(here.second, dst.second, m_t.dimY(), wrapy);
            if (m_torus)
            {
                // dateline (spec §10.2): a packet whose X leg (from its source) wraps uses VC 1 on X,
                // and likewise on Y; the rule is a function of (src, dst) so order is preserved
                bool wx = false, wy = false;
                stepIn(src.first, dst.first, m_t.dimX(), wx); stepIn(src.second, dst.second, m_t.dimY(), wy);
                bool onX = sx != 0;
                rt.vc_mask = (onX ? wx : wy) ? 0b10u : 0b01u;
            }
            if (sx != 0) rt.out_port = portForStep(c.router, sx, 0);
            else if (sy != 0) rt.out_port = portForStep(c.router, 0, sy);
            else rt.out_port = c.packet ? localPortFor(m_t, c.router, c.packet->dst_endpoint) : m_t.linkPorts(c.router);
            return rt;
        }
        Ordering ordering() const override { return Ordering::PAIR_PER_CLASS; }
        bool deadlockFreeOn(const Topology &) const override { return true; }    // by construction
        std::string name() const override { return "XY"; }
    };

    // ------------------------------------------------------------------ WestFirst (grid)
    class WestFirstRouting : public XYRouting
    {
        int sumCredits(const RouteContext &c, int port) const
        {
            if (!c.here) return 0;
            int s = 0; for (int v = 0; v < c.here->vcsPerClass(); v++) s += c.here->credits(port, c.cls, v);
            return s;
        }
    public:
        WestFirstRouting(const Topology &t, const RoutingParams &p) : XYRouting(t, p)
        {
            if (t.kind() != Topology::Kind::Grid2D) throw ConfigError("WestFirst routing needs a grid2d topology");
        }
        Route nextHop(const RouteContext &c) const override
        {
            Route rt; rt.vc_mask = allVCs(m_vcs);
            auto here = m_t.coord(c.router), dst = m_t.coord(c.dst_router);
            int dx = dst.first - here.first, dy = dst.second - here.second;
            if (dx == 0 && dy == 0) { rt.out_port = c.packet ? localPortFor(m_t, c.router, c.packet->dst_endpoint) : m_t.linkPorts(c.router); return rt; }
            if (dx < 0) { rt.out_port = portForStep(c.router, -1, 0); return rt; }   // west first, always
            // allowed minimal outputs: east if dx > 0, north/south if dy != 0; pick the most credits, ties lowest port
            std::vector<int> cand;
            if (dx > 0) cand.push_back(portForStep(c.router, 1, 0));
            if (dy != 0) cand.push_back(portForStep(c.router, 0, dy > 0 ? 1 : -1));
            std::sort(cand.begin(), cand.end());
            int best = cand[0], bestc = sumCredits(c, cand[0]);
            for (size_t i = 1; i < cand.size(); i++) { int cr = sumCredits(c, cand[i]); if (cr > bestc) { best = cand[i]; bestc = cr; } }
            rt.out_port = best; return rt;
        }
        Ordering ordering() const override { return Ordering::NONE; }
        std::string name() const override { return "WestFirst"; }
    };

    // ------------------------------------------------------------------ AdaptiveMinimal
    class AdaptiveMinimalRouting : public XYRouting
    {
        int m_escape;
        int sumCredits(const RouteContext &c, int port) const
        {
            if (!c.here) return 0;
            int s = 0; for (int v = 0; v < c.here->vcsPerClass(); v++) if (v != m_escape) s += c.here->credits(port, c.cls, v);
            return s;
        }
    public:
        AdaptiveMinimalRouting(const Topology &t, const RoutingParams &p) : XYRouting(t, p), m_escape(p.escape_vc)
        {
            if (p.escape_vc < 0) throw ConfigError("AdaptiveMinimal needs escape_vc >= 0");
            if (p.escape_vc >= p.vcs_per_class) throw ConfigError("escape_vc must be < vcs_per_class");
            if (p.vcs_per_class < 2) throw ConfigError("AdaptiveMinimal needs vcs_per_class >= 2 (one adaptive VC plus the escape VC)");
        }
        bool needsEscapeVC() const override { return true; }
        Route nextHop(const RouteContext &c) const override
        {
            // on the escape VC, or when no adaptive VC has credits: XY on the escape VC
            Route esc = XYRouting::nextHop(c); esc.vc_mask = 1u << m_escape;
            if (c.in_vc == m_escape) return esc;
            auto here = m_t.coord(c.router), dst = m_t.coord(c.dst_router);
            int dx = dst.first - here.first, dy = dst.second - here.second;
            bool wx, wy;
            int sx = stepIn(here.first, dst.first, m_t.dimX(), wx), sy = stepIn(here.second, dst.second, m_t.dimY(), wy);
            if (dx == 0 && dy == 0) { esc.vc_mask = allVCs(m_vcs); return esc; }
            std::vector<int> cand;
            if (sx) cand.push_back(portForStep(c.router, sx, 0));
            if (sy) cand.push_back(portForStep(c.router, 0, sy));
            std::sort(cand.begin(), cand.end());
            int best = cand[0], bestc = sumCredits(c, cand[0]);
            for (size_t i = 1; i < cand.size(); i++) { int cr = sumCredits(c, cand[i]); if (cr > bestc) { best = cand[i]; bestc = cr; } }
            if (c.here && bestc == 0) return esc;                 // no adaptive credit anywhere: escape
            Route rt; rt.out_port = best; rt.vc_mask = allVCs(m_vcs) & ~(1u << m_escape); return rt;
        }
        Ordering ordering() const override { return Ordering::NONE; }
        bool deadlockFreeOn(const Topology &t) const override { return XYRouting::deadlockFreeOn(t); }   // the escape VC is XY
        std::string name() const override { return "AdaptiveMinimal"; }
    };

    // ------------------------------------------------------------------ factory
    std::unique_ptr<RoutingAlgorithm> createRouting(const std::string &name, const Topology &t, const RoutingParams &p)
    {
        if (p.vcs_per_class < 1) throw ConfigError("vcs_per_class must be >= 1");
        if (p.escape_vc >= p.vcs_per_class) throw ConfigError("escape_vc must be < vcs_per_class");
        if (name == "Table") return std::make_unique<TableRouting>(t, p);
        if (name == "XY") return std::make_unique<XYRouting>(t, p);
        if (name == "WestFirst") return std::make_unique<WestFirstRouting>(t, p);
        if (name == "AdaptiveMinimal") return std::make_unique<AdaptiveMinimalRouting>(t, p);
        if (name == "TDM_XY") throw ConfigError("routing TDM_XY is deferred (docs/NoC_Architecture_Spec.md s12)");
        throw ConfigError("unknown routing algorithm '" + name + "' (Table, XY, WestFirst, AdaptiveMinimal)");
    }

    // ------------------------------------------------------------------ deadlock check
    bool channelDependencyAcyclic(const Topology &t, const RoutingAlgorithm &r, int vcs_per_class, std::string *cycle)
    {
        // channel = (router, out link port, vc). Edge c1 -> c2 when some path uses c1 then c2.
        int R = t.routers();
        auto chan = [&](int router, int port, int vc) { return (router * 64 + port) * 32 + vc; };
        std::map<int, std::set<int>> edges; std::set<int> nodes;
        std::vector<std::vector<int>> routes;   // only for paths; VC masks come from nextHop per hop
        for (int s = 0; s < R; s++) for (int d = 0; d < R; d++)
        {
            if (s == d) continue;
            auto hops = r.path(t, s, d);
            int prev = -1;
            for (size_t i = 0; i < hops.size(); i++)
            {
                int router = hops[i].first, port = hops[i].second;
                if (t.isLocalPort(router, port)) break;
                RouteContext c; c.router = router; c.dst_router = d; c.src_router = s; c.in_port = i ? t.link(hops[i - 1].first, hops[i - 1].second).port : -1;
                uint32_t mask = r.nextHop(c).vc_mask;
                for (int vc = 0; vc < vcs_per_class; vc++)
                {
                    if (!(mask & (1u << vc))) continue;
                    int id = chan(router, port, vc); nodes.insert(id);
                    if (prev >= 0) edges[prev].insert(id);
                }
                // the dependency chain follows the packet's own VC; with one VC per hop in the mask
                // that is exact, with several we conservatively link every allowed VC
                for (int vc = 0; vc < vcs_per_class; vc++) if (mask & (1u << vc)) { prev = chan(router, port, vc); break; }
            }
        }
        // DFS cycle detection
        std::map<int, int> colour; std::vector<int> stack;
        std::function<bool(int)> dfs = [&](int u) -> bool {
            colour[u] = 1; stack.push_back(u);
            for (int v : edges[u])
            {
                if (colour[v] == 1)
                {
                    if (cycle)
                    {
                        std::ostringstream o; o << "cycle:";
                        auto it = std::find(stack.begin(), stack.end(), v);
                        for (; it != stack.end(); ++it) { int id = *it; int vc = id % 32, port = (id / 32) % 64, router = id / 32 / 64; o << " (router " << t.routerId(router) << " " << t.portName(router, port) << " vc" << vc << ")"; }
                        *cycle = o.str();
                    }
                    return false;
                }
                if (colour[v] == 0 && !dfs(v)) return false;
            }
            colour[u] = 2; stack.pop_back(); return true;
        };
        for (int n : nodes) if (colour[n] == 0 && !dfs(n)) return false;
        if (cycle) cycle->clear();
        return true;
    }

    void checkOrderingRequirement(const std::string &required, const RoutingAlgorithm &r, const Topology &t, int vcs_per_class)
    {
        if (required.empty() || required == "none") return;
        if (required != "pair_per_class") throw ConfigError("requires_ordering must be none or pair_per_class, got '" + required + "'");
        if (r.ordering() != Ordering::PAIR_PER_CLASS)
            throw ConfigError("the protocol requires pair_per_class ordering but routing " + r.name() + " does not preserve it");
        bool dateline = (t.kind() == Topology::Kind::Torus2D && r.name() == "XY");
        if (vcs_per_class > 1 && !dateline)
            throw ConfigError("the protocol requires pair_per_class ordering but vcs_per_class = " + std::to_string(vcs_per_class) +
                              " lets a pair's packets take different VCs (only the torus dateline fixes the VC)");
    }
}}
