/*
 * File  :      Topology.h
 * The network graph: routers, unidirectional links, port numbering, endpoint attachment and
 * shape capabilities. Specification: docs/NoC_Architecture_Spec.md §5; the construction API
 * and the capabilities: docs/NoC_Topology_Plugin_Plan.md s3.
 *
 * Routers are numbered 0..R-1 internally; routerId(r) gives the configured id (the matrix
 * node id for `custom`, r itself for generated topologies). Ports of a router are numbered
 * with the link ports first in a fixed order and the local ports last.
 *
 * Routing reads capabilities, never a topology name: lattice() (coordinates, wraps and the
 * port of each direction) for meshes, tori and rings of any dimension, attr() for metadata of
 * other shapes (a tree level, a dragonfly group), implicitRouter() for the zero-latency
 * routers of `custom` endpoint nodes.
 */
#ifndef _NET_TOPOLOGY_H
#define _NET_TOPOLOGY_H
#include "NetTypes.h"
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace octopus { namespace net
{
    // A k-ary lattice: router r has coordinates with dimension 0 varying fastest,
    // r = c[0] + dims[0] * (c[1] + dims[1] * (c[2] + ...)).
    struct Lattice
    {
        std::vector<int>  dims;
        std::vector<bool> wrap;                     // a wrap link in this dimension (torus, ring)
        int  size() const { int n = 1; for (int k : dims) n *= k; return n; }
        int  rank() const { return (int)dims.size(); }
        bool anyWrap() const { for (bool w : wrap) if (w) return true; return false; }
        std::vector<int> coord(int r) const
        {
            std::vector<int> c(dims.size());
            for (size_t d = 0; d < dims.size(); d++) { c[d] = r % dims[d]; r /= dims[d]; }
            return c;
        }
        int index(const std::vector<int> &c) const
        {
            int r = 0, s = 1;
            for (size_t d = 0; d < dims.size(); d++) { r += c[d] * s; s *= dims[d]; }
            return r;
        }
    };

    class Topology
    {
    public:
        struct PortRef { int router = -1; int port = -1; bool valid() const { return router >= 0; } };

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

        const std::string &kindName() const { return m_kind_name; }   // the builder's name
        int  routers() const { return (int)m_routers.size(); }
        int  ports(int r) const { return linkPorts(r) + localPorts(r); }
        int  linkPorts(int r) const { return (int)m_routers[r].links.size(); }
        int  localPorts(int r) const { return (int)m_routers[r].local_ports.size(); }
        bool isLocalPort(int r, int p) const { return p >= linkPorts(r); }
        // The far end of a link port; invalid for a local port.
        PortRef link(int r, int p) const { return p < linkPorts(r) ? m_routers[r].links[p] : PortRef{}; }
        int  routerId(int r) const { return m_routers[r].id; }
        int  routerIndex(int id) const;                       // -1 if not a router id

        // ---- capabilities
        const Lattice *lattice() const { return m_lattice.get(); }   // null: not a lattice
        // the link port of router r one step in dimension `dim` (dir +1 or -1); -1 if none
        int  dirPort(int r, int dim, int dir) const;
        // the lattice direction of link port p of router r; false if it has none
        bool portDir(int r, int p, int &dim, int &dir) const;
        int  attr(int r, const std::string &key, int dflt = -1) const;
        // custom matrices give every endpoint node an implicit router: it stands for the endpoint's
        // own link ports and adds no pipeline latency (spec s5.1)
        bool implicitRouter(int r) const { return m_routers[r].implicit; }

        const std::vector<int> &endpoints() const { return m_endpoint_ids; }
        int  routerOf(int endpoint) const;                    // -1 if unknown
        int  localPort(int endpoint) const;                   // port index at its router
        const std::vector<int> &endpointsAt(int r) const { return m_routers[r].endpoints; }
        std::vector<int> endpointsAtPort(int r, int p) const;
        bool isEndpoint(int id) const { return m_endpoint_router.count(id) > 0; }

        // Port names for messages and the hang dump: "east", "local0", "to 7", ...
        std::string portName(int r, int p) const;
        std::string describe() const;

        // ---- construction, used by the topology builders only (docs/NoC_Topology_Plugin_Plan.md
        // s3.1); the engine never calls these. Routers are indexed in the order they are added,
        // link ports in the order addPort is called on their router, endpoints attach in call
        // order. finalize() pairs every port r->n with the matching port n->r (the k-th port
        // r->n with the k-th port n->r), numbers the local ports and validates the graph.
        void setKindName(const std::string &k) { m_kind_name = k; }
        // a lattice's routers must be added in lattice index order before setLattice
        void setLattice(const Lattice &l);
        int  addRouter(int id);
        void addPort(int r, int to, const std::string &name);
        void addPort(int r, int to, const std::string &name, int dim, int dir);   // a lattice port
        void setImplicit(int r, bool v) { m_routers[r].implicit = v; }
        void setAttr(int r, const std::string &key, int value) { m_routers[r].attrs[key] = value; }
        void attachAt(int r, int endpoint);
        void finalize(int local_ports);

    private:
        struct Router
        {
            int id = 0;
            bool implicit = false;
            std::vector<PortRef> links;                   // one per link port, in port order
            std::vector<std::string> link_names;
            std::vector<int> dir_port;                    // lattice: [2*dim + (dir < 0)] -> port
            std::map<std::string, int> attrs;
            std::vector<std::vector<int>> local_ports;    // endpoints per local port
            std::vector<int> endpoints;                   // all endpoints at this router
        };
        std::string m_kind_name = "custom";
        std::shared_ptr<Lattice> m_lattice;               // shared: Topology is copied by value
        std::vector<Router> m_routers;
        std::vector<int> m_endpoint_ids;
        std::map<int, int> m_endpoint_router;             // endpoint -> router index
        std::map<int, int> m_endpoint_port;               // endpoint -> port index
        std::map<int, int> m_id_to_index;

        void finishLocalPorts(int local_ports);
    };
}}
#endif /* _NET_TOPOLOGY_H */
