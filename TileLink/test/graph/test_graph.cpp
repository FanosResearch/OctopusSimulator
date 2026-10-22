/*
 * File  :      test_graph.cpp
 * TL-GRAPH (plan §6): rule 1 of §5, the agent graph of §5.3. A 2-agent cycle; a 3-agent cycle
 * through a crossbar-like agent; the DMA example of §5.1 coupled (refused, path printed) and
 * decoupled (accepted); a diamond that is not a cycle; a multigraph with two links between
 * one pair; the topological order; the dot export; the config keys. The refusals are fatal
 * (exit 2), so they run in a child process (Subprocess.h).
 */
#include "Subprocess.h"
#include "tilelink/GraphCheck.h"
#include <memory>

using namespace octopus::tl;

namespace
{
    // An agent that does nothing on any link in any role (the graph only needs the links).
    class Node : public Agent
    {
    public:
        explicit Node(const std::string &n) : Agent(n) {}
        Level capability(const Link &, Role) const override { return Level::C; }
        void offer(uint64_t) override {}
        void accept(uint64_t) override {}
        void commit(uint64_t) override {}
    };

    struct Sys
    {
        Kernel k;
        Sys() : k(KernelOptions{0, 1, DataMode::None}) {}
        Agent &node(const std::string &n) { return k.add(std::unique_ptr<Agent>(new Node(n))); }
        Link &link(const std::string &n, Agent &m, Agent &s)
        {
            LinkParams p; p.level = Level::UH;
            return k.connect(n, p, m, s);
        }
    };

    // The §5.1 DMA engine: CPU -> Xbar -> {Mem, DMA control}; DMA master -> Xbar.
    struct DmaSys : Sys
    {
        Agent &cpu, &xbar, &mem, &dma;
        DmaSys() : cpu(node("CPU")), xbar(node("Xbar")), mem(node("Mem")), dma(node("DMA"))
        {
            link("L_cpu", cpu, xbar);
            link("L_mem", xbar, mem);
            link("L_ctrl", xbar, dma);
            link("L_dma", dma, xbar);
        }
    };

    // Every edge goes from an earlier to a later vertex of the order.
    bool orderValid(const GraphCheck &g)
    {
        std::vector<int> order = g.topologicalOrder();
        if (order.size() != g.vertices().size()) return false;
        std::vector<int> pos(order.size());
        for (size_t i = 0; i < order.size(); i++) pos[size_t(order[i])] = int(i);
        for (auto &e : g.edges()) if (pos[size_t(e.from)] >= pos[size_t(e.to)]) return false;
        return true;
    }

    // The DMA system from a config, through Kernel::build (graph_check(i) defaults to 1).
    void buildDmaFromConfig(Kernel &k, const std::string &decoupled)
    {
        Config cfg;
        cfg.set("reset_cycles(i),0");
        cfg.set("agent[0].type(s),NullMaster"); cfg.set("agent[0].name(s),CPU");
        cfg.set("agent[1].type(s),NullMaster"); cfg.set("agent[1].name(s),Xbar");
        cfg.set("agent[2].type(s),NullSlave");  cfg.set("agent[2].name(s),Mem");
        cfg.set("agent[3].type(s),NullMaster"); cfg.set("agent[3].name(s),DMA");
        if (!decoupled.empty()) cfg.set("agent[3].decoupled_groups(s)," + decoupled);
        cfg.set("link[*].level(s),UH");
        cfg.set("link[0].name(s),L_cpu");  cfg.set("link[0].master(s),CPU");  cfg.set("link[0].slave(s),Xbar");
        cfg.set("link[1].name(s),L_mem");  cfg.set("link[1].master(s),Xbar"); cfg.set("link[1].slave(s),Mem");
        cfg.set("link[2].name(s),L_ctrl"); cfg.set("link[2].master(s),Xbar"); cfg.set("link[2].slave(s),DMA");
        cfg.set("link[3].name(s),L_dma");  cfg.set("link[3].master(s),DMA");  cfg.set("link[3].slave(s),Xbar");
        k.build(cfg, AgentRegistry::withBuiltins());
    }
}

// ---- child cases: each must end in fatal (exit 2) or return (exit 0)

CHILD_CASE(cycle2)
{
    Sys s;
    Agent &a = s.node("A"), &b = s.node("B");
    s.link("L1", a, b);
    s.link("L2", b, a);
    GraphCheck g;
    s.k.addObserver(&g);
    s.k.elaborate();
}

CHILD_CASE(cycle3)
{
    Sys s;
    Agent &m = s.node("M"), &x = s.node("Xbar"), &sl = s.node("S");
    s.link("L1", m, x);
    s.link("L2", x, sl);
    s.link("L3", sl, m);   // the slave masters a link back to the master: a cycle through the crossbar
    GraphCheck g;
    s.k.addObserver(&g);
    s.k.elaborate();
}

CHILD_CASE(dma_coupled)
{
    DmaSys s;
    GraphCheck g;
    s.k.addObserver(&g);
    s.k.elaborate();
}

CHILD_CASE(dma_coupled_config)
{
    Kernel k;
    buildDmaFromConfig(k, "");
    k.elaborate();
}

CHILD_CASE(dma_decoupled_config)
{
    Kernel k;
    buildDmaFromConfig(k, "L_ctrl;L_dma");
    k.elaborate();   // accepted: returns, exit 0
}

CHILD_CASE(bad_group)
{
    DmaSys s;
    s.dma.setDecoupledGroups({{"L_cpu"}});   // not one of DMA's links
    GraphCheck g;
    s.k.addObserver(&g);
    s.k.elaborate();
}

// ---- the suite

TEST(two_agent_cycle)
{
    // §5 rule 1: A -> B -> A is a cycle, and the path names both links.
    Sys s;
    Agent &a = s.node("A"), &b = s.node("B");
    s.link("L1", a, b);
    s.link("L2", b, a);
    GraphCheck g;
    g.build(s.k);
    CHECK_EQ(g.vertices().size(), size_t(2));
    CHECK_EQ(g.edges().size(), size_t(2));
    CHECK_EQ(g.findCycle(), std::string("A -> B -> A (links: L1, L2)"));
    CHECK(g.topologicalOrder().empty());
    CHECK(g.agentOrder().empty());
    // elaboration refuses it: exit 2 with the path
    tltest::ChildResult r = tltest::runCase("cycle2");
    CHECK_EQ(r.status, 2);
    CHECK_EQ(r.firstErrLine(), std::string("TileLink: agent graph has a cycle (rule 1, §5): A -> B -> A (links: L1, L2)"));
}

TEST(three_agent_cycle_through_crossbar)
{
    Sys s;
    Agent &m = s.node("M"), &x = s.node("Xbar"), &sl = s.node("S");
    s.link("L1", m, x);
    s.link("L2", x, sl);
    s.link("L3", sl, m);
    GraphCheck g;
    g.build(s.k);
    CHECK_EQ(g.findCycle(), std::string("M -> Xbar -> S -> M (links: L1, L2, L3)"));
    tltest::ChildResult r = tltest::runCase("cycle3");
    CHECK_EQ(r.status, 2);
    CHECK(r.firstErrLine().find("M -> Xbar -> S -> M (links: L1, L2, L3)") != std::string::npos);
}

TEST(dma_coupled_refused)
{
    // §5.1: a DMA engine that withholds control answers until the copy completes is one agent
    // on both links, and Xbar -> DMA -> Xbar is a cycle.
    DmaSys s;
    GraphCheck g;
    g.build(s.k);
    CHECK_EQ(g.vertices().size(), size_t(4));
    CHECK_EQ(g.findCycle(), std::string("Xbar -> DMA -> Xbar (links: L_ctrl, L_dma)"));
    tltest::ChildResult r = tltest::runCase("dma_coupled");
    CHECK_EQ(r.status, 2);
    CHECK_EQ(r.firstErrLine(), std::string("TileLink: agent graph has a cycle (rule 1, §5): Xbar -> DMA -> Xbar (links: L_ctrl, L_dma)"));
    // the same through Kernel::build, where graph_check(i) defaults to 1
    tltest::ChildResult rc = tltest::runCase("dma_coupled_config");
    CHECK_EQ(rc.status, 2);
    CHECK(rc.firstErrLine().find("Xbar -> DMA -> Xbar (links: L_ctrl, L_dma)") != std::string::npos);
}

TEST(dma_decoupled_accepted)
{
    // §5.1: the control interface answers without waiting for the DMA mastering, so the two
    // links are two agents; the vertex splits and the graph is acyclic.
    DmaSys s;
    s.dma.setDecoupledGroups({{"L_ctrl"}, {"L_dma"}});
    GraphCheck g;
    g.build(s.k);
    CHECK_EQ(g.vertices().size(), size_t(5));   // CPU, Xbar, Mem, DMA(L_ctrl), DMA(L_dma); no plain DMA vertex
    bool plain_dma = false;
    for (auto &v : g.vertices()) if (v.name == "DMA") plain_dma = true;
    CHECK(!plain_dma);
    CHECK_EQ(g.vertices()[3].name, std::string("DMA(L_ctrl)"));
    CHECK_EQ(g.vertices()[4].name, std::string("DMA(L_dma)"));
    CHECK_EQ(g.findCycle(), std::string(""));
    CHECK(orderValid(g));
    std::vector<Agent *> order = g.agentOrder();
    CHECK_EQ(order.size(), size_t(4));
    if (order.size() == 4)
    {
        CHECK(order[0] == &s.cpu || order[0] == &s.dma);     // the two sources first
        CHECK(order[3] == &s.mem || order[3] == &s.dma);     // Mem and DMA(L_ctrl) are sinks
    }
    s.k.elaborate();   // accepted in-process (no observer here; the child below has one)
    // only the master link decoupled: the control link stays with the agent's own vertex
    DmaSys s2;
    s2.dma.setDecoupledGroups({{"L_dma"}});
    GraphCheck g2;
    g2.build(s2.k);
    CHECK_EQ(g2.vertices().size(), size_t(5));
    CHECK_EQ(g2.vertices()[3].name, std::string("DMA"));
    CHECK_EQ(g2.vertices()[3].links.size(), size_t(1));
    CHECK_EQ(g2.findCycle(), std::string(""));
    // through the config key agent[k].decoupled_groups(s)
    tltest::ChildResult r = tltest::runCase("dma_decoupled_config");
    CHECK_EQ(r.status, 0);
    CHECK(r.err.empty());
    Kernel k;
    buildDmaFromConfig(k, "L_ctrl ; L_dma");
    std::vector<std::vector<std::string>> groups = k.agent("DMA")->decoupledGroups();
    CHECK_EQ(groups.size(), size_t(2));
    if (groups.size() == 2) { CHECK_EQ(groups[0][0], std::string("L_ctrl")); CHECK_EQ(groups[1][0], std::string("L_dma")); }
    Kernel k2;
    buildDmaFromConfig(k2, "L_ctrl+L_dma");   // one group of both links: still one vertex, still a cycle
    GraphCheck g3;
    g3.build(k2);
    CHECK_EQ(g3.vertices().size(), size_t(4));
    CHECK_EQ(g3.findCycle(), std::string("Xbar -> DMA(L_ctrl+L_dma) -> Xbar (links: L_ctrl, L_dma)"));
}

TEST(decoupled_group_errors)
{
    DmaSys s;
    s.dma.setDecoupledGroups({{"L_cpu"}});
    GraphCheck g;
    bool thrown = false;
    try { g.build(s.k); }
    catch (ConfigError &e) { thrown = true; CHECK(std::string(e.what()).find("not one of its links") != std::string::npos); }
    CHECK(thrown);
    s.dma.setDecoupledGroups({{"L_ctrl"}, {"L_ctrl"}});
    thrown = false;
    try { g.build(s.k); }
    catch (ConfigError &e) { thrown = true; CHECK(std::string(e.what()).find("two decoupled groups") != std::string::npos); }
    CHECK(thrown);
    tltest::ChildResult r = tltest::runCase("bad_group");
    CHECK_EQ(r.status, 2);
    CHECK(r.firstErrLine().find("decoupled group names link L_cpu") != std::string::npos);
}

TEST(diamond_is_not_a_cycle)
{
    // M -> X1 -> S and M -> X2 -> S share M and S; no edge returns, so no cycle.
    Sys s;
    Agent &m = s.node("M"), &x1 = s.node("X1"), &x2 = s.node("X2"), &sl = s.node("S");
    s.link("L1", m, x1);
    s.link("L2", m, x2);
    s.link("L3", x1, sl);
    s.link("L4", x2, sl);
    GraphCheck g;
    g.build(s.k);
    CHECK_EQ(g.findCycle(), std::string(""));
    CHECK(orderValid(g));
    std::vector<Agent *> order = g.agentOrder();
    CHECK_EQ(order.size(), size_t(4));
    if (order.size() == 4) { CHECK(order[0] == &m); CHECK(order[3] == &sl); }
    s.k.addObserver(&g);
    s.k.elaborate();
}

TEST(multigraph)
{
    // §5.3: two links between the same pair in the same direction are legal; one in each
    // direction is a cycle.
    Sys s;
    Agent &m = s.node("M"), &sl = s.node("S");
    s.link("L1", m, sl);
    s.link("L2", m, sl);
    GraphCheck g;
    g.build(s.k);
    CHECK_EQ(g.edges().size(), size_t(2));
    CHECK_EQ(g.findCycle(), std::string(""));
    CHECK(orderValid(g));
    s.link("L3", sl, m);
    g.build(s.k);
    CHECK_EQ(g.findCycle(), std::string("M -> S -> M (links: L1, L3)"));
}

TEST(dot_export)
{
    DmaSys s;
    s.dma.setDecoupledGroups({{"L_ctrl"}, {"L_dma"}});
    GraphCheck g;
    g.build(s.k);
    std::string d = g.dot();
    CHECK(d.find("digraph agents {") == 0);
    CHECK(d.find("v0 [label=\"CPU\"]") != std::string::npos);
    CHECK(d.find("subgraph cluster_3") != std::string::npos);          // the DMA's two vertices in a cluster
    CHECK(d.find("[label=\"DMA(L_dma)\"]") != std::string::npos);
    CHECK(d.find("v0 -> v1 [label=\"L_cpu TL-UH\"]") != std::string::npos);
    CHECK(d.find("v4 -> v1 [label=\"L_dma TL-UH\"]") != std::string::npos);
    CHECK(d.rfind("}\n") == d.size() - 2);
}

TEST(graph_check_config_key)
{
    // graph_check(i)=0 leaves the checker out; the default registers it.
    Config on;
    on.set("agent[0].type(s),NullMaster"); on.set("agent[0].name(s),m");
    on.set("agent[1].type(s),NullSlave");  on.set("agent[1].name(s),s");
    on.set("link[0].master(s),m"); on.set("link[0].slave(s),s");
    Kernel k1;
    k1.build(on, AgentRegistry::withBuiltins());
    CHECK(k1.observer("GraphCheck") != nullptr);
    Config off = on;
    off.set("graph_check(i),0");
    Kernel k2;
    k2.build(off, AgentRegistry::withBuiltins());
    CHECK(k2.observer("GraphCheck") == nullptr);
}

TL_TEST_MAIN_WITH_CHILDREN("test_graph")
