/*
 * File  :      Routing.h
 * Routing algorithms (plug-ins), the routing-level deadlock check, and the ordering rules.
 * Specification: docs/NoC_Architecture_Spec.md §10, §14.1, §14.3.
 */
#ifndef _NET_ROUTING_H
#define _NET_ROUTING_H
#include "NetTypes.h"
#include "Topology.h"
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
    };

    class RoutingAlgorithm
    {
    public:
        virtual ~RoutingAlgorithm() {}
        virtual Route nextHop(const RouteContext &) const = 0;
        virtual Ordering ordering() const = 0;
        virtual bool needsEscapeVC() const { return false; }
        virtual bool deadlockFreeOn(const Topology &) const = 0;
        virtual std::string name() const = 0;
        // the full path src -> dst as (router, out_port) hops, following nextHop with no
        // adaptivity (credits unknown); used by the deadlock check and by tests
        std::vector<std::pair<int, int>> path(const Topology &t, int src, int dst, int cls = 0) const;
    };

    struct RoutingParams
    {
        int vcs_per_class = 1;
        int escape_vc = -1;
        std::map<int, std::vector<int>> routing_table;     // router id -> dest,port,dest,port,...
    };

    // Factory keyed by name: Table, XY, WestFirst, AdaptiveMinimal. ConfigError on an unknown
    // name or a combination the topology cannot support (spec §4.5).
    std::unique_ptr<RoutingAlgorithm> createRouting(const std::string &name, const Topology &t, const RoutingParams &p);

    // Routing-level deadlock check (spec §14.3): channel dependency graph over (link, vc) for
    // every source/destination path the algorithm produces; true if acyclic. On failure
    // `cycle` receives a human-readable description of one cycle.
    bool channelDependencyAcyclic(const Topology &t, const RoutingAlgorithm &r, int vcs_per_class, std::string *cycle);

    // Spec §14.1: refuse a protocol ordering requirement the network cannot meet.
    void checkOrderingRequirement(const std::string &required, const RoutingAlgorithm &r, const Topology &t, int vcs_per_class);
}}
#endif /* _NET_ROUTING_H */
