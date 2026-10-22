/*
 * File  :      TileLinkInterconnect.h
 * interconnect_type TileLink (docs/TileLink_Plan.md s5.3 I-A): the TileLink library's Xbar between
 * the L1s (masters) and the LLC (slave), one TLPort per endpoint, the kernel ticked from Octopus's
 * clock. A ClockedObj and a Configurable like net::Network, with the same getInterfaceFor(id).
 *
 * Clock (plan s3.2, one clock per kernel): m_clk_period is half the controllers' period and the
 * kernel steps on the falling edge, as Mesh and Network do, so one TileLink cycle is one core
 * cycle and a beat accepted in cycle t is seen by the controllers at t+1. elaborate() refuses a
 * system whose controllers do not share one period (a clock-crossing adapter is out of scope,
 * plan s9) and, with m_clk_period -1 (the default), derives the half period from them.
 * Inside Octopus reset_cycles is 0 (plan s3.2).
 *
 * Configuration (configuration/Interconnect/TileLink.csv, the `interconnect` sub-map):
 *   master_ids(vi)        endpoints that are TileLink masters (the L1s), one link L<id> each
 *   slave_ids(vi)         endpoints that are slaves (the LLC), one link S<id> each
 *   memory_ids(vi)        endpoints whose port is a memory-side link (their events log as MEM_BUS;
 *                         plan s5.3 I-B); -1 = none
 *   level(s), w(i), a(i), z(i)    every link's level and width, address and size bits
 *   o(i)                  source bits of a master link (pool of 2^o per namespace)
 *   i(i)                  sink bits of a slave link (pool of 2^i)
 *   slave_o(i), master_i(i)       -1 = derived: the slave links' o holds every master's sources,
 *                         the master links' i every slave's sinks (TileLink s6.4, the Xbar's rule)
 *   arbitration(s), tdm_slot(i), xbar_depth(i)    the Xbar's (agents/Xbar.h)
 *   routes(vs)            <slave id>:<base>:<size> entries (A/C address map); empty = one slave
 *                         takes everything. Octopus's line-interleaved banks cannot be written as
 *                         ranges, so a banked preset is refused at its first mismatching message
 *   rx_depth(i), tx_depth(i), rx_depth_A..E(i), tx_depth_A..E(i)   TLPort queues (-1 = the default)
 *   tl_monitor(i)         1: a Monitor on every link (plan s6: a run is valid only with 0 violations)
 *   tl_monitor_mode(s)    fatal | report; block_bytes(i) the TL-C block (the line size)
 *   tl_graph_check(i), tl_watchdog(i), progress_bound(i)   the library's checkers (the watchdog is
 *                         off by default: Octopus has its own OCTOPUS_HANG_DUMP)
 *   seed(i), transcript(s)  the kernel's seed; a path for the beat transcript (empty = none)
 *   tl_stats(i)           1: TLStats.csv in the report directory at the end of the run
 */
#ifndef _TILELINK_INTERCONNECT_H
#define _TILELINK_INTERCONNECT_H
#include "ClockManager.h"
#include "Configurable.h"
#include "CommunicationInterface.h"
#include "TLPort.h"
#include "tilelink/Kernel.h"
#include "tilelink/Monitor.h"
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace octopus { class BaseController; }
namespace octopus { namespace tl { class Xbar; } }
namespace octopus
{
    class TileLinkInterconnect : public ClockedObj, public Configurable
    {
    public:
        TileLinkInterconnect(ParametersMap map, std::string pname = "",
                             std::string config_path = std::string(CONFIGURATION_PATH) + std::string(INTERCONNECT),
                             std::string name = "TileLink");
        ~TileLinkInterconnect();
        void cycleProcess() override;
        void init() override {}

        CommunicationInterface *getInterfaceFor(int id);
        TLPort *port(int id) const;                  // null when id has no port
        // After every controller exists: one clock per kernel (plan s3.2), then the kernel's
        // elaboration (link parameters, capabilities, GraphCheck). Fatal on a refusal.
        void elaborate(const std::vector<BaseController *> &controllers);
        void elaborate();                            // the kernel only (tests)
        void step();                                 // one TileLink cycle (the falling edge; tests call it directly)
        tl::Kernel &kernel() { return *m_kernel; }
        tl::Xbar &xbar() { return *m_xbar; }
        uint64_t cycle() const { return m_kernel->cycle(); }

        // ---- used by TLPort
        // The Octopus side of a message in flight, keyed by tl::Msg::id (kept by every agent's clone).
        void stash(uint64_t tl_id, const Message &msg, int src, int dst);
        bool unstash(uint64_t tl_id, Message *out, int *src, int *dst);
        const Message *record(uint64_t tl_id) const;
        int endpointOfAddress(uint64_t address) const;   // the slave the Xbar routes A/C to, -1 if unmapped
        int endpointOfSource(uint64_t source) const;     // the master owning a source of the slaves' namespace
        int endpointOfSink(uint64_t sink) const;         // the slave owning a sink of the masters' namespace
        uint64_t sourceBaseOf(int master_id) const;      // where a master's sources start in the slaves' namespace
        size_t inFlight() const { return m_records.size(); }

        // ---- observability
        uint64_t monitorViolations() const;
        std::string dump() const;                    // hang dump: every port's queues and pools
        // End of run (atexit or destructor, once): the monitors' end-of-run rules in report mode,
        // their summaries on stderr, TLStats.csv when tl_stats is set.
        void finish();
        [[noreturn]] static void fatal(const std::string &msg);

    private:
        void readConfig();
        void build();
        int  geti(const std::string &k, int def) const;
        std::string gets(const std::string &k, const std::string &def) const;
        std::vector<int> getvi(const std::string &k) const;   // -1 or absent = empty
        static void atExit();

        std::unique_ptr<tl::Kernel> m_kernel;        // owns the ports and the Xbar; destroyed after the monitors
        std::vector<std::unique_ptr<tl::Monitor>> m_monitors;
        tl::Xbar *m_xbar = nullptr;
        std::map<int, TLPort *> m_ports;
        std::map<const tl::Link *, int> m_link_endpoint;   // link -> the Octopus endpoint at its far end from the Xbar
        std::vector<int> m_master_ids, m_slave_ids, m_memory_ids;
        struct Record { Message msg; int src = -1, dst = -1; };
        std::unordered_map<uint64_t, Record> m_records;
        uint64_t m_edges = 0;
        bool m_elaborated = false, m_finished = false;
        static TileLinkInterconnect *s_instance;
    };
}

#endif /* _TILELINK_INTERCONNECT_H */
