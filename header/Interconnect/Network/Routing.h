/*
 * File  :      Routing.h
 * Routing algorithms (plug-ins), the routing-level deadlock check, and the ordering rules.
 * Specification: docs/NoC_Architecture_Spec.md §10, §14.1, §14.3.
 */
#ifndef _NET_ROUTING_H
#define _NET_ROUTING_H
#include "NetTypes.h"
#include "Topology.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace octopus { namespace net
{
    enum class Ordering { NONE, PAIR_PER_CLASS };

    // What a routing function may read of the current router: credit counts only (spec §10.1).
    struct RouterView
    {
        virtual ~RouterView() {}
        virtual int credits(int out_port, int cls, int vc) const = 0;   // free slots downstream
        virtual int vcsPerClass() const = 0;
    };

    struct RouteContext
    {
        int router = 0, in_port = 0, in_vc = 0, dst_router = 0, cls = 0, src_router = 0;
        const Packet *packet = nullptr;
        const RouterView *here = nullptr;      // may be null (no adaptivity possible then)
    };
    struct Route
    {
        int out_port = -1;
        uint32_t vc_mask = ~0u;                // bit i = output VC i of the class is allowed
        // a fallback the head may take at VC allocation when the primary cannot be granted
        // (Duato: the escape route of an adaptive algorithm); alt_port < 0 = none
        int alt_port = -1;
        uint32_t alt_mask = 0;
    };

    class RoutingAlgorithm
    {
    public:
        virtual ~RoutingAlgorithm() {}
        virtual Route nextHop(const RouteContext &) const = 0;
        virtual Ordering ordering() const = 0;
        virtual bool needsEscapeVC() const { return false; }
        // true if output VC `vc` may be granted to a new packet only when its downstream buffer is
        // empty (all credits back): atomic VC reallocation. Duato's condition needs it on the
        // adaptive VCs of an escape-VC algorithm: a head granted an adaptive VC still holding
        // another packet's flits is committed to it and can no longer fall back to the escape
        // network, so the adaptive VCs could close a cycle (docs/NoC_Architecture_Spec.md s14.3).
        virtual bool atomicVC(int /*vc*/) const { return false; }
        // called once when a packet is created, after its source and destination routers are set:
        // an oblivious algorithm draws its per-packet choice here (Packet::route_aux), from a hash
        // of the packet's sequence number and the routing seed, so runs stay deterministic
        virtual void onInject(Packet &) const {}
        // true if the VC a packet uses is a function of (source, destination, class), so that
        // several VCs per class still keep a pair's packets in order (a dateline does this)
        virtual bool fixesVCPerPair() const { return false; }
        // Every output the algorithm may use at this hop, whatever the credits (the primary and
        // any fallback). The channel dependency check explores these. Default: nextHop and its
        // fallback.
        virtual std::vector<Route> candidates(const RouteContext &c) const;
        virtual bool deadlockFreeOn(const Topology &) const = 0;
        // why deadlockFreeOn failed: one cycle, for the error message (empty if none)
        virtual std::string deadlockCycle(const Topology &t, int vcs_per_class) const;
        virtual std::string name() const = 0;
        // the full path src -> dst as (router, out_port) hops, following nextHop with no
        // adaptivity (credits unknown); used by the deadlock check and by tests
        std::vector<std::pair<int, int>> path(const Topology &t, int src, int dst, int cls = 0) const;
    };

    struct RoutingParams
    {
        int vcs_per_class = 1;
        int escape_vc = -1;
        uint64_t seed = 1;                                 // routing_seed: oblivious algorithms' per-packet draws
        std::map<int, std::vector<int>> routing_table;     // router id -> dest,port,dest,port,...
        std::map<int, std::vector<int>> routing_via;       // router id -> dest,neighbour,... (port-order independent)
        std::map<int, std::vector<int>> link_weight;       // router id -> neighbour,weight,... (Table: shortest weighted paths)
    };

    // Factory keyed by name: Table, XY, WestFirst, AdaptiveMinimal. ConfigError on an unknown
    // name or a combination the topology cannot support (spec §4.5).
    std::unique_ptr<RoutingAlgorithm> createRouting(const std::string &name, const Topology &t, const RoutingParams &p);

    // Routing-level deadlock check (spec §14.3): channel dependency graph over (link, vc) for
    // every source/destination path the algorithm produces; true if acyclic. On failure
    // `cycle` receives a human-readable description of one cycle.
    bool channelDependencyAcyclic(const Topology &t, const RoutingAlgorithm &r, int vcs_per_class, std::string *cycle);
    // The same check over a route function of (router, in_port, in_vc, destination router) for
    // packets that may enter at any router: used for the escape network of an adaptive algorithm
    // (Duato: escape packets never leave it, so its own graph must be acyclic).
    bool escapeDependencyAcyclic(const Topology &t, int vcs_per_class,
                                 const std::function<Route(int router, int in_port, int in_vc, int dst)> &escape,
                                 std::string *cycle);

    // Spec §14.1: refuse a protocol ordering requirement the network cannot meet.
    void checkOrderingRequirement(const std::string &required, const RoutingAlgorithm &r, const Topology &t, int vcs_per_class);
}}
#endif /* _NET_ROUTING_H */
