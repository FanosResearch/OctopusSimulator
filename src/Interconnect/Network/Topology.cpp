/*
 * File  :      Topology.cpp
 * Specification: docs/NoC_Architecture_Spec.md §5.
 */
#include "../../../header/Interconnect/Network/Topology.h"
#include "../../../header/Interconnect/Network/TopologyBuilder.h"
#include <algorithm>
#include <set>
#include <sstream>

namespace octopus { namespace net
{
    // ------------------------------------------------------------------ helpers
    int Topology::routerIndex(int id) const
    {
        auto it = m_id_to_index.find(id);
        return it == m_id_to_index.end() ? -1 : it->second;
    }
    int Topology::routerOf(int endpoint) const
    {
        auto it = m_endpoint_router.find(endpoint);
        return it == m_endpoint_router.end() ? -1 : it->second;
    }
    int Topology::localPort(int endpoint) const
    {
        auto it = m_endpoint_port.find(endpoint);
        return it == m_endpoint_port.end() ? -1 : it->second;
    }
    int Topology::ejectRouter(int endpoint) const
    {
        auto it = m_eject_router.find(endpoint);
        return it == m_eject_router.end() ? -1 : it->second;
    }
    int Topology::ejectPort(int endpoint) const
    {
        auto it = m_eject_port.find(endpoint);
        return it == m_eject_port.end() ? -1 : it->second;
    }
    std::vector<std::pair<int, int>> Topology::routedPairs() const
    {
        std::vector<std::pair<int, int>> out;
        if (!m_oneway)
        {
            for (int s = 0; s < routers(); s++) for (int d = 0; d < routers(); d++) if (s != d) out.push_back({s, d});
            return out;
        }
        std::set<int> src, dst;
        for (int e : m_endpoint_ids) { src.insert(routerOf(e)); dst.insert(ejectRouter(e)); }
        for (int s : src) for (int d : dst) if (s != d) out.push_back({s, d});
        return out;
    }
    std::vector<int> Topology::endpointsAtPort(int r, int p) const
    {
        if (!isLocalPort(r, p)) return {};
        return m_routers[r].local_ports[p - linkPorts(r)];
    }
    std::string Topology::portName(int r, int p) const
    {
        if (isLocalPort(r, p)) return "local" + std::to_string(p - linkPorts(r));
        return m_routers[r].link_names[p];
    }

    // ------------------------------------------------------------------ construction API
    int Topology::addRouter(int id)
    {
        if (m_id_to_index.count(id)) throw ConfigError("router id " + std::to_string(id) + " added twice");
        Router R; R.id = id;
        m_id_to_index[id] = (int)m_routers.size();
        m_routers.push_back(R);
        return (int)m_routers.size() - 1;
    }
    void Topology::addPort(int r, int to, const std::string &name)
    {
        m_routers[r].links.push_back(PortRef{to, -1});
        m_routers[r].ups.push_back(PortRef{});
        m_routers[r].oneway.push_back(0);
        m_routers[r].link_names.push_back(name);
    }
    void Topology::addOneWay(int a, int b, const std::string &name_a, const std::string &name_b)
    {
        if (a == b) throw ConfigError("a one-way link from router " + std::to_string(m_routers[a].id) + " to itself");
        Router &A = m_routers[a], &B = m_routers[b];
        int pa = (int)A.links.size(), pb = (int)B.links.size();
        A.links.push_back(PortRef{b, pb}); A.ups.push_back(PortRef{}); A.oneway.push_back(1); A.link_names.push_back(name_a);
        B.links.push_back(PortRef{}); B.ups.push_back(PortRef{a, pa}); B.oneway.push_back(2); B.link_names.push_back(name_b);
        m_oneway = true;
    }
    void Topology::addPort(int r, int to, const std::string &name, int dim, int dir)
    {
        if (!m_lattice) throw ConfigError("internal: a lattice port before setLattice");
        auto &dp = m_routers[r].dir_port;
        if (dp.empty()) dp.assign(2 * m_lattice->rank(), -1);
        dp[2 * dim + (dir < 0 ? 1 : 0)] = (int)m_routers[r].links.size();
        addPort(r, to, name);
    }
    void Topology::setLattice(const Lattice &l)
    {
        if (l.dims.size() != l.wrap.size()) throw ConfigError("internal: lattice dims and wrap differ in rank");
        if (l.size() != routers()) throw ConfigError("internal: lattice of " + std::to_string(l.size()) + " routers on " + std::to_string(routers()));
        m_lattice = std::make_shared<Lattice>(l);
    }
    int Topology::dirPort(int r, int dim, int dir) const
    {
        const auto &dp = m_routers[r].dir_port;
        size_t i = 2 * dim + (dir < 0 ? 1 : 0);
        return i < dp.size() ? dp[i] : -1;
    }
    bool Topology::portDir(int r, int p, int &dim, int &dir) const
    {
        const auto &dp = m_routers[r].dir_port;
        for (size_t i = 0; i < dp.size(); i++) if (dp[i] == p) { dim = (int)i / 2; dir = (i % 2) ? -1 : +1; return true; }
        return false;
    }
    int Topology::attr(int r, const std::string &key, int dflt) const
    {
        auto it = m_routers[r].attrs.find(key);
        return it == m_routers[r].attrs.end() ? dflt : it->second;
    }

    void Topology::attachAt(int r, int endpoint)
    {
        if (m_endpoint_router.count(endpoint))
            throw ConfigError("endpoint " + std::to_string(endpoint) + " attached twice");
        m_endpoint_router[endpoint] = r;
        m_eject_router[endpoint] = r;
        m_routers[r].endpoints.push_back(endpoint);
        m_endpoint_ids.push_back(endpoint);
    }
    void Topology::attachSplit(int inject, int eject, int endpoint)
    {
        attachAt(inject, endpoint);
        if (eject == inject) return;
        m_eject_router[endpoint] = eject;
        m_routers[eject].endpoints.push_back(endpoint);
    }

    // the k-th two-way port r->n pairs with the k-th two-way port n->r; one-way links are
    // paired when they are added
    void Topology::finalize(int local_ports)
    {
        for (int r = 0; r < routers(); r++)
        {
            std::map<int, int> seen;                     // neighbour -> two-way ports to it so far
            Router &R = m_routers[r];
            for (size_t p = 0; p < R.links.size(); p++)
            {
                if (R.oneway[p]) continue;
                PortRef &l = R.links[p];
                int k = seen[l.router]++;
                const Router &N = m_routers[l.router];
                int found = -1;
                for (size_t q = 0; q < N.links.size(); q++)
                    if (!N.oneway[q] && N.links[q].router == r && k-- == 0) { found = (int)q; break; }
                if (found < 0)
                    throw ConfigError("link from router " + std::to_string(R.id) + " to " + std::to_string(N.id) + " has no reverse link");
                l.port = found;
                R.ups[p] = l;
            }
        }
        finishLocalPorts(local_ports);
    }

    // distribute each router's endpoints over its local ports (spec §5.2); local ports are
    // numbered after the link ports
    void Topology::finishLocalPorts(int local_ports)
    {
        if (local_ports < 0) throw ConfigError("local_ports must be >= 0, got " + std::to_string(local_ports));
        for (int r = 0; r < routers(); r++)
        {
            Router &R = m_routers[r];
            int n = local_ports == 0 ? (int)R.endpoints.size() : local_ports;
            R.local_ports.assign(n, {});
            for (size_t i = 0; i < R.endpoints.size(); i++)
            {
                int lp = n == 0 ? -1 : (int)(i % n);
                if (lp < 0) throw ConfigError("router " + std::to_string(R.id) + " has endpoints but local_ports is 0");
                int e = R.endpoints[i];
                R.local_ports[lp].push_back(e);
                if (m_endpoint_router[e] == r) m_endpoint_port[e] = (int)R.links.size() + lp;
                if (m_eject_router[e] == r) m_eject_port[e] = (int)R.links.size() + lp;
            }
        }
    }

    // ------------------------------------------------------------------ the shipped generators
    // (implemented as builders in topologies/*.cpp; these names stay for the unit tests)
    Topology Topology::grid2d(int kx, int ky, const std::map<int, int> &node_map, int local_ports, bool torus)
    { return buildGrid2D(kx, ky, node_map, local_ports, torus); }
    Topology Topology::ring(int k, const std::map<int, int> &node_map, int local_ports)
    { return buildRing(k, node_map, local_ports); }
    Topology Topology::star(const std::vector<int> &endpoint_ids, int local_ports)
    { return buildStar(endpoint_ids, local_ports); }
    Topology Topology::custom(const std::vector<std::vector<int>> &matrix, const std::vector<int> &endpoint_ids,
                              const std::vector<int> &router_ids, int local_ports)
    { return buildCustom(matrix, endpoint_ids, router_ids, local_ports); }

    std::string Topology::describe() const
    {
        std::ostringstream o;
        o << m_kind_name << ": " << routers() << " routers, " << m_endpoint_ids.size() << " endpoints\n";
        for (int r = 0; r < routers(); r++)
        {
            o << "  router " << routerId(r);
            if (m_lattice) { o << " ("; auto c = m_lattice->coord(r); for (size_t d = 0; d < c.size(); d++) o << (d ? "," : "") << c[d]; o << ")"; }
            o << ":";
            for (int p = 0; p < ports(r); p++)
            {
                o << " [" << p << "] " << portName(r, p);
                if (!isLocalPort(r, p) && canSend(r, p)) o << "->" << routerId(link(r, p).router) << ":" << link(r, p).port;
                else if (!isLocalPort(r, p)) o << "<-" << routerId(upstream(r, p).router) << ":" << upstream(r, p).port;
                else { o << "{"; for (int e : endpointsAtPort(r, p)) o << " " << e; o << " }"; }
            }
            o << "\n";
        }
        return o.str();
    }
}}
