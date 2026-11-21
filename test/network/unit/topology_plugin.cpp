// Topology plug-in steps (docs/NoC_Topology_Plugin_Plan.md s5): T0 onward.
#include "testing.h"
#include "Routing.h"
#include "Topology.h"
#include "TopologyBuilder.h"
#include <map>
#include <set>
#include <vector>
using namespace octopus;
using namespace octopus::net;

static std::string errOf(std::function<void()> f) { try { f(); } catch (ConfigError &e) { return e.what(); } return ""; }

// the neighbour ids of every link port of a router, in port order
static std::vector<int> portIds(const Topology &t, int id)
{
    int r = t.routerIndex(id); std::vector<int> v;
    for (int p = 0; p < t.linkPorts(r); p++) v.push_back(t.routerId(t.link(r, p).router));
    return v;
}
static void farEndsConsistent(const Topology &t)
{
    for (int r = 0; r < t.routers(); r++) for (int p = 0; p < t.linkPorts(r); p++)
    { auto f = t.link(r, p); CHECK_EQ(t.link(f.router, f.port).router, r); }
}
// router ids a Table path from src to dst crosses (src and dst included)
static std::vector<int> routeIds(const Topology &t, const RoutingAlgorithm &a, int src, int dst)
{
    std::vector<int> ids;
    for (auto &h : a.path(t, t.routerIndex(src), t.routerIndex(dst))) ids.push_back(t.routerId(h.first));
    return ids;
}

static const std::vector<std::vector<int>> MESH = {{0, 1, 2, 3, 10}, {1, 0, 2, 3, 10}, {2, 0, 1, 3, 10}, {3, 0, 1, 2, 10}, {10, 0, 1, 2, 3}};
static const std::vector<std::vector<int>> NOC  = {{0, 200, 10}, {1, 200, 10}, {2, 200, 10}, {3, 200, 10}, {10, 0, 1, 2, 3}, {200, 0, 1, 2, 3}};

// T0: every shipped matrix keeps exactly the port lists it had before the row-order rule
TEST("T0-SHIPPED-PORTS")
{
    Topology m = Topology::custom(MESH, {0, 1, 2, 3, 10}, {}, 0);
    CHECK(portIds(m, 0) == std::vector<int>({1, 2, 3, 10}));
    CHECK(portIds(m, 1) == std::vector<int>({0, 2, 3, 10}));
    CHECK(portIds(m, 2) == std::vector<int>({0, 1, 3, 10}));
    CHECK(portIds(m, 3) == std::vector<int>({0, 1, 2, 10}));
    CHECK(portIds(m, 10) == std::vector<int>({0, 1, 2, 3}));
    farEndsConsistent(m);
    Topology n = Topology::custom(NOC, {0, 1, 2, 3, 10}, {200}, 0);
    for (int e : {0, 1, 2, 3}) CHECK(portIds(n, e) == std::vector<int>({200, 10}));
    CHECK(portIds(n, 10) == std::vector<int>({0, 1, 2, 3}));
    CHECK(portIds(n, 200) == std::vector<int>({0, 1, 2, 3}));
    farEndsConsistent(n);
}
// T0: a row not in ascending order gets its own order (the old rule put lower ids first)
TEST("T0-ROW-ORDER")
{
    std::vector<std::vector<int>> tri = {{0, 2, 1}, {1, 2, 0}, {2, 1, 0}};
    Topology t = Topology::custom(tri, {0, 1, 2}, {}, 0);
    CHECK(portIds(t, 0) == std::vector<int>({2, 1}));
    CHECK(portIds(t, 1) == std::vector<int>({2, 0}));
    CHECK(portIds(t, 2) == std::vector<int>({1, 0}));
    farEndsConsistent(t);
    CHECK_EQ(t.localPort(2), 2);                          // local ports after the link ports
    // a neighbour named twice gets one port
    Topology d = Topology::custom({{0, 1, 1}, {1, 0}}, {0, 1}, {}, 0);
    CHECK_EQ(d.linkPorts(d.routerIndex(0)), 1);
}
// T0: routing_via states a route by neighbour, independent of port order
TEST("T0-ROUTING-VIA")
{
    // NetNoC with the L1 rows reordered: the LLC (10) is now port 0 of every L1
    std::vector<std::vector<int>> re = {{0, 10, 200}, {1, 10, 200}, {2, 10, 200}, {3, 10, 200}, {10, 0, 1, 2, 3}, {200, 0, 1, 2, 3}};
    Topology t = Topology::custom(re, {0, 1, 2, 3, 10}, {200}, 0);
    auto plain = createRouting("Table", t, {});
    CHECK(routeIds(t, *plain, 0, 1) == std::vector<int>({0, 10, 1}));     // the tie now goes to the LLC
    RoutingParams p;
    for (int s : {0, 1, 2, 3}) for (int d : {0, 1, 2, 3}) if (s != d) { p.routing_via[s].push_back(d); p.routing_via[s].push_back(200); }
    auto via = createRouting("Table", t, p);
    for (int s : {0, 1, 2, 3}) for (int d : {0, 1, 2, 3}) if (s != d) CHECK(routeIds(t, *via, s, d) == std::vector<int>({s, 200, d}));
    // the same entries on the shipped order give the same routes
    Topology n = Topology::custom(NOC, {0, 1, 2, 3, 10}, {200}, 0);
    auto vn = createRouting("Table", n, p);
    auto pn = createRouting("Table", n, {});
    for (int s : {0, 1, 2, 3}) for (int d : {0, 1, 2, 3}) if (s != d) CHECK(routeIds(n, *vn, s, d) == routeIds(n, *pn, s, d));
    // errors name the key
    RoutingParams bad; bad.routing_via[0] = {1, 3};
    CHECK(errOf([&] { createRouting("Table", t, bad); }).find("not a neighbour") != std::string::npos);
    RoutingParams odd; odd.routing_via[0] = {1};
    CHECK(errOf([&] { createRouting("Table", t, odd); }).find("dest,neighbour pairs") != std::string::npos);
}

// T2: every lattice direction port leads to the neighbouring coordinate (mod k on a wrapped
// dimension), and to the port the old neighbour search found (the first link to that router)
static void checkLattice(const Topology &t)
{
    const Lattice *L = t.lattice(); CHECK(L != nullptr);
    CHECK_EQ(L->size(), t.routers());
    for (int r = 0; r < t.routers(); r++)
    {
        auto c = L->coord(r); CHECK_EQ(L->index(c), r);
        for (int d = 0; d < L->rank(); d++) for (int dir : {+1, -1})
        {
            auto n = c; n[d] += dir;
            bool inside = n[d] >= 0 && n[d] < L->dims[d];
            if (!inside && L->wrap[d]) { n[d] = (n[d] + L->dims[d]) % L->dims[d]; inside = true; }
            int p = t.dirPort(r, d, dir);
            if (!inside) { CHECK_EQ(p, -1); continue; }
            CHECK(p >= 0);
            CHECK_EQ(t.link(r, p).router, L->index(n));
            int first = -1; for (int q = 0; q < t.linkPorts(r); q++) if (t.link(r, q).router == L->index(n)) { first = q; break; }
            CHECK_EQ(p, first);
        }
    }
}
TEST("T2-LATTICE-PORTS")
{
    checkLattice(Topology::grid2d(4, 4, {}, 0));
    checkLattice(Topology::grid2d(3, 5, {}, 0));
    checkLattice(Topology::grid2d(4, 4, {}, 0, true));
    checkLattice(Topology::grid2d(3, 3, {}, 0, true));
    checkLattice(Topology::ring(3, {}, 0));
    checkLattice(Topology::ring(8, {}, 0));
    CHECK(Topology::star({0, 1}, 0).lattice() == nullptr);
    CHECK(Topology::custom(MESH, {0, 1, 2, 3, 10}, {}, 0).lattice() == nullptr);
}
// T3: implicit routers are exactly the endpoint nodes of a custom graph; kinds are names
TEST("T3-FLAGS")
{
    Topology n = Topology::custom(NOC, {0, 1, 2, 3, 10}, {200}, 0);
    for (int r = 0; r < n.routers(); r++) CHECK_EQ(n.implicitRouter(r), n.isEndpoint(n.routerId(r)));
    Topology g = Topology::grid2d(2, 2, {{0, 0}, {1, 3}}, 0);
    for (int r = 0; r < 4; r++) CHECK_EQ(g.implicitRouter(r), false);
    CHECK_EQ(g.kindName(), std::string("grid2d")); CHECK_EQ(n.kindName(), std::string("custom"));
    CHECK_EQ(Topology::ring(3, {}, 0).kindName(), std::string("ring"));
    CHECK_EQ(Topology::star({0}, 0).kindName(), std::string("star"));
    CHECK_EQ(g.attr(0, "level"), -1); CHECK_EQ(g.attr(0, "level", 7), 7);
}
// T1: the registry lists the shipped builders in order and names them in its error
TEST("T1-REGISTRY")
{
    auto names = TopologyRegistry::builtin().names();
    CHECK(names == std::vector<std::string>({"custom", "grid2d", "torus2d", "ring", "star", "mesh3d", "torus3d"}));
    Configurable::ParametersMap empty; KeyView v(empty);
    std::string e = errOf([&] { TopologyRegistry::builtin().create("hypercube", v, {}, 0); });
    CHECK(e.find("unknown topology 'hypercube' (custom, grid2d, torus2d, ring, star, mesh3d, torus3d)") != std::string::npos);
}

// T5: the dependency check explores every candidate output
TEST("T5-CHECK-DETERMINISTIC-AND-TURN")
{
    RoutingParams p1; p1.vcs_per_class = 1;
    RoutingParams p2; p2.vcs_per_class = 2;
    for (auto dims : std::vector<std::pair<int, int>>{{2, 2}, {4, 4}, {3, 5}})
    {
        Topology g = Topology::grid2d(dims.first, dims.second, {}, 0);
        std::string c;
        CHECK(channelDependencyAcyclic(g, *createRouting("XY", g, p1), 1, &c));
        CHECK(channelDependencyAcyclic(g, *createRouting("WestFirst", g, p1), 1, &c));    // all minimal turns allowed
        CHECK(channelDependencyAcyclic(g, *createRouting("Table", g, p1), 1, &c));
    }
    for (int k : {3, 4, 5})
    {
        Topology t = Topology::grid2d(k, k, {}, 0, true); std::string c;
        CHECK(channelDependencyAcyclic(t, *createRouting("XY", t, p2), 2, &c));          // the dateline
    }
    Topology ring4 = Topology::ring(4, {}, 0); std::string c;
    CHECK(!channelDependencyAcyclic(ring4, *createRouting("Table", ring4, p1), 1, &c));
    CHECK(c.find("cycle:") != std::string::npos);
}
// T5: an escape network that ignores the torus dateline (the AdaptiveMinimal of 2026-11-13) is
// refused by the escape check; the fixed escape network passes
TEST("T5-ESCAPE-TORUS")
{
    Topology t = Topology::grid2d(4, 4, {}, 0, true);
    RoutingParams p2; p2.vcs_per_class = 2;
    auto xy = createRouting("XY", t, p2);
    auto oneVC = [&](int r, int, int, int d) { RouteContext c; c.router = r; c.dst_router = d; c.src_router = r; Route rt = xy->nextHop(c); rt.vc_mask = 1u; return rt; };
    std::string c;
    CHECK(!escapeDependencyAcyclic(t, 2, oneVC, &c));
    CHECK(c.find("wrap") != std::string::npos);
    RoutingParams p; p.vcs_per_class = 3; p.escape_vc = 0;
    auto am = createRouting("AdaptiveMinimal", t, p);
    CHECK(am->deadlockFreeOn(t)); CHECK(am->deadlockCycle(t, 3).empty());
    RoutingParams q; q.vcs_per_class = 2; q.escape_vc = 0;
    CHECK(errOf([&] { createRouting("AdaptiveMinimal", t, q); }).find("two escape VCs") != std::string::npos);
    Topology g = Topology::grid2d(4, 4, {}, 0);
    RoutingParams gp; gp.vcs_per_class = 2; gp.escape_vc = 1;
    CHECK(createRouting("AdaptiveMinimal", g, gp)->deadlockFreeOn(g));
}
// T5: AdaptiveMinimal offers its escape route as the fallback of every adaptive choice
TEST("T5-ADAPTIVE-FALLBACK")
{
    Topology g = Topology::grid2d(4, 4, {}, 0);
    RoutingParams p; p.vcs_per_class = 2; p.escape_vc = 1;
    auto am = createRouting("AdaptiveMinimal", g, p);
    RouteContext c; c.router = 0; c.dst_router = 15; c.src_router = 0; c.in_port = -1;
    Route rt = am->nextHop(c);
    CHECK_EQ(rt.vc_mask, 1u);                            // adaptive VC 0
    CHECK(rt.alt_port >= 0); CHECK_EQ(rt.alt_mask, 2u);  // escape VC 1 on the XY port
    CHECK_EQ(rt.alt_port, g.dirPort(0, 0, +1));
    // a packet that arrived over a link on the escape VC stays on it, with no fallback
    c.router = 1; c.in_port = g.dirPort(1, 0, -1); c.in_vc = 1;
    Route st = am->nextHop(c);
    CHECK_EQ(st.vc_mask, 2u); CHECK_EQ(st.alt_port, -1);
}

// ---------------------------------------------------------------- T4: features on the plug-ins
static int hopsTo(const RoutingAlgorithm &a, const Topology &t, int s, int d) { return (int)a.path(t, s, d).size() - 1; }
static int latticeDistance(const Lattice &L, int a, int b)
{
    auto x = L.coord(a), y = L.coord(b); int h = 0;
    for (int d = 0; d < L.rank(); d++) { int k = L.dims[d], v = std::abs(x[d] - y[d]); h += L.wrap[d] ? std::min(v, k - v) : v; }
    return h;
}
TEST("T4-LATTICE3D")
{
    Topology m = buildLattice3D(4, 3, 2, {}, 0, false);
    checkLattice(m);
    // an interior router has all six ports, in the order east, west, north, south, up, down
    Topology c = buildLattice3D(3, 3, 3, {}, 0, false);
    int mid = c.lattice()->index({1, 1, 1});
    std::vector<std::string> names; for (int p = 0; p < c.linkPorts(mid); p++) names.push_back(c.portName(mid, p));
    CHECK(names == std::vector<std::string>({"east", "west", "north", "south", "up", "down"}));
    checkLattice(buildLattice3D(3, 3, 3, {}, 0, true));
    checkLattice(buildLattice3D(4, 3, 5, {}, 0, true));
    CHECK(errOf([] { buildLattice3D(3, 2, 3, {}, 0, true); }).find("torus3d needs dims >= 3,3,3") != std::string::npos);
    RoutingParams p1; p1.vcs_per_class = 1; RoutingParams p2; p2.vcs_per_class = 2;
    auto xy = createRouting("XY", m, p1);
    for (int s = 0; s < m.routers(); s++) for (int d = 0; d < m.routers(); d++) if (s != d) CHECK_EQ(hopsTo(*xy, m, s, d), latticeDistance(*m.lattice(), s, d));
    std::string cyc;
    CHECK(channelDependencyAcyclic(m, *xy, 1, &cyc));
    Topology t = buildLattice3D(3, 3, 3, {}, 0, true);
    auto xyt = createRouting("XY", t, p2);
    for (int s = 0; s < t.routers(); s++) for (int d = 0; d < t.routers(); d++) if (s != d) CHECK_EQ(hopsTo(*xyt, t, s, d), latticeDistance(*t.lattice(), s, d));
    CHECK(channelDependencyAcyclic(t, *xyt, 2, &cyc));
    CHECK(errOf([&] { createRouting("XY", t, p1); }).find("torus3d with XY needs vcs_per_class >= 2") != std::string::npos);
    RoutingParams pa; pa.vcs_per_class = 2; pa.escape_vc = 1;
    CHECK(createRouting("AdaptiveMinimal", m, pa)->deadlockFreeOn(m));
    RoutingParams pt; pt.vcs_per_class = 3; pt.escape_vc = 1;
    CHECK(createRouting("AdaptiveMinimal", t, pt)->deadlockFreeOn(t));
}
TEST("T4-RING-XY")
{
    RoutingParams p1; p1.vcs_per_class = 1; RoutingParams p2; p2.vcs_per_class = 2;
    for (int k = 3; k <= 9; k++)
    {
        Topology r = Topology::ring(k, {}, 0);
        auto xy = createRouting("XY", r, p2);
        for (int s = 0; s < k; s++) for (int d = 0; d < k; d++) if (s != d) CHECK_EQ(hopsTo(*xy, r, s, d), latticeDistance(*r.lattice(), s, d));
        std::string cyc; CHECK(channelDependencyAcyclic(r, *xy, 2, &cyc));
        CHECK(xy->fixesVCPerPair());
        CHECK(errOf([&] { createRouting("XY", r, p1); }).find("ring with XY needs vcs_per_class >= 2") != std::string::npos);
    }
}
TEST("T4-TURN-MODELS")
{
    RoutingParams p1; p1.vcs_per_class = 1; RoutingParams p2; p2.vcs_per_class = 2;
    for (const char *name : {"WestFirst", "NorthLast", "NegativeFirst", "OddEven"})
        for (auto dims : std::vector<std::pair<int, int>>{{4, 4}, {5, 3}, {6, 6}, {2, 7}})
        {
            Topology g = Topology::grid2d(dims.first, dims.second, {}, 0);
            for (auto *pp : {&p1, &p2})
            {
                auto a = createRouting(name, g, *pp);
                std::string cyc;
                if (!channelDependencyAcyclic(g, *a, pp->vcs_per_class, &cyc)) octest::fail(std::string(name) + " has a dependency cycle: " + cyc);
                // in every state reachable from the source there is a candidate, and every candidate is minimal
                for (int s = 0; s < g.routers(); s++) for (int d = 0; d < g.routers(); d++)
                {
                    if (s == d) continue;
                    std::vector<int> todo{s}; std::vector<bool> seen(g.routers(), false); seen[s] = true;
                    while (!todo.empty())
                    {
                        int r = todo.back(); todo.pop_back();
                        if (r == d) continue;
                        RouteContext c; c.router = r; c.dst_router = d; c.src_router = s;
                        auto cands = a->candidates(c);
                        CHECK(!cands.empty());
                        for (auto &rt : cands)
                        {
                            CHECK(!g.isLocalPort(r, rt.out_port));
                            int n = g.link(r, rt.out_port).router;
                            CHECK_EQ(latticeDistance(*g.lattice(), n, d), latticeDistance(*g.lattice(), r, d) - 1);
                            if (!seen[n]) { seen[n] = true; todo.push_back(n); }
                        }
                    }
                }
                for (int s = 0; s < g.routers(); s++) for (int d = 0; d < g.routers(); d++) if (s != d) CHECK_EQ(hopsTo(*a, g, s, d), latticeDistance(*g.lattice(), s, d));
            }
            CHECK(errOf([&] { createRouting(name, Topology::grid2d(3, 3, {}, 0, true), p2); }).find("needs a grid2d topology") != std::string::npos);
        }
}
TEST("T4-WEIGHTED-TABLE")
{
    // 2x2 grid: 0 -> 3 has two shortest paths, via 1 (east, port 0) and via 2 (south, port 1)
    Topology g = Topology::grid2d(2, 2, {}, 0);
    auto plain = createRouting("Table", g, {});
    CHECK(routeIds(g, *plain, 0, 3) == std::vector<int>({0, 1, 3}));
    RoutingParams w; w.link_weight[0] = {1, 5};                     // make 0 -> 1 expensive
    auto wt = createRouting("Table", g, w);
    CHECK(routeIds(g, *wt, 0, 3) == std::vector<int>({0, 2, 3}));
    CHECK(routeIds(g, *wt, 0, 1) == std::vector<int>({0, 2, 3, 1})); // 3 hops of weight 1 beat one of 5
    RoutingParams bad; bad.link_weight[0] = {3, 2};
    CHECK(errOf([&] { createRouting("Table", g, bad); }).find("not a neighbour") != std::string::npos);
    RoutingParams zero; zero.link_weight[0] = {1, 0};
    CHECK(errOf([&] { createRouting("Table", g, zero); }).find(">= 1") != std::string::npos);
    // all weights 1 give the same table as the BFS
    RoutingParams ones; for (int r = 0; r < 4; r++) for (int p = 0; p < g.linkPorts(r); p++) { ones.link_weight[r].push_back(g.link(r, p).router); ones.link_weight[r].push_back(1); }
    auto one = createRouting("Table", g, ones);
    for (int s = 0; s < 4; s++) for (int d = 0; d < 4; d++) if (s != d) CHECK(routeIds(g, *one, s, d) == routeIds(g, *plain, s, d));
}

// walk one packet with its per-packet state; the VC taken at each hop is the lowest the mask allows
static std::vector<int> walk(const RoutingAlgorithm &a, const Topology &t, uint64_t seq, int s, int d)
{
    Packet p; p.seq = seq; p.src_router = s; p.dst_router = d; p.dst_endpoint = -1;
    a.onInject(p);
    std::vector<int> seq_r{s}; int r = s, in_port = -1, in_vc = 0;
    for (int guard = 0; guard < 4 * t.routers() + 8; guard++)
    {
        RouteContext c; c.router = r; c.in_port = in_port; c.in_vc = in_vc; c.dst_router = d; c.src_router = s; c.packet = &p;
        Route rt = a.nextHop(c);
        if (rt.out_port < 0 || t.isLocalPort(r, rt.out_port) || rt.out_port >= t.linkPorts(r)) return seq_r;   // arrived (no endpoint in this walk)
        int vc = 0; while (!(rt.vc_mask & (1u << vc))) vc++;
        auto f = t.link(r, rt.out_port); r = f.router; in_port = f.port; in_vc = vc; seq_r.push_back(r);
    }
    octest::fail("walk did not reach the destination");
    return seq_r;
}
TEST("T4-OBLIVIOUS")
{
    RoutingParams p2; p2.vcs_per_class = 2; RoutingParams p4; p4.vcs_per_class = 4;
    std::vector<Topology> ts = {Topology::grid2d(4, 4, {}, 0), buildLattice3D(3, 3, 2, {}, 0, false)};
    for (const char *name : {"O1TURN", "Valiant", "ROMM"})
        for (auto &t : ts)
        {
            if (std::string(name) == "O1TURN" && t.lattice()->rank() != 2) { CHECK(!errOf([&] { createRouting(name, t, p2); }).empty()); continue; }
            for (auto *pp : {&p2, &p4})
            {
                auto a = createRouting(name, t, *pp);
                std::string cyc;
                if (!channelDependencyAcyclic(t, *a, pp->vcs_per_class, &cyc)) octest::fail(std::string(name) + " has a dependency cycle: " + cyc);
                std::set<std::vector<int>> distinct;
                for (int s = 0; s < t.routers(); s++) for (int d = 0; d < t.routers(); d++)
                {
                    if (s == d) continue;
                    for (uint64_t q = 1; q <= 6; q++)
                    {
                        auto path = walk(*a, t, q * 7919 + s * 31 + d, s, d);
                        CHECK_EQ(path.back(), d);
                        int hops = (int)path.size() - 1, dist = latticeDistance(*t.lattice(), s, d);
                        if (std::string(name) != "Valiant") CHECK_EQ(hops, dist);            // O1TURN and ROMM are minimal
                        else CHECK(hops >= dist);
                        if (s == 0 && d == t.routers() - 1) distinct.insert(path);
                    }
                }
                CHECK(distinct.size() >= 2);                                               // the draw does vary the path
            }
        }
    Topology tor = Topology::grid2d(4, 4, {}, 0, true);
    CHECK(errOf([&] { createRouting("Valiant", tor, p2); }).find("without wrap links") != std::string::npos);
    Topology g = Topology::grid2d(4, 4, {}, 0); RoutingParams p1; p1.vcs_per_class = 1;
    CHECK(errOf([&] { createRouting("ROMM", g, p1); }).find("vcs_per_class >= 2") != std::string::npos);
    // the draw is a function of the packet and the seed only
    RoutingParams s7 = p2; s7.seed = 7;
    auto v1 = createRouting("Valiant", g, p2), v2 = createRouting("Valiant", g, p2), v7 = createRouting("Valiant", g, s7);
    int same = 0, diff = 0;
    for (uint64_t q = 1; q <= 50; q++) { CHECK(walk(*v1, g, q, 0, 15) == walk(*v2, g, q, 0, 15)); (walk(*v1, g, q, 0, 15) == walk(*v7, g, q, 0, 15) ? same : diff)++; }
    CHECK(diff > 0);
}
