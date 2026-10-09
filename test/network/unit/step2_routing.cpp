// Step 2 tests: routing algorithms, the routing-level deadlock check, ordering (plan §5).
#include "testing.h"
#include "Routing.h"
#include <cstdlib>
using namespace octopus;
using namespace octopus::net;

static std::string errOf(std::function<void()> f) { try { f(); } catch (ConfigError &e) { return e.what(); } return ""; }
static int manhattan(const Topology &t, int a, int b) { return std::abs(t.coord(a).first - t.coord(b).first) + std::abs(t.coord(a).second - t.coord(b).second); }
static int hopsOf(const RoutingAlgorithm &r, const Topology &t, int s, int d) { return (int)r.path(t, s, d).size() - 1; }  // last hop is the local port

// an independent XY reference: step X to the column, then Y; returns the router sequence
static std::vector<int> xyRef(const Topology &t, int s, int d)
{
    std::vector<int> seq{s}; int x = t.coord(s).first, y = t.coord(s).second;
    while (x != t.coord(d).first) { x += (t.coord(d).first > x) ? 1 : -1; seq.push_back(y * t.dimX() + x); }
    while (y != t.coord(d).second) { y += (t.coord(d).second > y) ? 1 : -1; seq.push_back(y * t.dimX() + x); }
    return seq;
}
static std::map<int, int> grid4 = {{0, 0}, {1, 3}, {2, 12}, {3, 15}, {10, 5}};

TEST("S2-TABLE-BFS")
{
    Topology t = Topology::grid2d(4, 4, grid4, 0);
    auto r = createRouting("Table", t, {});
    for (int s = 0; s < 16; s++) for (int d = 0; d < 16; d++) if (s != d) CHECK_EQ(hopsOf(*r, t, s, d), manhattan(t, s, d));
}
TEST("S2-TABLE-TIES")
{
    Topology t = Topology::grid2d(4, 4, grid4, 0);
    auto r = createRouting("Table", t, {});
    // from 0 to 5: east (port 0) and south (port 1) are both shortest; lowest port = east
    RouteContext c; c.router = 0; c.dst_router = 5; CHECK_EQ(r->nextHop(c).out_port, 0);
    // from 5 to 0: west (port 1) and north (port 2) tie; lowest = west
    c.router = 5; c.dst_router = 0; CHECK_EQ(r->nextHop(c).out_port, 1);
    // from 15 to 0: west (port 0) and north (port 1); lowest = west
    c.router = 15; c.dst_router = 0; CHECK_EQ(r->nextHop(c).out_port, 0);
}
TEST("S2-TABLE-CSV")
{
    Topology t = Topology::grid2d(4, 4, grid4, 0);
    RoutingParams p; p.routing_table[5] = {0, 2, 15, 3};     // to 0 go north (port 2), to 15 go south (port 3)
    auto r = createRouting("Table", t, p);
    RouteContext c; c.router = 5; c.dst_router = 0; CHECK_EQ(r->nextHop(c).out_port, 2);
    c.dst_router = 15; CHECK_EQ(r->nextHop(c).out_port, 3);
    c.dst_router = 7; CHECK_EQ(r->nextHop(c).out_port, 0);  // untouched entry: BFS east
    for (int d = 0; d < 16; d++) if (d != 5) CHECK(hopsOf(*r, t, 5, d) == manhattan(t, 5, d));   // still minimal
}
TEST("S2-TABLE-BADPORT")
{
    Topology t = Topology::grid2d(4, 4, grid4, 0);
    RoutingParams p; p.routing_table[0] = {15, 7};
    std::string e = errOf([&] { createRouting("Table", t, p); });
    CHECK(e.find("router 0") != std::string::npos); CHECK(e.find("port 7") != std::string::npos);
}
TEST("S2-XY")
{
    Topology t = Topology::grid2d(4, 4, grid4, 0);
    auto r = createRouting("XY", t, {});
    int pairs = 0;
    for (int s = 0; s < 16; s++) for (int d = 0; d < 16; d++)
    {
        if (s == d) continue; pairs++;
        auto hops = r->path(t, s, d); auto ref = xyRef(t, s, d);
        CHECK_EQ((int)hops.size(), (int)ref.size());
        for (size_t i = 0; i < ref.size(); i++) CHECK_EQ(hops[i].first, ref[i]);
    }
    CHECK_EQ(pairs, 240);
}
TEST("S2-XY-LOCAL")
{
    Topology t = Topology::grid2d(4, 4, grid4, 0);
    auto r = createRouting("XY", t, {});
    Packet p; p.dst_endpoint = 10;
    RouteContext c; c.router = 5; c.dst_router = 5; c.packet = &p;
    CHECK_EQ(r->nextHop(c).out_port, t.localPort(10));
    CHECK_EQ(t.isLocalPort(5, r->nextHop(c).out_port), true);
}
TEST("S2-XY-TORUS-VC")
{
    Topology t = Topology::grid2d(4, 4, {}, 0, true);
    RoutingParams p; p.vcs_per_class = 2;
    auto r = createRouting("XY", t, p);
    // 0 -> 3 in X: shorter way is west across the wrap (1 hop) -> VC 1 on the X leg
    RouteContext c; c.router = 0; c.src_router = 0; c.dst_router = 3;
    Route rt = r->nextHop(c); CHECK_EQ(rt.vc_mask, 0b10u); CHECK_EQ(t.portName(0, rt.out_port), std::string("west(wrap)"));
    // 0 -> 1 in X: direct, no wrap -> VC 0
    c.dst_router = 1; CHECK_EQ(r->nextHop(c).vc_mask, 0b01u);
    // 0 -> 12 in Y: wrap north -> VC 1 ; 0 -> 4: direct south -> VC 0
    c.dst_router = 12; CHECK_EQ(r->nextHop(c).vc_mask, 0b10u);
    c.dst_router = 4; CHECK_EQ(r->nextHop(c).vc_mask, 0b01u);
    // 0 -> 15: X wraps (0->3), Y wraps (0->3): every hop VC 1
    for (auto h : r->path(t, 0, 15)) { RouteContext cc; cc.router = h.first; cc.src_router = 0; cc.dst_router = 15; if (!t.isLocalPort(h.first, h.second)) CHECK_EQ(r->nextHop(cc).vc_mask, 0b10u); }
}
TEST("S2-XY-TORUS-1VC")
{
    Topology t = Topology::grid2d(4, 4, {}, 0, true);
    std::string e = errOf([&] { createRouting("XY", t, {}); });
    CHECK(e.find("dateline") != std::string::npos);
}
TEST("S2-WF-TURNS")
{
    Topology t = Topology::grid2d(4, 4, grid4, 0);
    auto r = createRouting("WestFirst", t, {});
    for (int s = 0; s < 16; s++) for (int d = 0; d < 16; d++)
    {
        if (s == d) continue;
        auto hops = r->path(t, s, d);
        CHECK_EQ((int)hops.size() - 1, manhattan(t, s, d));
        bool moved_ns = false;
        for (size_t i = 0; i + 1 < hops.size(); i++)
        {
            std::string pn = t.portName(hops[i].first, hops[i].second);
            if (pn == "west") CHECK_MSG(!moved_ns, "west after a north/south move on " + octest::str(s) + "->" + octest::str(d));
            if (pn == "north" || pn == "south") moved_ns = true;
        }
    }
    CHECK_EQ(r->ordering() == Ordering::NONE, true);
}
TEST("S2-AM-MASK")
{
    Topology t = Topology::grid2d(4, 4, grid4, 0);
    RoutingParams p; p.vcs_per_class = 2; p.escape_vc = 0;
    auto r = createRouting("AdaptiveMinimal", t, p);
    RouteContext c; c.router = 0; c.dst_router = 15; c.in_vc = 1;
    CHECK_EQ(r->nextHop(c).vc_mask, 0b10u);            // adaptive: all but the escape VC
    c.in_vc = 0; CHECK_EQ(r->nextHop(c).vc_mask, 0b01u); // on the escape VC: stays there, XY
    CHECK_EQ(r->needsEscapeVC(), true);
    CHECK(!errOf([&] { RoutingParams q; q.vcs_per_class = 1; q.escape_vc = 0; createRouting("AdaptiveMinimal", t, q); }).empty());
    CHECK(!errOf([&] { RoutingParams q; q.vcs_per_class = 2; createRouting("AdaptiveMinimal", t, q); }).empty());
}
TEST("S2-CDG-CYCLE")
{
    // a 4-router ring with a table that always turns clockwise: 0->1->2->3->0, every pair
    Topology t = Topology::ring(4, {{0, 0}, {1, 1}, {2, 2}, {3, 3}}, 0);
    RoutingParams p; for (int r = 0; r < 4; r++) { for (int d = 0; d < 4; d++) if (d != r) { p.routing_table[r].push_back(d); p.routing_table[r].push_back(0); } }
    std::string e = errOf([&] { auto r = createRouting("Table", t, p); std::string c; CHECK_EQ(channelDependencyAcyclic(t, *r, 1, &c), false); CHECK(c.find("cycle") != std::string::npos); CHECK(c.find("clockwise") != std::string::npos); throw ConfigError(c); });
    CHECK(e.find("router 0") != std::string::npos); CHECK(e.find("router 3") != std::string::npos);
}
TEST("S2-CDG-ESCAPE")
{
    Topology t = Topology::ring(4, {{0, 0}, {1, 1}, {2, 2}, {3, 3}}, 0);
    RoutingParams p; p.vcs_per_class = 2; p.escape_vc = 1;
    for (int r = 0; r < 4; r++) for (int d = 0; d < 4; d++) if (d != r) { p.routing_table[r].push_back(d); p.routing_table[r].push_back(0); }
    auto r = createRouting("Table", t, p);
    // the adaptive VC (0) alone is cyclic; the escape VC's graph is what the check must judge:
    // on a ring the table on VC 1 is the same cyclic table, so this configuration is refused too
    std::string c; CHECK_EQ(channelDependencyAcyclic(t, *r, 1, &c), false);
    // the BFS table on an even ring is cyclic as well (every tie resolves clockwise, the classic
    // ring deadlock without a dateline); the checker must say so, naming the ring
    auto bfs = createRouting("Table", t, {}); CHECK_EQ(channelDependencyAcyclic(t, *bfs, 1, &c), false);
    CHECK(c.find("clockwise") != std::string::npos);
    // a 3-ring has no ties: 0->1 clockwise, 0->2 counter-clockwise; no cycle
    Topology t3 = Topology::ring(3, {{0, 0}, {1, 1}, {2, 2}}, 0); auto b3 = createRouting("Table", t3, {});
    CHECK_MSG(channelDependencyAcyclic(t3, *b3, 1, &c), c);
}
TEST("S2-CDG-SHIPPED")
{
    std::string c;
    { Topology t = Topology::grid2d(4, 4, grid4, 0); auto r = createRouting("Table", t, {}); CHECK(channelDependencyAcyclic(t, *r, 1, &c)); auto x = createRouting("XY", t, {}); CHECK(channelDependencyAcyclic(t, *x, 1, &c)); }
    { Topology t = Topology::grid2d(4, 4, {}, 0, true); RoutingParams p; p.vcs_per_class = 2; auto x = createRouting("XY", t, p); CHECK_MSG(channelDependencyAcyclic(t, *x, 2, &c), c); }
    { Topology t = Topology::ring(6, {}, 0); auto r = createRouting("Table", t, {}); CHECK_EQ(channelDependencyAcyclic(t, *r, 1, &c), false); }   // even ring: needs a dateline (see report)
    { Topology t = Topology::ring(5, {}, 0); auto r = createRouting("Table", t, {}); CHECK_EQ(channelDependencyAcyclic(t, *r, 1, &c), false); }  // any ring with 2-hop paths: cyclic without a dateline
    { Topology t = Topology::star({0, 1, 2}, 0); auto r = createRouting("Table", t, {}); CHECK(channelDependencyAcyclic(t, *r, 1, &c)); }
}
TEST("S2-ORDERING")
{
    Topology t = Topology::grid2d(4, 4, grid4, 0);
    CHECK(createRouting("Table", t, {})->ordering() == Ordering::PAIR_PER_CLASS);
    CHECK(createRouting("XY", t, {})->ordering() == Ordering::PAIR_PER_CLASS);
    CHECK(createRouting("WestFirst", t, {})->ordering() == Ordering::NONE);
    RoutingParams p; p.vcs_per_class = 2; p.escape_vc = 0;
    CHECK(createRouting("AdaptiveMinimal", t, p)->ordering() == Ordering::NONE);
    CHECK(!errOf([&] { createRouting("TDM_XY", t, {}); }).empty());
    CHECK(errOf([&] { createRouting("TDM_XY", t, {}); }).find("deferred") != std::string::npos);
}
TEST("S2-REQUIRES")
{
    Topology t = Topology::grid2d(4, 4, grid4, 0);
    auto wf = createRouting("WestFirst", t, {}), xy = createRouting("XY", t, {});
    CHECK(!errOf([&] { checkOrderingRequirement("pair_per_class", *wf, t, 1); }).empty());
    CHECK(errOf([&] { checkOrderingRequirement("pair_per_class", *xy, t, 1); }).empty());
    CHECK(!errOf([&] { checkOrderingRequirement("pair_per_class", *xy, t, 2); }).empty());
    CHECK(errOf([&] { checkOrderingRequirement("none", *wf, t, 2); }).empty());
    Topology tt = Topology::grid2d(4, 4, {}, 0, true); RoutingParams p; p.vcs_per_class = 2; auto txy = createRouting("XY", tt, p);
    CHECK(errOf([&] { checkOrderingRequirement("pair_per_class", *txy, tt, 2); }).empty());   // dateline fixes the VC
}
