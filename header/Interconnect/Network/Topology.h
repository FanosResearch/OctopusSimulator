/*
 * File  :      Topology.h
 * The network graph: routers, unidirectional links, port numbering, endpoint attachment,
 * coordinates. Specification: docs/NoC_Architecture_Spec.md §5.
 *
 * Routers are numbered 0..R-1 internally; routerId(r) gives the configured id (the matrix
 * node id for `custom`, r itself for generated topologies). Ports of a router are numbered
 * with the link ports first in a fixed order and the local ports last.
 */
#ifndef _NET_TOPOLOGY_H
#define _NET_TOPOLOGY_H
#include "NetTypes.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace octopus { namespace net
{
    class Topology
    {
    public:
        struct PortRef { int router = -1; int port = -1; bool valid() const { return router >= 0; } };
        enum class Kind { Custom, Grid2D, Torus2D, Ring, Star };

        // `custom`: the matrix rows are "node id, neighbour, neighbour, ..." (spec §4.1).
        // Nodes in endpoint_ids are endpoints; every endpoint gets an implicit router of its
        // own (so the legacy direct-link matrices stay representable); nodes in neither list
        // are explicit routers (router_ids may name them; empty = derived). The matrix must
        // be symmetric. local_ports: 0 = one per endpoint.
        static Topology custom(const std::vector<std::vector<int>> &matrix,
                               const std::vector<int> &endpoint_ids,
                               const std::vector<int> &router_ids, int local_ports);
        // grid2d / torus2d: kx * ky routers row-major, node_map endpoint -> router.
        static Topology grid2d(int kx, int ky, const std::map<int, int> &node_map, int local_ports, bool torus = false);
        static Topology ring(int k, const std::map<int, int> &node_map, int local_ports);
        // star: one router, every endpoint on it.
        static Topology star(const std::vector<int> &endpoint_ids, int local_ports);

        Kind kind() const { return m_kind; }
        int  routers() const { return (int)m_routers.size(); }
        int  ports(int r) const { return linkPorts(r) + localPorts(r); }
        int  linkPorts(int r) const { return (int)m_routers[r].links.size(); }
        int  localPorts(int r) const { return (int)m_routers[r].local_ports.size(); }
        bool isLocalPort(int r, int p) const { return p >= linkPorts(r); }
        // The far end of a link port; invalid for a local port.
        PortRef link(int r, int p) const { return p < linkPorts(r) ? m_routers[r].links[p] : PortRef{}; }
        std::pair<int, int> coord(int r) const { return m_routers[r].coord; }
        int  routerId(int r) const { return m_routers[r].id; }
        int  routerIndex(int id) const;                       // -1 if not a router id
        int  dimX() const { return m_kx; }
        int  dimY() const { return m_ky; }

        const std::vector<int> &endpoints() const { return m_endpoint_ids; }
        int  routerOf(int endpoint) const;                    // -1 if unknown
        int  localPort(int endpoint) const;                   // port index at its router
        const std::vector<int> &endpointsAt(int r) const { return m_routers[r].endpoints; }
        std::vector<int> endpointsAtPort(int r, int p) const;
        bool isEndpoint(int id) const { return m_endpoint_router.count(id) > 0; }
        // custom matrices give every endpoint node an implicit router: it stands for the endpoint's
        // own link ports and adds no pipeline latency (spec s5.1)
        bool implicitRouter(int r) const { return m_kind == Kind::Custom && isEndpoint(m_routers[r].id); }

        // Port names for messages and the hang dump: "east", "local0", "to 7", ...
        std::string portName(int r, int p) const;
        std::string describe() const;

    private:
        struct Router
        {
            int id = 0;
            std::pair<int, int> coord{0, 0};
            std::vector<PortRef> links;                   // one per link port, in port order
            std::vector<std::string> link_names;
            std::vector<std::vector<int>> local_ports;    // endpoints per local port
            std::vector<int> endpoints;                   // all endpoints at this router
        };
        Kind m_kind = Kind::Custom;
        int m_kx = 0, m_ky = 0;
        std::vector<Router> m_routers;
        std::vector<int> m_endpoint_ids;
        std::map<int, int> m_endpoint_router;             // endpoint -> router index
        std::map<int, int> m_endpoint_port;               // endpoint -> port index
        std::map<int, int> m_id_to_index;

        void attach(const std::map<int, int> &node_map, int local_ports); // generated topologies
        void attachAt(int r, int endpoint);
        void finishLocalPorts(int local_ports);
        void connect(int a, int pa_name_hint, int b, const std::string &name_ab, const std::string &name_ba);
    };
}}
#endif /* _NET_TOPOLOGY_H */
