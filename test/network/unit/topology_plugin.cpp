// Topology plug-in steps (docs/NoC_Topology_Plugin_Plan.md s5): T0 onward.
#include "testing.h"
#include "Routing.h"
#include "Topology.h"
#include "TopologyBuilder.h"
#include <map>
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
    CHECK(names == std::vector<std::string>({"custom", "grid2d", "torus2d", "ring", "star"}));
    Configurable::ParametersMap empty; KeyView v(empty);
    std::string e = errOf([&] { TopologyRegistry::builtin().create("hypercube", v, {}, 0); });
    CHECK(e.find("unknown topology 'hypercube' (custom, grid2d, torus2d, ring, star)") != std::string::npos);
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
