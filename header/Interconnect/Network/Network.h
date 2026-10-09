/*
 * File  :      Network.h
 * The network: routers with virtual channels and credits, the per-tick engine, statistics.
 * Specification: docs/NoC_Architecture_Spec.md §8, §9, §11, §13, §15, §16.
 */
#ifndef _NET_NETWORK_H
#define _NET_NETWORK_H
#include "ClockManager.h"
#include "Configurable.h"
#include "Arbiter.h"
#include "Logger.h"
#include "NetTypes.h"
#include "Classifier.h"
#include "Topology.h"
#include "Routing.h"
#include "NetworkInterface.h"
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace octopus { namespace net
{
    struct NetConfig
    {
        int flit_bytes = 16, header_bytes = 8, num_classes = 3, vcs_per_class = 1, vc_depth = 4;
        int router_latency = 2, link_latency = 1, credit_latency = 1;
        std::vector<int> class_link_latency;          // empty = use link_latency
        bool half_duplex = false;
        std::string link_model = "pipelined";         // pipelined: one flit per tick per link; serial: a flit holds the link for its latency
        int inject_depth = 16, eject_depth = 16, local_ports = 0, escape_vc = -1;
        std::string rx_serving = "priority", routing = "Table", arbiter_type = "FCFSArbiter", topology = "custom";
        int linkLatency(int cls) const { return class_link_latency.empty() ? link_latency : class_link_latency[cls]; }
        std::vector<int> classPriority() const;        // classes in serving order, highest first
    };

    class TrafficGenerator;

    class Network : public ClockedObj, public Configurable
    {
    public:
        Network(ParametersMap map, std::string pname = "",
                std::string config_path = std::string(CONFIGURATION_PATH) + std::string(INTERCONNECT),
                std::string name = STRINGIFY(Network));
        ~Network();
        void cycleProcess() override;
        void init() override;

        CommunicationInterface *getInterfaceFor(int id);
        std::vector<int> *getLowerLevelIds();
        const Topology &topology() const { return *m_topo; }
        const NetConfig &cfg() const { return m_cfg; }
        const Classifier &classifier() const { return *m_classifier; }
        uint64_t tick() const { return m_tick; }
        // the system may state the protocol's ordering requirement (spec §14.1)
        void requireOrdering(const std::string &req);
        // network-only mode: the network is the clock trigger and stops when every generator is
        // done and no packet is in flight
        void standalone(const std::vector<TrafficGenerator *> &gens);

        // ---- used by NetworkInterface
        uint64_t newPacket(const Message &msg, int src_endpoint, int dst_endpoint, int cls);
        Packet &packet(uint64_t seq) { return m_packets.at(seq); }
        bool hasPacket(uint64_t seq) const { return m_packets.count(seq) > 0; }
        int  classOf(const Message &m, int dst) const { return m_classifier->classifyOrFatal(m, dst); }

        // ---- observability
        std::string dump() const;                      // hang dump (spec §15.5)
        void writeStats(const std::string &path) const; // NetworkStats.csv (spec §15.4)
        struct Totals { uint64_t injected_flits = 0, ejected_flits = 0, injected_packets = 0, ejected_packets = 0, link_flit_hops = 0; };
        const Totals &totals() const { return m_totals; }
        size_t inFlight() const { return m_packets.size(); }

        // one input VC
        struct VCState
        {
            std::deque<Flit> fifo;
            uint64_t cur = 0;          // seq of the routed packet at the front (0 = none)
            bool routed = false;
            int out_port = -1, out_vc = -1;
            uint32_t vc_mask = 0;
            uint64_t head_arrival = 0;
        };
        struct OutVC { int credits = 0; uint64_t holder = 0; int holder_ep = -1; std::unique_ptr<Arbiter> va; };   // holder_ep: the injecting endpoint (local ports only)
        struct OutPort
        {
            std::vector<std::vector<OutVC>> vcs;      // [cls][vc]
            std::unique_ptr<Arbiter> sa;
            uint64_t attempts = 0, losses = 0, flits = 0, busy_ticks = 0;
        };
        struct RouterState
        {
            std::vector<std::vector<std::vector<VCState>>> in;   // [port][cls][vc]
            std::vector<OutPort> out;                            // [port]
            std::vector<uint64_t> in_last_sent;                  // [port] tick a flit last left this input port
        };
        // the upstream side of a local input port: credits and holders per [cls][vc], shared by
        // the endpoints on that port (spec §7.1, §5.2)
        struct LocalPort
        {
            int router = 0, port = 0;
            std::vector<std::vector<OutVC>> vcs;
            std::vector<NetworkInterface *> nis;
            std::unique_ptr<Arbiter> arb;
            uint64_t last_sent = 0;
        };

    private:
        // the view a routing function gets of a router (credits only)
        struct View : RouterView
        {
            const Network *net; int router;
            View(const Network *n, int r) : net(n), router(r) {}
            int credits(int out_port, int cls, int vc) const override { return net->m_routers[router].out[out_port].vcs[cls][vc].credits; }
            int vcsPerClass() const override { return net->m_cfg.vcs_per_class; }
        };
        struct Arrival { int router, port, cls, vc; Flit flit; };
        struct CreditEvt { bool local; int router, port, cls, vc; };

        void readConfig();
        void buildTopology();
        void buildRouters();
        std::unique_ptr<Arbiter> makeArbiter();
        void step();
        void phaseInject();
        void phaseArrivals();
        void phaseRoute();
        void phaseVCAlloc();
        void phaseSwitch();
        void eject(uint64_t seq, int router, int port);
        void sendFlit(int router, int in_port, int cls, int vc, VCState &ivc);
        uint64_t linkKey(int router, int port) const;
        void returnCredit(int router, int in_port, int cls, int vc);
        int  classIndexOf(int cls) const { return cls; }
        std::string flitName(const Flit &f) const;

        NetConfig m_cfg;
        std::vector<int> m_component_ids, m_home_ids, m_lower_ids;
        std::unique_ptr<Topology> m_topo;
        std::unique_ptr<RoutingAlgorithm> m_routing;
        std::unique_ptr<Classifier> m_classifier;
        std::vector<RouterState> m_routers;
        std::map<std::pair<int, int>, LocalPort> m_local;    // (router, port) -> upstream state
        std::map<int, NetworkInterface *> m_nis;
        std::map<uint64_t, Packet> m_packets;
        std::vector<int> m_owner_ids;                         // for the arbiters' rotations
        std::multimap<uint64_t, Arrival> m_arrivals;
        std::multimap<uint64_t, CreditEvt> m_credits;
        std::map<uint64_t, uint64_t> m_link_busy;             // linkKey -> busy_until
        uint64_t m_seq = 0, m_tick = 0, m_cycle_edges = 1;
        Totals m_totals;
        struct ClassStats { uint64_t packets = 0, sum_latency = 0, max_latency = 0, max_wait = 0, first_inject = 0, last_eject = 0; };
        struct EndpointStats { uint64_t packets = 0, sum_latency = 0, max_latency = 0, first_owner = 0, first_seq = 0, max_ahead = 0; };
        std::map<int, EndpointStats> m_ep_stats;
        std::string m_debug;    // OCTOPUS_NET_DEBUG: credit | rx | mask (fatal-path tests) | nocheck (skip the deadlock check)
        long m_hang_ticks = 0;  // OCTOPUS_HANG_DUMP: ticks without a flit moving (packets in flight) before the dump
        uint64_t m_last_move = 0;
        std::vector<ClassStats> m_class_stats;
        std::vector<TrafficGenerator *> m_gens; bool m_standalone = false;
        std::string m_ordering_req;
        static Network *s_instance;
        static void statsAtExit();
        void flushStats();                           // writes NetworkStats.csv once: destructor (normal end) or atexit (abnormal end)
        bool m_stats_written = false;
    };
}}
#endif /* _NET_NETWORK_H */
