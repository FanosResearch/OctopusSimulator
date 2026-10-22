/*
 * File  :      TLNetPort.h
 * TileLink over the Network engine (docs/TileLink_Plan.md s5.3 I-C): the TileLink boundary at each
 * network interface, so that the TL-C handlers (TLC_L1, TLC_LLC) and the memory adapters (TLMemSlave,
 * TLMemMaster) run unchanged on interconnect_type Network with num_classes 5 or 7.
 *
 * Where the crossbar port's functions live on the Network (TLPort.h lists them):
 *   - whole-message delivery, the sender's stamp (Message::from) and the fan-out of a B over
 *     Message::to: the NetworkInterface, unchanged. It delivers a message at its tail flit, stamps
 *     every push, and injects a multicast atomically (one packet per destination, all or none);
 *   - the identifier pools (the A and C source pools of a master, the Grant sink pool of a slave,
 *     txFreeIdsCh / txReserveIdsCh / txReleaseIdsCh) and the namespace remap a crossbar does (TileLink
 *     s6.4): TLNetPort, a thin adapter between the controller and its network interface. It IS a
 *     TLPort (it reuses the pools, the link view, channelOf and the C-PERM registry TLPort::forEndpoint
 *     that TLC_L1 reports to), so the handlers see exactly what they see on the crossbar: an L1 its own
 *     tags, the LLC every source in one namespace, sinks restored on the GrantAck. The NI has no
 *     TileLink state and stays as every Octopus preset uses it;
 *   - the routing checks of TLPort: the network routes by Message::to; TLNetPort refuses a message whose
 *     address (A, C), source (D) or sink (E) names another endpoint than Message::to, as TLPort does.
 *
 * The network carries IDs in the crossbar's namespaces: sources of A, C and D as the slave side sees
 * them (an L1's pool id plus its range base, as the LLC sees them on a crossbar), sinks of D and E as
 * the master side sees them (a bank's pool id plus its range base). The one exception is B: the NI
 * copies a multicast unchanged, so a Probe travels with source 0, which is what every target sees on
 * its own link (its range base minus itself); the LLC side's per-target source (the target's base) is
 * the link view below.
 *
 * Message-level links (plan s5.3 I-C: "the monitor checks the message rules, not the burst rules").
 * The NoC is a TileLink agent whose boundary is the network interfaces. TLNetBoundary builds a library
 * kernel that is never stepped: one TLNetPort per endpoint, the NoC as the library's Xbar (one for the
 * inner TL-C links; with memory on the network a second for the outer TL-UH links: the two decoupled
 * agents of plan s5.3 I-C), and one link per endpoint, w = block_bytes, so every message is one beat.
 * Each port drives its own link synchronously: a push accepted by the NI is a beat offered and
 * accepted on that link in the network's tick, a delivery into the NI's receive queue is a beat
 * offered and accepted on the receiving endpoint's link in the tick it arrives. The Xbars are never
 * stepped either; they hold the topology (source and sink ranges, the bank and controller address
 * maps) that the Monitor's client tracing and the remap read. On those links:
 *   - the library's Monitor (tl_monitor(i), default 1; tl_monitor_mode fatal | report): every rule of
 *     docs/TileLink_Monitor_Rules.md that a one-beat message can break (opcode, param, size, alignment,
 *     IDs, responses, concurrency, permissions); the burst rules hold trivially;
 *   - CoherenceCheck (tl_coherence_check(i), default 1) on the L1s' links: C-SWMR at each Grant's
 *     accept, C-GRANT at its offer, and C-PERM at every access a TL-C L1 serves from its copy (TLC_L1's
 *     report through TLPort::forEndpoint reaches this port), against the permission its link shows.
 *     That permission rises when the Grant enters the L1's receive queue and falls when the L1's
 *     ProbeAck or Release enters its NI's inject queue, so it is never below the L1's own: a report is
 *     never a false alarm.
 *   What the message-level link does not see: the network's own ordering inside a channel (the NoC is
 *   one agent; the monitor sees the order in which each endpoint accepts), and beats (w is the line).
 *
 * Accounting: the Network logs every TileLink copy at its ejection with comp (opcode << 8) | channel
 * (Network::eject), as TLPort does, and the Logger's TileLink columns (Probe, Completion) are on.
 *
 * Configuration (the interconnect sub-map; every key optional):
 *   tl_o(i) 4          source bits of an L1's link (2^o per pool)
 *   tl_i(i) 4          sink bits of an LLC bank's link (2^i outstanding Grants)
 *   tl_mem_o(i) 6      source bits of a bank's memory link (7 classes)
 *   block_bytes(i) 64  the line: the links' width w and the monitors' block
 *   tl_monitor(i) 1, tl_monitor_mode(s) fatal, tl_coherence_check(i) 1
 * Refused at construction: inject_queue_depth below the number of L1s (a bank's probe fan-out reserves
 * one B packet per cache, as tx_depth_B on the crossbar, TLC_Tables s11.1 item 11), an endpoint of a
 * TileLink role that is not a network endpoint.
 */
#ifndef _TLNETPORT_H
#define _TLNETPORT_H
#include "TLPort.h"
#include "Configurable.h"
#include "tilelink/Kernel.h"
#include "tilelink/Monitor.h"
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace octopus { namespace net { class Network; class NetworkInterface; } }
namespace octopus { namespace tl { class Xbar; } }
namespace octopus
{
    class TLNetBoundary;

    class TLNetPort : public TLPort
    {
    public:
        TLNetPort(TLNetBoundary *b, net::NetworkInterface *ni, int id, const std::string &name, tl::Role role, bool memory_side);

        // ---- CommunicationInterface: the endpoint's side (the NI behind it)
        bool peekMessage(Message *out_msg) override;
        void popFrontMessage() override;
        bool pushMessage(Message &msg, uint64_t cycle = 0, MessageType type = MessageType::REQUEST) override;
        // a TileLink message: the NI's receive queue of its class; an untagged local fill (BaseController's
        // H1 forward, the perfect LLC) waits here, on the D receive path, as on TLPort
        bool pushMessage2RX(Message &msg, MessageType type = MessageType::REQUEST) override;
        bool canAcceptRX(MessageType type = MessageType::REQUEST) override;
        int  txFreeSlotsCh(int tl_channel) override;
        void txReserveCh(int tl_channel, int n) override;
        void txReleaseCh(int tl_channel, int n) override;
        // the identifier pools (txFreeIdsCh, txReserveIdsCh, txReleaseIdsCh) are TLPort's
        void accessPerformed(uint64_t addr, bool store) override;

        // ---- tl::Agent: the kernel is never stepped; the port drives its link from the NI's events
        void offer(uint64_t) override {}
        void accept(uint64_t) override {}
        void commit(uint64_t) override {}
        bool idle() const override { return true; }

        // ---- the boundary
        void delivered(int cls, Message &m);           // the NI's delivery hook (NetworkInterface::setDeliverHook)
        void setBases(uint64_t src_base, uint64_t sink_base) { m_src_base = src_base; m_sink_base = sink_base; }
        net::NetworkInterface *ni() const { return m_ni; }
        std::string dump() const;

    private:
        // one beat offered and accepted on this port's link in the network's tick (the message-level link)
        void present(const Message &m, tl::Channel ch, uint64_t source, uint64_t sink);

        TLNetBoundary *m_b;
        net::NetworkInterface *m_ni;
        uint64_t m_src_base = 0, m_sink_base = 0;      // this link's range in its Xbar's namespaces (TileLink s6.4)
        std::deque<Message> m_local;                   // untagged local fills (memory port)
        bool m_peek_local = false;
    };

    class TLNetBoundary
    {
    public:
        // the endpoints by TileLink role; *_shift: the bank and controller interleave (homeOf)
        struct Endpoints
        {
            std::vector<int> l1s, banks, mem_ports, mems;
            int bank_shift = 0, mem_shift = 0;
        };
        TLNetBoundary(net::Network *net, const Configurable::ParametersMap &map, const Endpoints &e);
        ~TLNetBoundary();

        CommunicationInterface *getInterfaceFor(int id);   // fatal when id has no TileLink role
        TLNetPort *port(int id) const;
        net::Network &network() const { return *m_net; }
        uint64_t tick() const;
        tl::Kernel &kernel() { return *m_kernel; }

        // the Xbars' views (TileLink s6.3, s6.4): the endpoint an address routes to (A, C), that owns a
        // source of the slaves' namespace (D), a sink of the masters' namespace (E); -1 if none
        int endpointOfAddress(uint64_t addr, bool outer) const;
        int endpointOfSource(uint64_t source, bool outer) const;
        int endpointOfSink(uint64_t sink) const;
        uint64_t sourceBaseOf(int master_id) const;    // a B's per-target source on the slave's link

        uint64_t monitorViolations() const { return tl::Monitor::totalViolations(m_monitors); }
        std::string dump() const;
        void finish();                                 // end of run (atexit, once): the verdict lines
        [[noreturn]] static void fatal(const std::string &msg);

    private:
        int geti(const std::string &k, int def) const;
        std::string gets(const std::string &k, const std::string &def) const;

        net::Network *m_net;
        Configurable::ParametersMap m_map;
        std::unique_ptr<tl::Kernel> m_kernel;
        std::vector<std::unique_ptr<tl::Monitor>> m_monitors;
        tl::Xbar *m_inner = nullptr, *m_outer = nullptr;
        std::map<int, TLNetPort *> m_ports;
        std::map<const tl::Link *, int> m_link_endpoint;   // a link -> the endpoint at its far end from the Xbar
        std::vector<int> m_l1s, m_banks, m_mem_ports, m_mems;
        bool m_finished = false;
        static TLNetBoundary *s_instance;
        static void atExit();
    };
}

#endif /* _TLNETPORT_H */
