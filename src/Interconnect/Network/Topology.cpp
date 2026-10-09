/*
 * File  :      Topology.cpp
 * Specification: docs/NoC_Architecture_Spec.md §5.
 */
#include "../../../header/Interconnect/Network/Topology.h"
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

    // link a -> b with port names; appends one port on each side, in call order
    void Topology::connect(int a, int /*hint*/, int b, const std::string &name_ab, const std::string &name_ba)
    {
        int pa = (int)m_routers[a].links.size(), pb = (int)m_routers[b].links.size();
        m_routers[a].links.push_back(PortRef{b, pb}); m_routers[a].link_names.push_back(name_ab);
        m_routers[b].links.push_back(PortRef{a, pa}); m_routers[b].link_names.push_back(name_ba);
    }

    void Topology::attachAt(int r, int endpoint)
    {
        if (m_endpoint_router.count(endpoint))
            throw ConfigError("endpoint " + std::to_string(endpoint) + " attached twice");
        m_endpoint_router[endpoint] = r;
        m_routers[r].endpoints.push_back(endpoint);
        m_endpoint_ids.push_back(endpoint);
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
                R.local_ports[lp].push_back(R.endpoints[i]);
                m_endpoint_port[R.endpoints[i]] = (int)R.links.size() + lp;
            }
        }
    }

    void Topology::attach(const std::map<int, int> &node_map, int local_ports)
    {
        for (auto &kv : node_map)
        {
            if (kv.second < 0 || kv.second >= routers())
                throw ConfigError("node_map[" + std::to_string(kv.first) + "] = " + std::to_string(kv.second) +
                                  " is not a router of this topology (routers 0.." + std::to_string(routers() - 1) + ")");
            attachAt(kv.second, kv.first);
        }
        finishLocalPorts(local_ports);
    }

    // ------------------------------------------------------------------ generators
    Topology Topology::grid2d(int kx, int ky, const std::map<int, int> &node_map, int local_ports, bool torus)
    {
        if (kx < 1 || ky < 1) throw ConfigError("dims must be >= 1,1");
        if (torus && (kx < 3 || ky < 3)) throw ConfigError("torus2d needs dims >= 3,3 (a wrap link must differ from the direct link)");
        Topology t; t.m_kind = torus ? Kind::Torus2D : Kind::Grid2D; t.m_kx = kx; t.m_ky = ky;
        t.m_routers.resize(kx * ky);
        for (int y = 0; y < ky; y++) for (int x = 0; x < kx; x++)
        {
            int r = y * kx + x; t.m_routers[r].id = r; t.m_routers[r].coord = {x, y}; t.m_id_to_index[r] = r;
        }
        // port order per router: east, west, north, south (absent directions skipped on a grid)
        auto at = [&](int x, int y) { return y * kx + x; };
        for (int y = 0; y < ky; y++) for (int x = 0; x < kx; x++)
        {
            int r = at(x, y); Router &R = t.m_routers[r];
            auto add = [&](int nx, int ny, const std::string &name) {
                int n = at(nx, ny);
                R.links.push_back(PortRef{n, -1}); R.link_names.push_back(name);
            };
            if (x + 1 < kx) add(x + 1, y, "east"); else if (torus) add(0, y, "east(wrap)");
            if (x - 1 >= 0) add(x - 1, y, "west"); else if (torus) add(kx - 1, y, "west(wrap)");
            if (y - 1 >= 0) add(x, y - 1, "north"); else if (torus) add(x, ky - 1, "north(wrap)");
            if (y + 1 < ky) add(x, y + 1, "south"); else if (torus) add(x, 0, "south(wrap)");
        }
        // resolve the far-end port indices: the neighbour's port that points back here
        for (int r = 0; r < t.routers(); r++)
            for (auto &l : t.m_routers[r].links)
            {
                const Router &N = t.m_routers[l.router];
                for (size_t q = 0; q < N.links.size(); q++) if (N.links[q].router == r) { l.port = (int)q; break; }
                if (l.port < 0) throw ConfigError("internal: asymmetric generated link");
            }
        t.attach(node_map, local_ports);
        return t;
    }

    Topology Topology::ring(int k, const std::map<int, int> &node_map, int local_ports)
    {
        if (k < 3) throw ConfigError("ring needs k >= 3");
        Topology t; t.m_kind = Kind::Ring; t.m_kx = k; t.m_ky = 1;
        t.m_routers.resize(k);
        for (int r = 0; r < k; r++) { t.m_routers[r].id = r; t.m_routers[r].coord = {r, 0}; t.m_id_to_index[r] = r; }
        for (int r = 0; r < k; r++)            // clockwise, counter-clockwise
        {
            t.m_routers[r].links.push_back(PortRef{(r + 1) % k, 1}); t.m_routers[r].link_names.push_back("clockwise");
            t.m_routers[r].links.push_back(PortRef{(r + k - 1) % k, 0}); t.m_routers[r].link_names.push_back("counter-clockwise");
        }
        t.attach(node_map, local_ports);
        return t;
    }

    Topology Topology::star(const std::vector<int> &endpoint_ids, int local_ports)
    {
        Topology t; t.m_kind = Kind::Star; t.m_kx = 1; t.m_ky = 1;
        t.m_routers.resize(1); t.m_routers[0].id = 0; t.m_id_to_index[0] = 0;
        for (int e : endpoint_ids) t.attachAt(0, e);
        t.finishLocalPorts(local_ports);
        return t;
    }

    Topology Topology::custom(const std::vector<std::vector<int>> &matrix, const std::vector<int> &endpoint_ids,
                              const std::vector<int> &router_ids, int local_ports)
    {
        Topology t; t.m_kind = Kind::Custom;
        std::set<int> endpoints(endpoint_ids.begin(), endpoint_ids.end());
        std::map<int, std::vector<int>> adj;
        for (auto &row : matrix)
        {
            if (row.empty()) throw ConfigError("connection_matrix row is empty");
            if (adj.count(row[0])) throw ConfigError("connection_matrix lists node " + std::to_string(row[0]) + " twice");
            adj[row[0]] = std::vector<int>(row.begin() + 1, row.end());
        }
        for (auto &kv : adj)
            for (int n : kv.second)
            {
                if (!adj.count(n)) throw ConfigError("connection_matrix: node " + std::to_string(kv.first) + " lists " + std::to_string(n) + " which has no row");
                auto &back = adj[n];
                if (std::find(back.begin(), back.end(), kv.first) == back.end())
                    throw ConfigError("connection_matrix is not symmetric: " + std::to_string(kv.first) + " lists " + std::to_string(n) +
                                      " but " + std::to_string(n) + " does not list " + std::to_string(kv.first));
            }
        // routers: every matrix node. An endpoint node gets an implicit router of its own id;
        // non-endpoint nodes are explicit routers (router_ids may restrict which; the rest must
        // then be endpoints).
        std::set<int> explicit_routers(router_ids.begin(), router_ids.end());
        for (auto &kv : adj)
        {
            if (!endpoints.count(kv.first) && !explicit_routers.empty() && !explicit_routers.count(kv.first))
                throw ConfigError("connection_matrix node " + std::to_string(kv.first) + " is neither an endpoint (component_ids) nor a router (router_ids)");
            Router R; R.id = kv.first; R.coord = {(int)t.m_routers.size(), 0};
            t.m_id_to_index[kv.first] = (int)t.m_routers.size();
            t.m_routers.push_back(R);
        }
        for (int e : endpoint_ids)
            if (!adj.count(e)) throw ConfigError("endpoint " + std::to_string(e) + " (component_ids) has no connection_matrix row");
        // links in matrix-row order; each undirected pair once, ports appended in the order the
        // rows name the neighbour
        for (auto &kv : adj)
        {
            int a = t.m_id_to_index[kv.first];
            for (int n : kv.second)
            {
                int b = t.m_id_to_index[n];
                // already connected?
                bool done = false;
                for (auto &l : t.m_routers[a].links) if (l.router == b) { done = true; break; }
                if (done) continue;
                t.m_routers[a].links.push_back(PortRef{b, -1}); t.m_routers[a].link_names.push_back("to " + std::to_string(n));
                t.m_routers[b].links.push_back(PortRef{a, -1}); t.m_routers[b].link_names.push_back("to " + std::to_string(kv.first));
            }
        }
        for (int r = 0; r < t.routers(); r++)
            for (auto &l : t.m_routers[r].links)
            {
                const Router &N = t.m_routers[l.router];
                for (size_t q = 0; q < N.links.size(); q++) if (N.links[q].router == r) { l.port = (int)q; break; }
            }
        // endpoints attach to their implicit routers
        for (int e : endpoint_ids) t.attachAt(t.m_id_to_index[e], e);
        t.finishLocalPorts(local_ports);
        return t;
    }

    std::string Topology::describe() const
    {
        std::ostringstream o;
        static const char *kinds[] = {"custom", "grid2d", "torus2d", "ring", "star"};
        o << kinds[(int)m_kind] << ": " << routers() << " routers, " << m_endpoint_ids.size() << " endpoints\n";
        for (int r = 0; r < routers(); r++)
        {
            o << "  router " << routerId(r) << " (" << coord(r).first << "," << coord(r).second << "):";
            for (int p = 0; p < ports(r); p++)
            {
                o << " [" << p << "] " << portName(r, p);
                if (!isLocalPort(r, p)) o << "->" << routerId(link(r, p).router) << ":" << link(r, p).port;
                else { o << "{"; for (int e : endpointsAtPort(r, p)) o << " " << e; o << " }"; }
            }
            o << "\n";
        }
        return o.str();
    }
}}
