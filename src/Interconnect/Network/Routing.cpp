/*
 * File  :      Routing.cpp
 * Specification: docs/NoC_Architecture_Spec.md §10, §14.1, §14.3.
 */
#include "../../../header/Interconnect/Network/Routing.h"
#include <algorithm>
#include <deque>
#include <functional>
#include <set>
#include <iostream>
#include <sstream>
#include <tuple>

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
        int m_ties = 0;                          // weighted: (router, destination) pairs with several equal-cost ports
        // Shortest weighted paths (link_weight[<router>](vi): neighbour,weight,...; default weight 1):
        // Dijkstra from every destination over reversed links; next hop = the lowest port on a
        // shortest path.
        void weighted(const Topology &t, const RoutingParams &p)
        {
            int R = t.routers();
            std::vector<std::vector<long>> w(R);
            for (int r = 0; r < R; r++) w[r].assign(t.linkPorts(r), 1);
            for (auto &kv : p.link_weight)
            {
                int r = t.routerIndex(kv.first);
                if (r < 0) throw ConfigError("link_weight[" + std::to_string(kv.first) + "]: not a router id");
                if (kv.second.size() % 2) throw ConfigError("link_weight[" + std::to_string(kv.first) + "]: expects neighbour,weight pairs");
                for (size_t i = 0; i + 1 < kv.second.size(); i += 2)
                {
                    int n = t.routerIndex(kv.second[i]), wt = kv.second[i + 1], port = n < 0 ? -1 : portTowards(t, r, n);
                    if (port < 0) throw ConfigError("link_weight[" + std::to_string(kv.first) + "]: " + std::to_string(kv.second[i]) + " is not a neighbour of router " + std::to_string(kv.first));
                    if (wt < 1) throw ConfigError("link_weight[" + std::to_string(kv.first) + "]: weights must be >= 1");
                    for (int q = 0; q < t.linkPorts(r); q++) if (t.link(r, q).router == n) w[r][q] = wt;
                }
            }
            const long INF = (long)1 << 60;
            for (int d = 0; d < R; d++)
            {
                std::vector<long> dist(R, INF); dist[d] = 0;
                std::set<std::pair<long, int>> pq{{0, d}};
                while (!pq.empty())
                {
                    int r = pq.begin()->second; pq.erase(pq.begin());
                    // every router n with a link n -> r relaxes through it
                    for (int q = 0; q < t.linkPorts(r); q++)
                    {
                        auto back = t.link(r, q); int n = back.router;
                        long nd = dist[r] + w[n][back.port];
                        if (nd < dist[n]) { pq.erase({dist[n], n}); dist[n] = nd; pq.insert({nd, n}); }
                    }
                }
                for (int r = 0; r < R; r++)
                {
                    if (r == d) { m_next[r][d] = t.linkPorts(r); continue; }
                    if (dist[r] >= INF) throw ConfigError("Table: router " + std::to_string(t.routerId(d)) + " is unreachable from router " + std::to_string(t.routerId(r)));
                    int best = -1, ties = 0;
                    for (int q = 0; q < t.linkPorts(r); q++)
                        if (w[r][q] + dist[t.link(r, q).router] == dist[r]) { if (best < 0) best = q; ties++; }
                    m_next[r][d] = best;
                    if (ties > 1) m_ties++;
                }
            }
            std::cout << "Table: weighted shortest paths; " << m_ties << " (router, destination) pairs chose between equal-cost ports by lowest port index (state preferences with routing_via)" << std::endl;
        }
    public:
        int equalCostTies() const { return m_ties; }
        TableRouting(const Topology &t, const RoutingParams &p) : m_t(t), m_escape_vc(p.escape_vc), m_vcs(p.vcs_per_class)
        {
            int R = t.routers();
            m_next.assign(R, std::vector<int>(R, -1));
            if (!p.link_weight.empty()) { weighted(t, p); }
            else
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
            // CSV overrides by neighbour: dest id, neighbour router id, ... (the preferred form: it
            // states the intent without depending on port numbering)
            for (auto &kv : p.routing_via)
            {
                int r = t.routerIndex(kv.first);
                if (r < 0) throw ConfigError("routing_via[" + std::to_string(kv.first) + "]: not a router id");
                if (kv.second.size() % 2) throw ConfigError("routing_via[" + std::to_string(kv.first) + "]: expects dest,neighbour pairs");
                for (size_t i = 0; i + 1 < kv.second.size(); i += 2)
                {
                    int d = t.routerIndex(kv.second[i]), n = t.routerIndex(kv.second[i + 1]);
                    if (d < 0) throw ConfigError("routing_via[" + std::to_string(kv.first) + "]: destination " + std::to_string(kv.second[i]) + " is not a router id");
                    int port = n < 0 ? -1 : portTowards(t, r, n);
                    if (port < 0)
                        throw ConfigError("routing_via[" + std::to_string(kv.first) + "]: " + std::to_string(kv.second[i + 1]) + " is not a neighbour of router " + std::to_string(kv.first));
                    m_next[r][d] = port;
                }
            }
            // CSV overrides by port: dest id, port, dest id, port, ... (applied last)
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

    // ------------------------------------------------------------------ XY: dimension order on any lattice
    // (grid, torus, ring, any rank). Dimension 0 first, then 1, ... On a wrapped dimension the
    // shorter way round, and the dateline rule: a leg (from the source) that crosses the wrap
    // link uses VC 1, any other leg VC 0. The rule is a function of (src, dst), so a pair's
    // packets keep one path and one VC.
    class XYRouting : public RoutingAlgorithm
    {
    protected:
        const Topology &m_t; int m_vcs; const Lattice &m_l;
        static const Lattice &needLattice(const Topology &t, const std::string &who)
        {
            if (!t.lattice()) throw ConfigError(who + " routing needs a lattice topology (grid2d, torus2d, ring)");
            return *t.lattice();
        }
        // signed step in dimension d: +1, -1 or 0; on a wrapped dimension the shorter way, and
        // `wrap` says whether that way crosses the wrap link
        int stepIn(int d, int from, int to, bool &wrap) const
        {
            wrap = false;
            if (from == to) return 0;
            int k = m_l.dims[d];
            int fwd = (to - from + k) % k, back = (from - to + k) % k;
            if (!m_l.wrap[d]) return to > from ? 1 : -1;
            if (fwd <= back) { wrap = (from + fwd) >= k; return 1; }
            wrap = (from - back) < 0; return -1;
        }
        int portForStep(int r, int d, int dir) const
        {
            int p = m_t.dirPort(r, d, dir);
            if (p < 0) throw ConfigError("internal: router " + std::to_string(m_t.routerId(r)) + " has no port in dimension " + std::to_string(d));
            return p;
        }
        int localOut(const RouteContext &c) const { return c.packet ? localPortFor(m_t, c.router, c.packet->dst_endpoint) : m_t.linkPorts(c.router); }
    public:
        XYRouting(const Topology &t, const RoutingParams &p) : m_t(t), m_vcs(p.vcs_per_class), m_l(needLattice(t, "XY"))
        {
            if (m_l.anyWrap() && m_vcs < 2) throw ConfigError(t.kindName() + " with XY needs vcs_per_class >= 2 (dateline rule)");
        }
        Route nextHop(const RouteContext &c) const override
        {
            Route rt; rt.vc_mask = allVCs(m_vcs);
            auto here = m_l.coord(c.router), dst = m_l.coord(c.dst_router), src = m_l.coord(c.src_router);
            int D = m_l.rank(), dim = D - 1, step = 0;
            for (int d = 0; d < D; d++) { bool w; int s = stepIn(d, here[d], dst[d], w); if (s) { dim = d; step = s; break; } }
            if (m_l.anyWrap())
            {
                bool w; stepIn(dim, src[dim], dst[dim], w);
                rt.vc_mask = w ? 0b10u : 0b01u;
            }
            rt.out_port = step ? portForStep(c.router, dim, step) : localOut(c);
            return rt;
        }
        Ordering ordering() const override { return Ordering::PAIR_PER_CLASS; }
        bool fixesVCPerPair() const override { return m_l.anyWrap(); }
        bool deadlockFreeOn(const Topology &) const override { return true; }    // by construction
        std::string name() const override { return "XY"; }
    };

    // ------------------------------------------------------------------ oblivious two-set routing
    // O1TURN, Valiant and ROMM choose once per packet (Packet::route_aux, drawn in onInject from a
    // hash of the packet's sequence number and routing_seed). They run on a lattice without wrap
    // links and split each class's VCs into two sets, A = [0, vcs/2) and B = [vcs/2, vcs): each
    // set carries dimension-order routes only, and a packet moves from A to B at most once, so
    // the channel dependency graph is acyclic.
    static uint64_t splitmix(uint64_t x)
    {
        x += 0x9E3779B97F4A7C15ull; x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull; return x ^ (x >> 31);
    }
    class TwoSetRouting : public XYRouting
    {
    protected:
        std::string m_name; uint64_t m_seed; uint32_t m_A, m_B;
        uint64_t draw(const Packet &p) const { return splitmix(p.seq * 0x100000001B3ull ^ m_seed); }
        bool overLink(const RouteContext &c) const { return c.in_port >= 0 && !m_t.isLocalPort(c.router, c.in_port); }
        bool inB(int vc) const { return (m_B >> vc) & 1u; }
        // the next dimension-order step from r toward target, dimensions in order (reverse = last
        // dimension first); -1 at the target
        int step(int r, int target, bool reverse) const
        {
            auto h = m_l.coord(r), g = m_l.coord(target);
            for (int i = 0; i < m_l.rank(); i++)
            {
                int d = reverse ? m_l.rank() - 1 - i : i;
                if (h[d] != g[d]) return portForStep(r, d, g[d] > h[d] ? 1 : -1);
            }
            return -1;
        }
        Route make(int port, uint32_t mask) const { Route rt; rt.out_port = port; rt.vc_mask = mask; return rt; }
        Route local(const RouteContext &c) const { return make(localOut(c), allVCs(m_vcs)); }
    public:
        TwoSetRouting(const Topology &t, const RoutingParams &p, const std::string &name) : XYRouting(t, p), m_name(name), m_seed(p.seed)
        {
            if (m_l.anyWrap()) throw ConfigError(name + " routing needs a lattice without wrap links (grid2d, mesh3d)");
            if (m_vcs < 2) throw ConfigError(name + " routing needs vcs_per_class >= 2 (two VC sets)");
            m_A = allVCs(m_vcs / 2); m_B = allVCs(m_vcs) & ~m_A;
        }
        Ordering ordering() const override { return Ordering::NONE; }
        bool fixesVCPerPair() const override { return false; }
        bool deadlockFreeOn(const Topology &) const override { return true; }   // by construction (see above)
        std::string name() const override { return m_name; }
    };
    // O1TURN (Seo et al., ISCA 2005): each packet takes XY (set A) or YX (set B) with equal probability.
    class O1TurnRouting : public TwoSetRouting
    {
        Route orderRoute(const RouteContext &c, int o) const
        {
            int p = step(c.router, c.dst_router, o == 1);
            return p < 0 ? local(c) : make(p, o == 0 ? m_A : m_B);
        }
    public:
        O1TurnRouting(const Topology &t, const RoutingParams &p) : TwoSetRouting(t, p, "O1TURN")
        {
            if (m_l.rank() != 2) throw ConfigError("O1TURN routing needs a 2-D lattice (grid2d)");
        }
        void onInject(Packet &p) const override { p.route_aux = (int)(draw(p) & 1u); }
        Route nextHop(const RouteContext &c) const override
        {
            int o = c.packet && c.packet->route_aux >= 0 ? c.packet->route_aux : (overLink(c) && inB(c.in_vc) ? 1 : 0);
            return orderRoute(c, o);
        }
        std::vector<Route> candidates(const RouteContext &c) const override
        {
            if (overLink(c)) return {orderRoute(c, inB(c.in_vc) ? 1 : 0)};
            return {orderRoute(c, 0), orderRoute(c, 1)};
        }
    };
    // Valiant (1982): dimension order to a random intermediate router on set A, then dimension
    // order to the destination on set B. ROMM (Nesson and Johnsson, 1995): the same with the
    // intermediate drawn inside the minimal box of source and destination, so paths stay minimal.
    class TwoPhaseRouting : public TwoSetRouting
    {
        bool m_minimal;
        bool phase2(const RouteContext &c) const
        {
            if (overLink(c) && inB(c.in_vc)) return true;
            return c.packet ? c.router == c.packet->route_aux : false;
        }
        Route toDst(const RouteContext &c) const { int p = step(c.router, c.dst_router, false); return p < 0 ? local(c) : make(p, m_B); }
    public:
        TwoPhaseRouting(const Topology &t, const RoutingParams &p, bool minimal) : TwoSetRouting(t, p, minimal ? "ROMM" : "Valiant"), m_minimal(minimal) {}
        void onInject(Packet &p) const override
        {
            uint64_t h = draw(p);
            if (!m_minimal) { p.route_aux = (int)(h % (uint64_t)m_t.routers()); return; }
            auto s = m_l.coord(p.src_router), d = m_l.coord(p.dst_router), i = s;
            for (int k = 0; k < m_l.rank(); k++)
            {
                int lo = std::min(s[k], d[k]), hi = std::max(s[k], d[k]);
                i[k] = lo + (int)(h % (uint64_t)(hi - lo + 1)); h = splitmix(h);
            }
            p.route_aux = m_l.index(i);
        }
        Route nextHop(const RouteContext &c) const override
        {
            if (!c.packet) return toDst(c);                       // no per-packet state: the direct route
            if (phase2(c)) return toDst(c);
            int p = step(c.router, c.packet->route_aux, false);
            return p < 0 ? toDst(c) : make(p, m_A);
        }
        std::vector<Route> candidates(const RouteContext &c) const override
        {
            if (overLink(c) && inB(c.in_vc)) return {toDst(c)};
            // phase 1 toward any intermediate: the dimension-order continuations of the arrival
            // direction (or every direction at the source), on set A; or the switch to phase 2 here
            std::vector<Route> out{toDst(c)};
            int dim = -1, dir = 0, ind = 0, indir = 0;
            if (overLink(c) && m_t.portDir(c.router, c.in_port, ind, indir)) { dim = ind; dir = -indir; }
            for (int d = 0; d < m_l.rank(); d++) for (int s : {+1, -1})
            {
                if (dim >= 0 && (d < dim || (d == dim && s != dir))) continue;
                int p = m_t.dirPort(c.router, d, s);
                if (p >= 0) out.push_back(make(p, m_A));
            }
            return out;
        }
    };

    // ------------------------------------------------------------------ turn models (2-D lattice without wraps)
    // A turn model allows a set of minimal outputs at each hop; the head takes the one whose VCs
    // of its class hold the most credits, ties to the lowest port. Directions: dimension 0 is x
    // (east +1, west -1), dimension 1 is y growing downwards (south +1, north -1). Each model
    // forbids enough turns to break every cycle, so it is deadlock-free with any VC count.
    class TurnModelRouting : public XYRouting
    {
        int sumCredits(const RouteContext &c, int port) const
        {
            if (!c.here) return 0;
            int s = 0; for (int v = 0; v < c.here->vcsPerClass(); v++) s += c.here->credits(port, c.cls, v);
            return s;
        }
    protected:
        std::string m_name;
        int east(int r) const { return portForStep(r, 0, +1); }
        int west(int r) const { return portForStep(r, 0, -1); }
        int ystep(int r, int dy) const { return portForStep(r, 1, dy > 0 ? 1 : -1); }
        // the allowed minimal outputs; dx, dy != (0, 0)
        virtual std::vector<int> allowed(const RouteContext &c, const std::vector<int> &here, const std::vector<int> &dst) const = 0;
        std::vector<int> ports(const RouteContext &c) const
        {
            auto here = m_l.coord(c.router), dst = m_l.coord(c.dst_router);
            if (here == dst) return {};
            std::vector<int> a = allowed(c, here, dst);
            std::sort(a.begin(), a.end());
            if (a.empty()) throw ConfigError(m_name + ": no allowed output at router " + std::to_string(m_t.routerId(c.router)));
            return a;
        }
    public:
        TurnModelRouting(const Topology &t, const RoutingParams &p, const std::string &name) : XYRouting(t, p), m_name(name)
        {
            if (m_l.rank() != 2 || m_l.anyWrap()) throw ConfigError(name + " routing needs a grid2d topology");
        }
        bool fixesVCPerPair() const override { return false; }
        std::vector<Route> candidates(const RouteContext &c) const override
        {
            Route rt; rt.vc_mask = allVCs(m_vcs);
            auto a = ports(c);
            if (a.empty()) { rt.out_port = localOut(c); return {rt}; }
            std::vector<Route> out;
            for (int p : a) { rt.out_port = p; out.push_back(rt); }
            return out;
        }
        Route nextHop(const RouteContext &c) const override
        {
            Route rt; rt.vc_mask = allVCs(m_vcs);
            auto a = ports(c);
            if (a.empty()) { rt.out_port = localOut(c); return rt; }
            int best = a[0], bestc = sumCredits(c, a[0]);
            for (size_t i = 1; i < a.size(); i++) { int cr = sumCredits(c, a[i]); if (cr > bestc) { best = a[i]; bestc = cr; } }
            rt.out_port = best; return rt;
        }
        Ordering ordering() const override { return Ordering::NONE; }
        std::string name() const override { return m_name; }
    };
    // West-first: all westward hops first; then any minimal mix of east, north and south.
    class WestFirstRouting : public TurnModelRouting
    {
    public:
        WestFirstRouting(const Topology &t, const RoutingParams &p) : TurnModelRouting(t, p, "WestFirst") {}
        std::vector<int> allowed(const RouteContext &c, const std::vector<int> &h, const std::vector<int> &d) const override
        {
            int dx = d[0] - h[0], dy = d[1] - h[1];
            if (dx < 0) return {west(c.router)};
            std::vector<int> a;
            if (dx > 0) a.push_back(east(c.router));
            if (dy != 0) a.push_back(ystep(c.router, dy));
            return a;
        }
    };
    // North-last: northward hops last, and no turn after them.
    class NorthLastRouting : public TurnModelRouting
    {
    public:
        NorthLastRouting(const Topology &t, const RoutingParams &p) : TurnModelRouting(t, p, "NorthLast") {}
        std::vector<int> allowed(const RouteContext &c, const std::vector<int> &h, const std::vector<int> &d) const override
        {
            int dx = d[0] - h[0], dy = d[1] - h[1];
            if (dy < 0) return dx != 0 ? std::vector<int>{dx > 0 ? east(c.router) : west(c.router)} : std::vector<int>{ystep(c.router, dy)};
            std::vector<int> a;
            if (dx != 0) a.push_back(dx > 0 ? east(c.router) : west(c.router));
            if (dy > 0) a.push_back(ystep(c.router, dy));
            return a;
        }
    };
    // Negative-first: the negative directions (west, north) first, adaptively; then the
    // positive ones (east, south), adaptively.
    class NegativeFirstRouting : public TurnModelRouting
    {
    public:
        NegativeFirstRouting(const Topology &t, const RoutingParams &p) : TurnModelRouting(t, p, "NegativeFirst") {}
        std::vector<int> allowed(const RouteContext &c, const std::vector<int> &h, const std::vector<int> &d) const override
        {
            int dx = d[0] - h[0], dy = d[1] - h[1];
            std::vector<int> a;
            if (dx < 0 || dy < 0)
            {
                if (dx < 0) a.push_back(west(c.router));
                if (dy < 0) a.push_back(ystep(c.router, dy));
                return a;
            }
            if (dx > 0) a.push_back(east(c.router));
            if (dy > 0) a.push_back(ystep(c.router, dy));
            return a;
        }
    };
    // Odd-even (Chiu, IEEE TPDS 2000): no east-to-north/south turn in an even column, no
    // north/south-to-west turn in an odd column. The minimal routing function of the paper.
    class OddEvenRouting : public TurnModelRouting
    {
    public:
        OddEvenRouting(const Topology &t, const RoutingParams &p) : TurnModelRouting(t, p, "OddEven") {}
        std::vector<int> allowed(const RouteContext &c, const std::vector<int> &h, const std::vector<int> &d) const override
        {
            int dx = d[0] - h[0], dy = d[1] - h[1];
            int sx = m_l.coord(c.src_router)[0];
            std::vector<int> a;
            if (dx == 0) return {ystep(c.router, dy)};
            if (dx > 0)
            {
                if (dy == 0) return {east(c.router)};
                if (h[0] % 2 == 1 || h[0] == sx) a.push_back(ystep(c.router, dy));
                if (d[0] % 2 == 1 || dx != 1) a.push_back(east(c.router));
                return a;
            }
            a.push_back(west(c.router));
            if (h[0] % 2 == 0 && dy != 0) a.push_back(ystep(c.router, dy));
            return a;
        }
    };

    // ------------------------------------------------------------------ AdaptiveMinimal
    // Minimal adaptive routing with an escape network (Duato). A head takes the minimal output
    // with the most adaptive credits; if it cannot get an adaptive VC there, VC allocation may
    // grant it the escape route instead (Route::alt). A packet that arrived over a link on an
    // escape VC stays on the escape network. The escape network is dimension-order routing; on a
    // wrapped dimension it uses two VCs, escape_vc and escape_vc + 1, with a memoryless dateline:
    // a hop whose remaining leg in that dimension still crosses the wrap link uses escape_vc + 1,
    // any other escape_vc. deadlockFreeOn checks the escape network's own dependency graph.
    class AdaptiveMinimalRouting : public XYRouting
    {
        int m_escape; bool m_wrapped;
        bool onEscape(int vc) const { return vc == m_escape || (m_wrapped && vc == m_escape + 1); }
        uint32_t escapeMask() const { return (1u << m_escape) | (m_wrapped ? (1u << (m_escape + 1)) : 0u); }
        uint32_t adaptiveMask() const { return allVCs(m_vcs) & ~escapeMask(); }
        int sumCredits(const RouteContext &c, int port) const
        {
            if (!c.here) return 0;
            int s = 0; for (int v = 0; v < c.here->vcsPerClass(); v++) if (!onEscape(v)) s += c.here->credits(port, c.cls, v);
            return s;
        }
        // the escape route at `router` toward `dst` (dst_endpoint for the local port when known)
        Route escapeRoute(int router, int dst, const Packet *pk) const
        {
            Route rt;
            auto here = m_l.coord(router), to = m_l.coord(dst);
            for (int d = 0; d < m_l.rank(); d++)
            {
                bool w; int s = stepIn(d, here[d], to[d], w);
                if (!s) continue;
                rt.out_port = portForStep(router, d, s);
                rt.vc_mask = 1u << ((m_l.wrap[d] && w) ? m_escape + 1 : m_escape);
                return rt;
            }
            rt.out_port = pk ? localPortFor(m_t, router, pk->dst_endpoint) : m_t.linkPorts(router);
            rt.vc_mask = allVCs(m_vcs);
            return rt;
        }
        std::vector<int> minimalPorts(const RouteContext &c) const
        {
            auto here = m_l.coord(c.router), dst = m_l.coord(c.dst_router);
            std::vector<int> cand;                                // one minimal output per dimension still to cover
            for (int d = 0; d < m_l.rank(); d++) { bool w; int s = stepIn(d, here[d], dst[d], w); if (s) cand.push_back(portForStep(c.router, d, s)); }
            std::sort(cand.begin(), cand.end());
            return cand;
        }
        bool stays(const RouteContext &c) const { return onEscape(c.in_vc) && c.in_port >= 0 && !m_t.isLocalPort(c.router, c.in_port); }
    public:
        AdaptiveMinimalRouting(const Topology &t, const RoutingParams &p) : XYRouting(t, p), m_escape(p.escape_vc), m_wrapped(m_l.anyWrap())
        {
            if (p.escape_vc < 0) throw ConfigError("AdaptiveMinimal needs escape_vc >= 0");
            if (p.escape_vc >= p.vcs_per_class) throw ConfigError("escape_vc must be < vcs_per_class");
            if (p.vcs_per_class < 2) throw ConfigError("AdaptiveMinimal needs vcs_per_class >= 2 (one adaptive VC plus the escape VC)");
            if (m_wrapped && (p.vcs_per_class < 3 || p.escape_vc + 1 >= p.vcs_per_class))
                throw ConfigError("AdaptiveMinimal on " + t.kindName() + " needs two escape VCs for the dateline (escape_vc and escape_vc + 1 < vcs_per_class) and one adaptive VC, so vcs_per_class >= 3");
        }
        bool needsEscapeVC() const override { return true; }
        Route nextHop(const RouteContext &c) const override
        {
            Route esc = escapeRoute(c.router, c.dst_router, c.packet);
            if (stays(c)) return esc;
            std::vector<int> cand = minimalPorts(c);
            if (cand.empty()) return esc;                          // arrived: the local port, any VC
            int best = cand[0], bestc = sumCredits(c, cand[0]);
            for (size_t i = 1; i < cand.size(); i++) { int cr = sumCredits(c, cand[i]); if (cr > bestc) { best = cand[i]; bestc = cr; } }
            if (c.here && bestc == 0) return esc;                 // no adaptive credit anywhere: escape
            Route rt; rt.out_port = best; rt.vc_mask = adaptiveMask();
            rt.alt_port = esc.out_port; rt.alt_mask = esc.vc_mask;  // Duato: the escape stays available
            return rt;
        }
        std::vector<Route> candidates(const RouteContext &c) const override
        {
            Route esc = escapeRoute(c.router, c.dst_router, c.packet);
            if (stays(c)) return {esc};
            std::vector<Route> out;
            for (int p : minimalPorts(c)) { Route rt; rt.out_port = p; rt.vc_mask = adaptiveMask(); out.push_back(rt); }
            out.push_back(esc);
            return out;
        }
        Ordering ordering() const override { return Ordering::NONE; }
        bool fixesVCPerPair() const override { return false; }
        bool deadlockFreeOn(const Topology &t) const override { return deadlockCycle(t, m_vcs).empty(); }
        std::string deadlockCycle(const Topology &t, int) const override
        {
            std::string c;
            escapeDependencyAcyclic(t, m_vcs, [this](int r, int, int, int d) { return escapeRoute(r, d, nullptr); }, &c);
            return c.empty() ? c : "its escape network has a " + c;
        }
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
        if (name == "NorthLast") return std::make_unique<NorthLastRouting>(t, p);
        if (name == "NegativeFirst") return std::make_unique<NegativeFirstRouting>(t, p);
        if (name == "OddEven") return std::make_unique<OddEvenRouting>(t, p);
        if (name == "O1TURN") return std::make_unique<O1TurnRouting>(t, p);
        if (name == "Valiant") return std::make_unique<TwoPhaseRouting>(t, p, false);
        if (name == "ROMM") return std::make_unique<TwoPhaseRouting>(t, p, true);
        if (name == "AdaptiveMinimal") return std::make_unique<AdaptiveMinimalRouting>(t, p);
        if (name == "TDM_XY") throw ConfigError("routing TDM_XY is deferred (docs/NoC_Architecture_Spec.md s12)");
        throw ConfigError("unknown routing algorithm '" + name + "' (Table, XY, WestFirst, NorthLast, NegativeFirst, OddEven, O1TURN, Valiant, ROMM, AdaptiveMinimal)");
    }

    // ------------------------------------------------------------------ deadlock check
    // channel = (router, out link port, vc); an edge c1 -> c2 when a packet may hold c1 and then
    // request c2 (spec s14.3).
    namespace
    {
        struct CDG
        {
            std::map<int, std::set<int>> edges; std::set<int> nodes;
            static int id(int router, int port, int vc) { return (router * 64 + port) * 32 + vc; }
            void add(int prev, int cur) { nodes.insert(cur); if (prev >= 0) edges[prev].insert(cur); }
        };
        bool acyclic(const Topology &t, CDG &g, std::string *cycle)
        {
            std::map<int, int> colour; std::vector<int> stack;
            std::function<bool(int)> dfs = [&](int u) -> bool {
                colour[u] = 1; stack.push_back(u);
                for (int v : g.edges[u])
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
            for (int n : g.nodes) if (colour[n] == 0 && !dfs(n)) return false;
            if (cycle) cycle->clear();
            return true;
        }
        void checkPorts(const Topology &t, const std::string &who, int r, int port, int d)
        {
            if (t.linkPorts(r) > 64 || port >= 64) throw ConfigError("the deadlock check supports at most 64 ports per router");
            if (port < 0 || port >= t.ports(r))
                throw ConfigError(who + ": router " + std::to_string(t.routerId(r)) + " has no port " + std::to_string(port) +
                                  " for destination router " + std::to_string(t.routerId(d)));
        }
    }

    std::vector<Route> RoutingAlgorithm::candidates(const RouteContext &c) const { return {nextHop(c)}; }
    std::string RoutingAlgorithm::deadlockCycle(const Topology &t, int vcs_per_class) const
    {
        std::string c; channelDependencyAcyclic(t, *this, vcs_per_class, &c); return c;
    }

    bool channelDependencyAcyclic(const Topology &t, const RoutingAlgorithm &r, int vcs_per_class, std::string *cycle)
    {
        if (vcs_per_class > 32) throw ConfigError("the deadlock check supports at most 32 VCs per class");
        // every state (router, in_port, in_vc) a packet from s to d can reach, following every
        // candidate output (primary and fallback) with every VC its mask allows
        int R = t.routers(); CDG g;
        uint32_t all = vcs_per_class >= 32 ? ~0u : ((1u << vcs_per_class) - 1);
        for (int s = 0; s < R; s++) for (int d = 0; d < R; d++)
        {
            if (s == d) continue;
            struct St { int router, in_port, in_vc, prev; };
            std::set<std::tuple<int, int, int>> seen; std::vector<St> todo{{s, -1, 0, -1}};
            while (!todo.empty())
            {
                St st = todo.back(); todo.pop_back();
                RouteContext c; c.router = st.router; c.in_port = st.in_port; c.in_vc = st.in_vc; c.dst_router = d; c.src_router = s;
                for (const Route &rt : r.candidates(c))
                {
                    std::pair<int, uint32_t> opts[2] = {{rt.out_port, rt.vc_mask}, {rt.alt_port, rt.alt_mask}};
                    for (int k = 0; k < 2; k++)
                    {
                        int port = opts[k].first; uint32_t mask = opts[k].second & all;
                        if (k == 1 && port < 0) continue;
                        if (st.router == d && port == t.linkPorts(st.router)) continue;    // the (possibly virtual) local port
                        checkPorts(t, r.name(), st.router, port, d);
                        if (t.isLocalPort(st.router, port)) continue;
                        auto f = t.link(st.router, port);
                        for (int vc = 0; vc < vcs_per_class; vc++)
                        {
                            if (!(mask & (1u << vc))) continue;
                            int id = CDG::id(st.router, port, vc); g.add(st.prev, id);
                            if (seen.insert({f.router, f.port, vc}).second) todo.push_back({f.router, f.port, vc, id});
                        }
                    }
                }
            }
        }
        return acyclic(t, g, cycle);
    }

    bool escapeDependencyAcyclic(const Topology &t, int vcs_per_class,
                                 const std::function<Route(int, int, int, int)> &escape, std::string *cycle)
    {
        // a packet may enter the escape network at any router; there it follows the escape route
        // to its destination and never leaves it
        int R = t.routers(); CDG g;
        uint32_t all = vcs_per_class >= 32 ? ~0u : ((1u << vcs_per_class) - 1);
        for (int e = 0; e < R; e++) for (int d = 0; d < R; d++)
        {
            if (e == d) continue;
            int r = e, in_port = -1, in_vc = -1, prev = -1;
            for (int guard = 0; r != d; guard++)
            {
                if (guard > 4 * (R + 2)) throw ConfigError("escape route from router " + std::to_string(t.routerId(e)) + " never reaches " + std::to_string(t.routerId(d)));
                Route rt = escape(r, in_port, in_vc, d);
                checkPorts(t, "escape", r, rt.out_port, d);
                if (t.isLocalPort(r, rt.out_port)) break;
                uint32_t mask = rt.vc_mask & all; int vc = -1;
                for (int v = 0; v < vcs_per_class; v++) if (mask & (1u << v)) { if (vc >= 0) throw ConfigError("an escape route must name exactly one VC"); vc = v; }
                if (vc < 0) throw ConfigError("an escape route named no VC");
                int id = CDG::id(r, rt.out_port, vc); g.add(prev, id); prev = id;
                auto f = t.link(r, rt.out_port); r = f.router; in_port = f.port; in_vc = vc;
            }
        }
        return acyclic(t, g, cycle);
    }

    void checkOrderingRequirement(const std::string &required, const RoutingAlgorithm &r, const Topology &t, int vcs_per_class)
    {
        if (required.empty() || required == "none") return;
        if (required != "pair_per_class") throw ConfigError("requires_ordering must be none or pair_per_class, got '" + required + "'");
        if (r.ordering() != Ordering::PAIR_PER_CLASS)
            throw ConfigError("the protocol requires pair_per_class ordering but routing " + r.name() + " does not preserve it");
        if (vcs_per_class > 1 && !r.fixesVCPerPair())
            throw ConfigError("the protocol requires pair_per_class ordering but vcs_per_class = " + std::to_string(vcs_per_class) +
                              " lets a pair's packets take different VCs (only the torus dateline fixes the VC)");
    }
}}
