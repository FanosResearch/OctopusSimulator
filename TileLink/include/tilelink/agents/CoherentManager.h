/*
 * File  :      CoherentManager.h
 * CoherentManager (docs/TileLink_Plan.md §3.5): the inclusive TL-C manager, executing the manager
 * table of the policy engine (plan §3.7; default tables/TLC_MESI_LLC.csv, docs/TileLink_TLC_Tables.md
 * §3; protocol(s)=MSI selects tables/TLC_MSI_LLC.csv). One TL-C slave port per inner link (one link
 * per master, or one link behind an Xbar: both), one TL-UH master port outward (to a RamSlave).
 *
 * Directory per block: the sharer set, the owner (Tip), the probe counter (Probes sent, TLC_Tables
 * §1.2 rule 4), the LLC copy (sets x ways, LRU), the parked request (§1.2 rule 3) and the Grant's
 * sink. Holders are clients: the masters behind the inner links, identified by source range. A
 * link whose master is an Xbar (possibly behind one-in-one-out adapters such as Throttle or Buffer,
 * which keep sources) has one client per Xbar upstream link, at the base the Xbar assigns
 * (Xbar::sourceBase, §6.4); any other link is one client owning its whole source range. A Probe to a
 * client carries the client's base source, so the Xbar routes it by source (Table 14) and the
 * ProbeAck comes back with that source (§9.3.5: copied from b_source).
 *
 * The engine around a cell (§1.2 plus the T6 additions listed in the README):
 *   - receive queues per channel (inner A, C, E; outer D), always ready (outside reset); each
 *     cycle the first message of each queue, in the order E, outer D, C, A, whose cell is not Stall
 *     (and, for A, whose resources are available) runs; a Stall leaves it parked in its queue and
 *     it is re-decoded every cycle, i.e. when the block changes (§1.2 rule 2); a parked C message
 *     never blocks E, a parked A never blocks C;
 *   - an inner-A cell that does not answer its request parks it on the block; the answering
 *     action (Grant*, AccessAckData_in, Execute, ForwardIn, HintAck*) answers the consumed inner-A
 *     message if the cell consumed one, else the parked request;
 *   - an Acquire is popped only with a free sink on its link (2^i, plan §5.2), reserved for its
 *     Grant and freed by the GrantAck; a cell that allocates the LLC copy (N -> NT_d) is popped only
 *     with a free way in its set, else a Replacement is raised on the set's LRU block whose
 *     Replacement cell runs (I, B, T), one eviction per set at a time;
 *   - outer requests leave in order through the outer port, a source from 2^o each; the
 *     PutFullData of a cell whose next state is stable (the eviction rows: I + Replacement,
 *     BN_e/TN_e + last ProbeAck) is a port transaction: its AccessAck is consumed by the port
 *     (§1.2 rule 7) and, until it arrives, later outer requests for that block are held (H1);
 *   - uncached Get / Put / Atomic and their responses are reported to CoherenceCheck::of(kernel()).
 *
 * Keys (agent[k].*): protocol(s) MESI|MSI, table(s) (overrides protocol), block_bytes(i) 64,
 * sets(i) 1024, ways(i) 8, addr_base(i)/addr_size(i) (the inner region declared to the monitor),
 * debug(i) 0.
 */
#ifndef _TL_AGENTS_COHERENTMANAGER_H
#define _TL_AGENTS_COHERENTMANAGER_H
#include "tilelink/Agent.h"
#include "tilelink/Monitor.h"
#include "tilelink/coh/Policy.h"
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace octopus { namespace tl
{
    class CoherentManager : public Agent, public SlaveLimits
    {
    public:
        CoherentManager(const std::string &name, const Config &params);
        ~CoherentManager();

        // TL-C toward the masters, TL-UH toward memory.
        Level capability(const Link &, Role r) const override { return r == Role::Slave ? Level::C : Level::UH; }
        void reset(uint64_t reset_cycles) override;
        void offer(uint64_t cycle) override;
        void accept(uint64_t cycle) override;
        void commit(uint64_t cycle) override;
        bool idle() const override;
        // Plan §3.6: latency = one transaction, at most 8 sequential hops (Probe, ProbeAck, outer
        // Get, AccessAckData, Grant, GrantAck, an eviction's write and its ack), each bounded by
        // the slowest non-cache agent's declared latency; queue = 4 requests per client.
        ProgressHints hints() const override;
        int declaredMaxSize(const Link &l) const override;
        std::vector<AddrRegion> declaredRegions(const Link &l) const override;

        // ---- inspection (tests)
        const coh::ManagerPolicy &policy() const { return *m_policy; }
        std::string blockState(uint64_t address) const;            // "N" when no entry
        bool blockValue(uint64_t address, unsigned bytes, std::vector<uint8_t> &out) const;   // the LLC copy, Full data
        int owner(uint64_t address) const;                          // client id or -1
        std::vector<int> sharers(uint64_t address) const;
        unsigned numClients() const { return unsigned(m_clients.size()); }
        std::string clientName(int c) const;                        // the client's link name
        unsigned blockBytes() const { return m_bb; }
        std::function<void(const coh::Transition &)> onTransition;

        struct Counters
        {
            uint64_t acquires = 0, grants = 0, denied_grants = 0, probes = 0, probe_acks = 0, releases = 0;
            uint64_t evictions = 0, outer_reads = 0, outer_writes = 0, uncached = 0, cmos = 0, hint_acks = 0;
            uint64_t stalls = 0, h1_holds = 0;
        };
        const Counters &counters() const { return m_c; }

    private:
        struct Entry;
        struct OutQ { std::deque<MsgPtr> q; unsigned beat = 0; };
        struct Port
        {
            Link *link = nullptr;
            OutQ  outB, outD;
            std::vector<uint64_t> free_sinks;
        };
        struct Client { int port = -1; uint64_t base = 0, count = 0; std::string name; };
        struct Rx { MsgPtr msg; int port = -1; int client = -1; };
        struct OuterRec { uint64_t block = 0; bool evict = false; MsgPtr req; };

        Link *outer() const;
        uint64_t blockOf(uint64_t a) const { return a & ~uint64_t(m_bb - 1); }
        unsigned setOf(uint64_t block) const { return unsigned((block / m_bb) % m_sets); }
        int stateOf(uint64_t block) const;
        Entry *entry(uint64_t block);
        Entry &makeEntry(uint64_t block);
        unsigned waysUsed(unsigned set) const;
        int clientOf(int port, uint64_t source) const;
        void discoverClients(Link *l, int port, uint64_t base, int depth);
        coh::DirView view(uint64_t block, int client) const;
        bool isStall(int state, int event) const;
        bool raiseReplacement(unsigned set, uint64_t cycle);

        void execute(uint64_t block, int event, const Rx *rx, uint64_t cycle);
        void respond(int port, const MsgHeader &h, const MsgPtr &data_src, uint64_t geom);
        void sendOuter(const MsgPtr &m, uint64_t block, bool evict);
        void executeAccess(Entry &e, const Msg &req, Msg &resp, uint64_t cycle);
        void reportForwarded(const Msg &req, const Msg &resp, uint64_t cycle);
        void report(const coh::Transition &t);
        void reportStall(uint64_t block, int event, const Rx &rx, uint64_t cycle);

        bool processE(uint64_t cycle);
        bool processOuterD(uint64_t cycle);
        bool processC(uint64_t cycle);
        bool processA(uint64_t cycle);
        void promoteOuter();

        std::unique_ptr<coh::ManagerPolicy> m_policy;
        unsigned m_bb = 64, m_lg = 6, m_sets = 1024, m_ways = 8;
        AddrRegion m_region;
        bool m_debug = false;

        std::vector<Port> m_ports;
        std::vector<Client> m_clients;
        std::map<uint64_t, std::unique_ptr<Entry>> m_dir;
        std::deque<Rx> m_rxA, m_rxC, m_rxE, m_rxD;
        std::map<std::pair<int, uint64_t>, uint64_t> m_grant_sink;   // (port, sink) -> block (unanswered Grant)
        std::set<uint64_t> m_evicting;                               // blocks a Replacement is evicting

        // the outer port
        std::deque<std::pair<MsgPtr, OuterRec>> m_outer_pending;     // not yet given a source
        OutQ m_outerA;
        std::vector<uint64_t> m_outer_free;
        std::map<uint64_t, OuterRec> m_outer_out;                    // source -> outstanding request
        std::map<uint64_t, unsigned> m_h1;                           // block -> unacknowledged eviction writes
        uint64_t m_use = 0;
        std::map<const Msg *, int> m_stall_seen;                     // parked message -> state+1 it was reported in
        Counters m_c;
    };
}}
#endif /* _TL_AGENTS_COHERENTMANAGER_H */
