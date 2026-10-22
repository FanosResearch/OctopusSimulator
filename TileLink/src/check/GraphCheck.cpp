/*
 * File  :      GraphCheck.cpp
 * The agent graph of §5.3 and rule 1 of §5: no cycle. Plan §3.6.
 */
#include "tilelink/GraphCheck.h"
#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace octopus { namespace tl
{
    void GraphCheck::build(const Kernel &k)
    {
        m_vertices.clear();
        m_edges.clear();
        m_agent_vertex.assign(k.agents().size(), -1);
        // One vertex per agent, then one per declared decoupled group (§5.1): a group names
        // links of that agent; a link in no group stays with the agent's own vertex. The
        // agent's own vertex exists only if some link stays with it.
        std::vector<std::vector<std::vector<std::string>>> groups(k.agents().size());
        for (auto &a : k.agents())
        {
            groups[size_t(a->id())] = a->decoupledGroups();
            std::set<std::string> seen;
            for (auto &g : groups[size_t(a->id())])
                for (auto &ln : g)
                {
                    bool own = false;
                    for (auto &lr : a->links()) if (lr.link->name() == ln) own = true;
                    if (!own) throw ConfigError("agent " + a->name() + ": decoupled group names link " + ln + " which is not one of its links");
                    if (!seen.insert(ln).second) throw ConfigError("agent " + a->name() + ": link " + ln + " is in two decoupled groups");
                }
        }
        for (auto &a : k.agents())
        {
            const auto &gs = groups[size_t(a->id())];
            std::set<std::string> grouped;
            for (auto &g : gs) grouped.insert(g.begin(), g.end());
            std::vector<std::string> rest;
            for (auto &lr : a->links()) if (!grouped.count(lr.link->name())) rest.push_back(lr.link->name());
            if (!rest.empty() || gs.empty())
            {
                m_agent_vertex[size_t(a->id())] = int(m_vertices.size());
                m_vertices.push_back({a.get(), a->name(), rest});
            }
            for (auto &g : gs)
            {
                std::string nm = a->name() + "(";
                for (size_t i = 0; i < g.size(); i++) nm += (i ? "+" : "") + g[i];
                m_vertices.push_back({a.get(), nm + ")", g});
            }
        }
        // §5.3: every link is a directed edge from master to slave.
        for (auto &l : k.links())
            m_edges.push_back({vertexOf(l->master(), l->name()), vertexOf(l->slave(), l->name()), l.get()});
    }

    int GraphCheck::vertexOf(Agent *a, const std::string &link_name) const
    {
        for (size_t v = 0; v < m_vertices.size(); v++)
            if (m_vertices[v].agent == a)
                for (auto &ln : m_vertices[v].links) if (ln == link_name) return int(v);
        return m_agent_vertex[size_t(a->id())];   // unreachable once build() has run: every link is listed somewhere
    }

    std::string GraphCheck::findCycle() const
    {
        // Iterative DFS with three colours; a back edge closes a cycle, and the path from the
        // target up the DFS stack is the cycle. Vertices in agent order, edges in link order,
        // so the report is deterministic.
        const int n = int(m_vertices.size());
        std::vector<std::vector<int>> out;              // outgoing edge indices per vertex
        out.resize(size_t(n));
        for (int e = 0; e < int(m_edges.size()); e++) out[size_t(m_edges[size_t(e)].from)].push_back(e);
        std::vector<int> colour(size_t(n), 0);          // 0 white, 1 grey (on the stack), 2 black
        for (int root = 0; root < n; root++)
        {
            if (colour[size_t(root)]) continue;
            struct Frame { int v; size_t next; int via; };   // via = the edge that reached v
            std::vector<Frame> stack{{root, 0, -1}};
            colour[size_t(root)] = 1;
            while (!stack.empty())
            {
                Frame &f = stack.back();
                if (f.next == out[size_t(f.v)].size()) { colour[size_t(f.v)] = 2; stack.pop_back(); continue; }
                int e = out[size_t(f.v)][f.next++];
                int w = m_edges[size_t(e)].to;
                if (colour[size_t(w)] == 0) { colour[size_t(w)] = 1; stack.push_back({w, 0, e}); }
                else if (colour[size_t(w)] == 1)
                {
                    // the cycle: w ... f.v -> w
                    size_t start = 0;
                    while (stack[start].v != w) start++;
                    std::string path, links;
                    for (size_t i = start; i < stack.size(); i++)
                    {
                        path += m_vertices[size_t(stack[i].v)].name + " -> ";
                        if (i > start) links += m_edges[size_t(stack[i].via)].link->name() + ", ";
                    }
                    path += m_vertices[size_t(w)].name;
                    links += m_edges[size_t(e)].link->name();
                    return path + " (links: " + links + ")";
                }
            }
        }
        return "";
    }

    std::vector<int> GraphCheck::topologicalOrder() const
    {
        const int n = int(m_vertices.size());
        std::vector<int> indeg(size_t(n), 0), order;
        for (auto &e : m_edges) indeg[size_t(e.to)]++;
        std::set<int> ready;
        for (int v = 0; v < n; v++) if (indeg[size_t(v)] == 0) ready.insert(v);
        while (!ready.empty())
        {
            int v = *ready.begin();
            ready.erase(ready.begin());
            order.push_back(v);
            for (auto &e : m_edges) if (e.from == v && --indeg[size_t(e.to)] == 0) ready.insert(e.to);
        }
        if (int(order.size()) != n) order.clear();   // a cycle: no order exists
        return order;
    }

    std::vector<Agent *> GraphCheck::agentOrder() const
    {
        std::vector<Agent *> agents;
        for (int v : topologicalOrder())
        {
            Agent *a = m_vertices[size_t(v)].agent;
            if (std::find(agents.begin(), agents.end(), a) == agents.end()) agents.push_back(a);
        }
        return agents;
    }

    std::string GraphCheck::dot() const
    {
        std::ostringstream o;
        o << "digraph agents {\n  rankdir=TB;\n  node [shape=ellipse];\n";
        // agents split into groups are drawn in a cluster, as §5.3's Figure 13 draws the DMA
        std::map<Agent *, std::vector<int>> by_agent;
        for (int v = 0; v < int(m_vertices.size()); v++) by_agent[m_vertices[size_t(v)].agent].push_back(v);
        for (auto &kv : by_agent)
        {
            if (kv.second.size() == 1)
            {
                const Vertex &vx = m_vertices[size_t(kv.second[0])];
                o << "  v" << kv.second[0] << " [label=\"" << vx.name << "\"];\n";
                continue;
            }
            o << "  subgraph cluster_" << kv.first->id() << " {\n    label=\"" << kv.first->name() << "\";\n";
            for (int v : kv.second) o << "    v" << v << " [label=\"" << m_vertices[size_t(v)].name << "\"];\n";
            o << "  }\n";
        }
        for (auto &e : m_edges)
            o << "  v" << e.from << " -> v" << e.to << " [label=\"" << e.link->name() << " TL-"
              << levelName(e.link->level()) << "\"];\n";
        o << "}\n";
        return o.str();
    }

    void GraphCheck::onElaborate(Kernel &k)
    {
        build(k);
        // §5 rule 1: the agent graph contains no cycles.
        std::string cycle = findCycle();
        if (!cycle.empty()) throw ConfigError("agent graph has a cycle (rule 1, §5): " + cycle);
    }
}}
