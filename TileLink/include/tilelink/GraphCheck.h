/*
 * File  :      GraphCheck.h
 * Rule 1 of §5 (deadlock freedom): the agent graph contains no cycle. The graph (§5.3) has
 * one vertex per agent and one directed edge per link, master to slave; it is a multigraph.
 * An agent that drives some of its links from independent state machines (§5.1, the DMA
 * example) declares them through Agent::decoupledGroups(): each group becomes its own
 * vertex, and the agent's remaining links stay with the agent's own vertex. Plan §3.6.
 *
 * GraphCheck runs at elaboration (Kernel::Observer). On a cycle it throws ConfigError with
 * the path "A -> B -> C -> A (links: L1, L2, L3)", which the kernel turns into fatal (exit 2).
 * It also exposes the topological order (plan §5.3: the Octopus class order follows it) and
 * a Graphviz export.
 */
#ifndef _TL_GRAPH_CHECK_H
#define _TL_GRAPH_CHECK_H
#include "Kernel.h"
#include <string>
#include <vector>

namespace octopus { namespace tl
{
    class GraphCheck : public Observer
    {
    public:
        // A vertex: an agent, or one decoupled group of an agent's links (§5.1).
        struct Vertex
        {
            Agent                   *agent;
            std::string              name;    // "DMA", or "DMA(L_dma)" for a group
            std::vector<std::string> links;   // the agent's links at this vertex
        };
        // An edge: one link, from the master's vertex to the slave's vertex (§5.3).
        struct Edge
        {
            int   from, to;
            Link *link;
        };

        GraphCheck() {}
        const char *name() const override { return "GraphCheck"; }
        void onElaborate(Kernel &k) override;

        // Builds the vertices and edges from the kernel's agents and links. Throws ConfigError
        // for a decoupled group that names a link the agent does not own, or a link in two
        // groups. Callable before elaboration, for the tests and the dot export.
        void build(const Kernel &k);
        const std::vector<Vertex> &vertices() const { return m_vertices; }
        const std::vector<Edge>   &edges() const { return m_edges; }

        // "" when the graph is acyclic; otherwise the first cycle found, as
        // "A -> B -> A (links: L1, L2)". Deterministic: vertices in agent order, edges in link
        // order.
        std::string findCycle() const;
        // Topological order of the vertices (Kahn, lowest index first among the ready ones):
        // every edge goes from an earlier to a later position. Empty when the graph has a
        // cycle. agentOrder() lists the agents in the order of their first vertex.
        std::vector<int>     topologicalOrder() const;
        std::vector<Agent *> agentOrder() const;
        // Graphviz: digraph with one node per vertex (agents with groups in a cluster) and
        // one edge per link labelled with the link's name and level.
        std::string dot() const;

    private:
        int vertexOf(Agent *a, const std::string &link_name) const;

        std::vector<Vertex> m_vertices;
        std::vector<Edge>   m_edges;
        std::vector<int>    m_agent_vertex;   // agent id -> its own (non-group) vertex, or -1
    };
}}
#endif /* _TL_GRAPH_CHECK_H */
