/*
 * File  :      TileLinkInterconnect.cpp
 * interconnect_type TileLink: the library's Xbar between TLPorts, ticked from Octopus's clock.
 * Specification: docs/TileLink_Plan.md s3.2 (one clock per kernel), s5.2, s5.3 I-A.
 */
#include "../../../header/Interconnect/TileLink/TileLinkInterconnect.h"
#include "../../../header/CacheControllers/BaseController.h"
#include "../../../header/HangDump.h"
#include "../../../header/Logger.h"
#include "tilelink/agents/Xbar.h"
#include "tilelink/GraphCheck.h"
#include "tilelink/Watchdog.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace octopus
{
    TileLinkInterconnect *TileLinkInterconnect::s_instance = nullptr;
    static bool s_fatal = false;   // the run ended on the adapter's fatal path: no end-of-run summary

    // The single fatal path of the adapter: one message, the hang dump if enabled, exit status 2
    // (as net::fatal and tl::fatal). Never status 0.
    void TileLinkInterconnect::fatal(const std::string &msg)
    {
        std::cout.flush();
        s_fatal = true;
        std::cerr << "TileLink: " << msg << std::endl;
        if (std::getenv("OCTOPUS_HANG_DUMP")) HangDump::run();
        std::cerr.flush();
        std::exit(2);
    }

    // ------------------------------------------------------------------ configuration
    int TileLinkInterconnect::geti(const std::string &k, int def) const
    {
        auto it = parameters.find(k);
        if (it == parameters.end()) return def;
        if (!std::holds_alternative<int>(it->second.value)) fatal("configuration key " + k + " must be an integer (i)");
        return std::get<int>(it->second.value);
    }
    std::string TileLinkInterconnect::gets(const std::string &k, const std::string &def) const
    {
        auto it = parameters.find(k);
        if (it == parameters.end()) return def;
        if (!std::holds_alternative<std::string>(it->second.value)) fatal("configuration key " + k + " must be a string (s)");
        return std::get<std::string>(it->second.value);
    }
    std::vector<int> TileLinkInterconnect::getvi(const std::string &k) const
    {
        auto it = parameters.find(k);
        if (it == parameters.end()) return {};
        if (!std::holds_alternative<std::vector<int>>(it->second.value)) fatal("configuration key " + k + " must be an integer vector (vi)");
        std::vector<int> v = std::get<std::vector<int>>(it->second.value);
        if (!v.empty() && v[0] < 0) v.clear();
        return v;
    }

    static unsigned bitsFor(uint64_t n) { unsigned b = 0; while ((uint64_t(1) << b) < n) b++; return b; }

    TileLinkInterconnect::TileLinkInterconnect(ParametersMap map, std::string pname, std::string config_path, std::string name)
        : ClockedObj(0), Configurable(map, config_path, name, pname)
    {
        build();
        s_instance = this;
        static bool registered = false;
        if (!registered) { std::atexit(atExit); registered = true; }
        HangDump::add([this]() { std::cerr << dump(); });
        Logger::getLogger()->enableTileLinkColumns();
    }

    TileLinkInterconnect::~TileLinkInterconnect()
    {
        finish();
        m_monitors.clear();          // the monitors unhook from the links before the kernel goes
        if (s_instance == this) s_instance = nullptr;
    }

    void TileLinkInterconnect::build()
    {
        m_clk_period = (uint64_t)std::max(0, geti("m_clk_period", -1));   // 0 = derived at elaboration
        m_master_ids = getvi("master_ids");
        m_slave_ids = getvi("slave_ids");
        m_memory_ids = getvi("memory_ids");
        if (m_master_ids.empty() || m_slave_ids.empty())
            fatal("interconnect_type TileLink needs master_ids(vi) and slave_ids(vi) (the L1s and the LLC)");
        for (int a : m_master_ids)
            if (std::find(m_slave_ids.begin(), m_slave_ids.end(), a) != m_slave_ids.end())
                fatal("endpoint " + std::to_string(a) + " is in both master_ids and slave_ids");

        tl::LinkParams base;
        std::string level = gets("level", "C");
        if (!tl::parseLevel(level, base.level)) fatal("level must be UL, UH or C, got " + level);
        base.w = (unsigned)geti("w", 8); base.a = (unsigned)geti("a", 64); base.z = (unsigned)geti("z", 3);
        unsigned o = (unsigned)geti("o", 4), i = (unsigned)geti("i", 2);
        int slave_o = geti("slave_o", -1), master_i = geti("master_i", -1);
        // TileLink s6.4 (the Xbar's rule): the slaves' links hold every master's sources in one
        // namespace, the masters' links every slave's sinks
        tl::LinkParams mp = base, sp = base;
        mp.o = o; sp.i = base.level == tl::Level::C ? i : 0;
        sp.o = slave_o >= 0 ? (unsigned)slave_o : bitsFor((uint64_t)m_master_ids.size() << o);
        mp.i = master_i >= 0 ? (unsigned)master_i : (base.level == tl::Level::C ? bitsFor((uint64_t)m_slave_ids.size() << i) : 0);

        TLPort::Depths d;
        int rx = geti("rx_depth", 4), tx = geti("tx_depth", 4);
        for (int c = 0; c < tl::NUM_CHANNELS; c++)
        {
            std::string n = TLPort::channelName(TLPort::toOctopus(c));
            int r = geti("rx_depth_" + n, -1), t = geti("tx_depth_" + n, -1);
            d.rx[c] = r >= 0 ? r : rx; d.tx[c] = t >= 0 ? t : tx;
        }

        tl::KernelOptions ko;
        ko.reset_cycles = 0;          // plan s3.2: no reset inside Octopus
        ko.seed = (uint64_t)geti("seed", 1);
        ko.data_mode = tl::DataMode::None;   // the ports choose per message (data mode full when Message::data is set)
        m_kernel.reset(new tl::Kernel(ko));

        // the Xbar's own keys (agents/Xbar.h); routes name slave endpoints, here mapped to their links
        tl::Config xc;
        try
        {
            xc.set("arbitration(s)=" + gets("arbitration", "rr"));
            xc.set("tdm_slot(i)=" + std::to_string(geti("tdm_slot", 1)));
            xc.set("depth(i)=" + std::to_string(geti("xbar_depth", 2)));
            auto rit = parameters.find("routes");
            if (rit != parameters.end() && std::holds_alternative<std::vector<std::string>>(rit->second.value))
            {
                std::string line = "routes(vs)=";
                bool any = false;
                for (const std::string &e : std::get<std::vector<std::string>>(rit->second.value))
                {
                    if (e.empty() || e == "-1") continue;
                    size_t c1 = e.find(':');
                    if (c1 == std::string::npos) fatal("routes entry '" + e + "' must be <slave id>:<base>:<size>");
                    int sid = std::atoi(e.substr(0, c1).c_str());
                    if (std::find(m_slave_ids.begin(), m_slave_ids.end(), sid) == m_slave_ids.end())
                        fatal("routes entry '" + e + "' names " + std::to_string(sid) + ", which is not in slave_ids");
                    line += (any ? "," : "") + std::string("S") + std::to_string(sid) + e.substr(c1);
                    any = true;
                }
                if (any) xc.set(line);
            }
        }
        catch (tl::ConfigError &e) { fatal(e.what()); }

        // agents in evaluation order (any order is correct, plan s3.2): masters, the crossbar, slaves
        auto isMem = [&](int id) { return std::find(m_memory_ids.begin(), m_memory_ids.end(), id) != m_memory_ids.end(); };
        for (int id : m_master_ids)
        {
            TLPort *p = new TLPort(this, id, "port" + std::to_string(id), tl::Role::Master, d, isMem(id));
            m_kernel->add(std::unique_ptr<tl::Agent>(p));
            m_ports[id] = p;
        }
        try { m_xbar = new tl::Xbar("xbar", xc); }
        catch (tl::ConfigError &e) { fatal(e.what()); }
        m_kernel->add(std::unique_ptr<tl::Agent>(m_xbar));
        for (int id : m_slave_ids)
        {
            TLPort *p = new TLPort(this, id, "port" + std::to_string(id), tl::Role::Slave, d, isMem(id));
            m_kernel->add(std::unique_ptr<tl::Agent>(p));
            m_ports[id] = p;
        }
        for (int id : m_master_ids) m_link_endpoint[&m_kernel->connect("L" + std::to_string(id), mp, *m_ports[id], *m_xbar)] = id;
        for (int id : m_slave_ids)  m_link_endpoint[&m_kernel->connect("S" + std::to_string(id), sp, *m_xbar, *m_ports[id])] = id;

        // plan s6: a monitor on every link, attached before the first cycle
        if (geti("tl_monitor", 1) != 0)
        {
            tl::MonitorOptions mo;
            std::string mode = gets("tl_monitor_mode", "fatal");
            if (!tl::MonitorOptions::parseMode(mode, mo.mode)) fatal("tl_monitor_mode must be fatal or report, got " + mode);
            mo.block_bytes = (unsigned)geti("block_bytes", 64);
            m_monitors = tl::Monitor::attachAll(*m_kernel, mo);
        }
        if (geti("tl_graph_check", 1) != 0) m_kernel->addObserver(std::unique_ptr<tl::Observer>(new tl::GraphCheck()));
        if (geti("tl_watchdog", 0) != 0)
        {
            tl::WatchdogOptions wo;
            int bound = geti("progress_bound", -1);
            if (bound > 0) wo.bound = (uint64_t)bound;
            m_kernel->addObserver(std::unique_ptr<tl::Observer>(new tl::Watchdog(wo)));
        }
        std::string tr = gets("transcript", "");
        if (!tr.empty()) m_kernel->enableTranscript(tr);
    }

    // ------------------------------------------------------------------ elaboration
    void TileLinkInterconnect::elaborate(const std::vector<BaseController *> &controllers)
    {
        // plan s3.2: one clock per kernel. Every controller shares one period, and the kernel steps
        // once per period (half-period edges, the falling one steps).
        uint64_t period = 0; int first = -1;
        for (BaseController *c : controllers)
        {
            uint64_t p = c->getClkPeriod();
            if (period == 0) { period = p; first = c->id(); continue; }
            if (p != period)
                fatal("one clock per kernel (docs/TileLink_Plan.md s3.2): controller " + std::to_string(first) + " has period " +
                      std::to_string(period) + " but controller " + std::to_string(c->id()) + " has " + std::to_string(p) +
                      "; L1s and LLC with different clocks need a clock-crossing adapter, which is out of scope (plan s9)");
        }
        if (period != 0)
        {
            if (period % 2 != 0) fatal("the controllers' period " + std::to_string(period) + " is odd: the kernel steps on the falling edge of half that period");
            if (m_clk_period == 0) m_clk_period = period / 2;
            else if (m_clk_period * 2 != period)
                fatal("interconnect.m_clk_period " + std::to_string(m_clk_period) + " is not half the controllers' period " + std::to_string(period) +
                      " (one clock per kernel, plan s3.2); leave it -1 to derive it");
        }
        elaborate();
    }

    void TileLinkInterconnect::elaborate()
    {
        if (m_elaborated) return;
        if (m_clk_period == 0) m_clk_period = 50;      // no controllers to derive from (tests): the default core period / 2
        m_kernel->elaborate();                         // link parameters, capabilities, GraphCheck: fatal on a refusal
        m_elaborated = true;
    }

    // ------------------------------------------------------------------ clock
    void TileLinkInterconnect::cycleProcess()
    {
        if (m_edges % 2 == 1) step();                  // falling edge, as Mesh and Network
        m_edges++;
    }
    void TileLinkInterconnect::step()
    {
        if (!m_elaborated) elaborate();
        m_kernel->step();
    }

    CommunicationInterface *TileLinkInterconnect::getInterfaceFor(int id)
    {
        TLPort *p = port(id);
        if (!p) fatal("no endpoint " + std::to_string(id) + " on the TileLink interconnect (master_ids, slave_ids)");
        return p;
    }
    TLPort *TileLinkInterconnect::port(int id) const
    {
        auto it = m_ports.find(id);
        return it == m_ports.end() ? nullptr : it->second;
    }

    // ------------------------------------------------------------------ records and routing
    void TileLinkInterconnect::stash(uint64_t tl_id, const Message &msg, int src, int dst)
    {
        Record &r = m_records[tl_id];
        r.msg.copy(msg);
        if (r.msg.data) { delete[] r.msg.data; r.msg.data = NULL; }   // the bytes travel on the wire
        r.src = src; r.dst = dst;
    }
    bool TileLinkInterconnect::unstash(uint64_t tl_id, Message *out, int *src, int *dst)
    {
        auto it = m_records.find(tl_id);
        if (it == m_records.end()) return false;
        out->copy(it->second.msg);
        *src = it->second.src; *dst = it->second.dst;
        m_records.erase(it);
        return true;
    }
    const Message *TileLinkInterconnect::record(uint64_t tl_id) const
    {
        auto it = m_records.find(tl_id);
        return it == m_records.end() ? nullptr : &it->second.msg;
    }
    int TileLinkInterconnect::endpointOfAddress(uint64_t address) const
    {
        tl::Link *l = m_xbar->routeOf(address);
        if (!l) return -1;
        auto it = m_link_endpoint.find(l);
        return it == m_link_endpoint.end() ? -1 : it->second;
    }
    int TileLinkInterconnect::endpointOfSource(uint64_t source) const
    {
        for (int id : m_master_ids)
        {
            const tl::Link *l = m_ports.at(id)->link();
            uint64_t b = m_xbar->sourceBase(*l);
            if (source >= b && source - b < l->params().numSources()) return id;
        }
        return -1;
    }
    int TileLinkInterconnect::endpointOfSink(uint64_t sink) const
    {
        for (int id : m_slave_ids)
        {
            const tl::Link *l = m_ports.at(id)->link();
            uint64_t b = m_xbar->sinkBase(*l);
            if (sink >= b && sink - b < l->params().numSinks()) return id;
        }
        return -1;
    }
    uint64_t TileLinkInterconnect::sourceBaseOf(int master_id) const
    {
        TLPort *p = port(master_id);
        if (!p || p->role() != tl::Role::Master) fatal("a B message addressed to " + std::to_string(master_id) + ", which is not a TileLink master");
        return m_xbar->sourceBase(*p->link());
    }

    // ------------------------------------------------------------------ observability
    uint64_t TileLinkInterconnect::monitorViolations() const { return tl::Monitor::totalViolations(m_monitors); }

    std::string TileLinkInterconnect::dump() const
    {
        std::ostringstream o;
        o << "[TL] TileLink interconnect: cycle " << m_kernel->cycle() << ", " << m_records.size() << " messages in flight, "
          << monitorViolations() << " monitor violations\n";
        for (auto &kv : m_ports) o << kv.second->dump();
        return o.str();
    }

    void TileLinkInterconnect::finish()
    {
        if (m_finished || !m_kernel || s_fatal) return;
        m_finished = true;
        // The end-of-run rules (M-BURST-4/5, M-RESP-7) only in report mode: an Octopus run may end
        // with write-backs in flight, and a fatal exit from an atexit handler is not allowed.
        bool report = !m_monitors.empty() && m_monitors[0]->options().mode == tl::MonitorOptions::Report;
        if (report) tl::Monitor::finishAll(m_monitors, m_kernel->cycle());
        if (!m_monitors.empty())
        {
            uint64_t v = monitorViolations();
            std::cerr << "TileLink: " << m_monitors.size() << " link monitors, " << v << " violations, " << m_kernel->cycle() << " cycles" << std::endl;
            for (auto &m : m_monitors) if (m->violations()) std::cerr << "TileLink: " << m->summary() << std::endl;
        }
        if (geti("tl_stats", 1) != 0 && m_elaborated)
        {
            std::string dir = Logger::getLogger()->reportPath();
            if (!dir.empty())
            {
                m_kernel->finishStats();
                std::ofstream f(dir + "/TLStats.csv");
                if (f) f << m_kernel->stats().csv();
            }
        }
        if (m_kernel->transcript()) m_kernel->transcript()->flush();
    }
    void TileLinkInterconnect::atExit() { if (s_instance) s_instance->finish(); }
}
