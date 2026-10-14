/*
 * File  :      Network.cpp
 * The per-tick engine. Specification: docs/NoC_Architecture_Spec.md §7-§9, §11, §13, §15, §16.
 */
#include "../../../header/Interconnect/Network/Network.h"
#include "../../../header/HangDump.h"
#include "../../../header/Interconnect/Network/TrafficGenerator.h"
#include "../../../header/Arbiters/FCFSArbiter.h"
#include "../../../header/Arbiters/RRArbiter.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace octopus { namespace net
{
    Network *Network::s_instance = nullptr;

    std::vector<int> NetConfig::classPriority() const
    {
        if (num_classes == 2) return {1, 0};
        return {CLASS_RESPONSE, CLASS_FORWARD, CLASS_REQUEST};
    }

    // ------------------------------------------------------------------ construction
    Network::Network(ParametersMap map, std::string pname, std::string config_path, std::string name)
        : ClockedObj(0), Configurable(map, config_path, name, pname)
    {
        if (const char *d = std::getenv("OCTOPUS_NET_DEBUG")) m_debug = d;
        if (const char *h = std::getenv("OCTOPUS_HANG_DUMP")) m_hang_ticks = std::atol(h);
        try
        {
            readConfig();
            buildTopology();
            buildRouters();
        }
        catch (ConfigError &e) { fatal(e.what()); }
        catch (std::out_of_range &e) { fatal(std::string("missing configuration key (") + e.what() + ")"); }
        s_instance = this;
        static bool registered = false;
        if (!registered) { std::atexit(statsAtExit); registered = true; }
        HangDump::add([this]() { std::cerr << dump(); });
        Logger::getLogger()->enableNocColumns();
    }
    Network::~Network()
    {
        flushStats();
        for (auto &kv : m_nis) delete kv.second;
        if (s_instance == this) s_instance = nullptr;
    }

    void Network::readConfig()
    {
        auto has  = [&](const std::string &k) { return parameters.count(k) > 0; };
        auto geti = [&](const std::string &k) { return std::get<int>(parameters.at(k).value); };
        auto gets = [&](const std::string &k) { return std::get<std::string>(parameters.at(k).value); };
        auto getvi = [&](const std::string &k) { return std::get<std::vector<int>>(parameters.at(k).value); };
        auto optvi = [&](const std::string &k) { std::vector<int> v; if (has(k)) v = getvi(k); if (!v.empty() && v[0] < 0) v.clear(); return v; };

        m_clk_period = geti("m_clk_period");
        m_component_ids = getvi("component_ids");
        NetConfig &c = m_cfg;
        c.topology = gets("topology");
        c.local_ports = geti("local_ports");
        c.flit_bytes = geti("flit_bytes"); c.header_bytes = geti("header_bytes");
        c.num_classes = geti("num_classes"); c.vcs_per_class = geti("vcs_per_class"); c.vc_depth = geti("vc_depth");
        c.router_latency = geti("router_latency"); c.link_latency = geti("link_latency");
        c.credit_latency = geti("credit_latency"); if (c.credit_latency < 0) c.credit_latency = c.link_latency;
        c.class_link_latency = optvi("class_link_latency");
        c.half_duplex = geti("half_duplex") != 0;
        c.link_model = gets("link_model");
        c.inject_depth = geti("inject_queue_depth"); c.eject_depth = geti("eject_queue_depth");
        c.rx_serving = gets("rx_serving"); c.routing = gets("routing"); c.escape_vc = geti("escape_vc"); c.arbiter_type = gets("arbiter_type");
        if (has("deadlock_check")) c.deadlock_check = gets("deadlock_check");
        m_home_ids = optvi("home_ids");

        auto bad = [](const std::string &m) { throw ConfigError(m); };
        if (c.flit_bytes < 0) bad("flit_bytes must be >= 0");
        if (c.header_bytes < 0) bad("header_bytes must be >= 0");
        if (c.num_classes != 2 && c.num_classes != 3) bad("num_classes must be 2 or 3");
        if (c.vcs_per_class < 1) bad("vcs_per_class must be >= 1");
        if (c.vc_depth < 1) bad("vc_depth must be >= 1");
        if (c.router_latency < 0) bad("router_latency must be >= 0");
        if (c.link_latency < 1) bad("link_latency must be >= 1");
        if (c.credit_latency < 0) bad("credit_latency must be >= 0");
        if (!c.class_link_latency.empty())
        {
            if ((int)c.class_link_latency.size() != c.num_classes) bad("class_link_latency needs one entry per class");
            for (int l : c.class_link_latency) if (l < 1) bad("class_link_latency entries must be >= 1");
        }
        if (geti("half_duplex") != 0 && geti("half_duplex") != 1) bad("half_duplex must be 0 or 1");
        if (c.link_model != "pipelined" && c.link_model != "serial") bad("link_model must be pipelined or serial, got " + c.link_model);
        if (c.deadlock_check != "refuse" && c.deadlock_check != "warn") bad("deadlock_check must be refuse or warn, got " + c.deadlock_check);
        if (c.inject_depth < 1 || c.eject_depth < 1) bad("inject_queue_depth and eject_queue_depth must be >= 1");
        if (c.escape_vc >= c.vcs_per_class) bad("escape_vc must be < vcs_per_class");
        if (c.arbiter_type == "TDMArbiter" || c.arbiter_type == "PriorityArbiter")
            bad("arbiter_type " + c.arbiter_type + " is deferred for the network (docs/NoC_Architecture_Spec.md s12)");
        if (c.arbiter_type != "FCFSArbiter" && c.arbiter_type != "RRArbiter") bad("arbiter_type must be FCFSArbiter or RRArbiter, got " + c.arbiter_type);
        for (auto &kv : parameters)
            if (kv.first.rfind("slot_table[", 0) == 0 || kv.first.rfind("vc_priority[", 0) == 0)
                bad("key " + kv.first + " belongs to the deferred real-time phase (docs/NoC_Architecture_Spec.md s12)");
        if (c.routing == "auto") c.routing = (c.topology == "grid2d" || c.topology == "torus2d") ? "XY" : "Table";
        m_class_stats.assign(c.num_classes, ClassStats{});
    }

    void Network::buildTopology()
    {
        auto has  = [&](const std::string &k) { return parameters.count(k) > 0; };
        auto geti = [&](const std::string &k) { return std::get<int>(parameters.at(k).value); };
        auto getvi = [&](const std::string &k) { return std::get<std::vector<int>>(parameters.at(k).value); };
        const std::string &t = m_cfg.topology;
        if (t == "custom")
        {
            std::vector<std::vector<int>> rows;
            for (int n = 0; has("connection_matrix[" + std::to_string(n) + "]"); n++) rows.push_back(getvi("connection_matrix[" + std::to_string(n) + "]"));
            if (rows.empty()) throw ConfigError("topology custom needs connection_matrix[i] rows");
            std::vector<int> rids; if (has("router_ids")) { rids = getvi("router_ids"); if (!rids.empty() && rids[0] < 0) rids.clear(); }
            m_topo = std::make_unique<Topology>(Topology::custom(rows, m_component_ids, rids, m_cfg.local_ports));
        }
        else if (t == "grid2d" || t == "torus2d" || t == "ring")
        {
            std::vector<int> dims = getvi("dims");
            std::map<int, int> node_map;
            for (int id : m_component_ids)
            {
                std::string k = "node_map[" + std::to_string(id) + "]";
                if (!has(k)) throw ConfigError("endpoint " + std::to_string(id) + " has no " + k + " (which router it attaches to)");
                node_map[id] = geti(k);
            }
            if (t == "ring") m_topo = std::make_unique<Topology>(Topology::ring(dims.empty() ? 0 : dims[0], node_map, m_cfg.local_ports));
            else
            {
                if (dims.size() < 2) throw ConfigError("dims needs kx,ky for " + t);
                m_topo = std::make_unique<Topology>(Topology::grid2d(dims[0], dims[1], node_map, m_cfg.local_ports, t == "torus2d"));
            }
        }
        else if (t == "star") m_topo = std::make_unique<Topology>(Topology::star(m_component_ids, m_cfg.local_ports));
        else throw ConfigError("unknown topology '" + t + "' (custom, grid2d, torus2d, ring, star)");

        RoutingParams rp; rp.vcs_per_class = m_cfg.vcs_per_class; rp.escape_vc = m_cfg.escape_vc;
        for (auto &kv : parameters)
            if (kv.first.rfind("routing_table[", 0) == 0)
            {
                int rid = std::atoi(kv.first.c_str() + 14);
                rp.routing_table[rid] = std::get<std::vector<int>>(kv.second.value);
            }
        m_routing = createRouting(m_cfg.routing, *m_topo, rp);
        if (!m_routing->deadlockFreeOn(*m_topo) && m_debug != "nocheck")
        {
            std::string cyc; channelDependencyAcyclic(*m_topo, *m_routing, m_cfg.vcs_per_class, &cyc);
            throw ConfigError("routing " + m_cfg.routing + " is not deadlock-free on this topology: " + cyc +
                              (m_cfg.escape_vc < 0 ? " (set escape_vc or use a deadlock-free algorithm)" : ""));
        }
        std::set<int> homes(m_home_ids.begin(), m_home_ids.end());
        m_classifier = std::make_unique<Classifier>(m_cfg.num_classes, homes);
    }

    std::unique_ptr<Arbiter> Network::makeArbiter()
    {
        if (m_cfg.arbiter_type == "RRArbiter") return std::make_unique<RRArbiter>(&m_owner_ids, 1);
        return std::make_unique<FCFSArbiter>(&m_owner_ids, 1);
    }

    void Network::buildRouters()
    {
        const Topology &t = *m_topo;
        m_owner_ids = t.endpoints(); std::sort(m_owner_ids.begin(), m_owner_ids.end());
        m_lower_ids = m_component_ids;
        m_routers.resize(t.routers());
        for (int r = 0; r < t.routers(); r++)
        {
            RouterState &R = m_routers[r];
            int P = t.ports(r);
            R.in.assign(P, std::vector<std::vector<VCState>>(m_cfg.num_classes, std::vector<VCState>(m_cfg.vcs_per_class)));
            R.in_last_sent.assign(P, 0);
            R.out.resize(P);
            for (int p = 0; p < P; p++)
            {
                R.out[p].vcs.resize(m_cfg.num_classes);
                for (int c = 0; c < m_cfg.num_classes; c++)
                {
                    R.out[p].vcs[c].resize(m_cfg.vcs_per_class);
                    for (int v = 0; v < m_cfg.vcs_per_class; v++)
                    {
                        R.out[p].vcs[c][v].credits = t.isLocalPort(r, p) ? 0 : m_cfg.vc_depth;
                        R.out[p].vcs[c][v].va = makeArbiter();
                    }
                }
                R.out[p].sa = makeArbiter();
                if (t.isLocalPort(r, p))
                {
                    LocalPort &lp = m_local[{r, p}];
                    lp.router = r; lp.port = p; lp.arb = makeArbiter();
                    lp.vcs.resize(m_cfg.num_classes);
                    for (int c = 0; c < m_cfg.num_classes; c++) { lp.vcs[c].resize(m_cfg.vcs_per_class); for (auto &o : lp.vcs[c]) o.credits = m_cfg.vc_depth; }
                }
            }
        }
        // switch-allocation units (spec s11.3): one per output port; under half_duplex the two
        // directions of a connection are one resource with one arbiter over both routers' candidates
        m_sa_units.clear();
        for (int r = 0; r < t.routers(); r++) for (int p = 0; p < t.ports(r); p++)
        {
            if (!t.isLocalPort(r, p) && m_cfg.half_duplex)
            {
                auto far = t.link(r, p);
                if (far.router < r || (far.router == r && far.port < p)) continue;      // the other side owns the unit
                m_sa_units.push_back({{r, p}, {far.router, far.port}});
            }
            else m_sa_units.push_back({{r, p}});
        }
        for (int id : t.endpoints())
        {
            NetworkInterface *ni = new NetworkInterface(this, id, m_cfg.num_classes, m_cfg.inject_depth, m_cfg.eject_depth, m_cfg.rx_serving);
            m_nis[id] = ni;
            m_local.at({t.routerOf(id), t.localPort(id)}).nis.push_back(ni);
        }
    }

    void Network::init() {}
    CommunicationInterface *Network::getInterfaceFor(int id)
    {
        auto it = m_nis.find(id);
        if (it == m_nis.end()) fatal("no endpoint " + std::to_string(id) + " in the network (component_ids)");
        return it->second;
    }
    std::vector<int> *Network::getLowerLevelIds() { return &m_lower_ids; }

    void Network::requireOrdering(const std::string &req)
    {
        try { checkOrderingRequirement(req, *m_routing, *m_topo, m_cfg.vcs_per_class); }
        catch (ConfigError &e) { fatal(e.what()); }
        m_ordering_req = req;
    }
    void Network::standalone(const std::vector<TrafficGenerator *> &gens)
    {
        m_gens = gens; m_standalone = true;
        ClockManager::getClockManager()->registerCLKTrigger(this);
    }

    // ------------------------------------------------------------------ packets
    uint64_t Network::newPacket(const Message &msg, int src_endpoint, int dst_endpoint, int cls)
    {
        uint64_t seq = ++m_seq;
        Packet &p = m_packets[seq];
        p.msg = msg;                                   // delivered bit-identical, `to` included (spec §3.2)
        p.seq = seq; p.cls = cls; p.dst_endpoint = dst_endpoint;
        p.src_router = m_topo->routerOf(src_endpoint);          // the injecting NI, not msg.from (a protocol may carry another id there)
        p.dst_router = m_topo->routerOf(dst_endpoint);
        if (p.src_router < 0)
            fatal("message injected by " + std::to_string(src_endpoint) + " which is not an endpoint: " + Classifier::describe(msg, dst_endpoint));
        p.flits = flitCount(msg, m_cfg.flit_bytes, m_cfg.header_bytes);
        return seq;
    }

    // ------------------------------------------------------------------ clock
    void Network::cycleProcess()
    {
        if (m_cycle_edges % 2 == 1) step();              // falling edge, as Mesh::cycleProcess
        m_cycle_edges++;
    }
    void Network::step()
    {
        m_tick++;
        if (m_hang_ticks > 0 && !m_packets.empty() && m_tick - m_last_move >= (uint64_t)m_hang_ticks)
        {
            Logger::getLogger()->traceFinalize();
            fatal("no flit moved for " + std::to_string(m_hang_ticks) + " ticks with " + std::to_string(m_packets.size()) + " packets in flight: the network is wedged");
        }
        phaseInject();
        phaseArrivals();
        phaseRoute();
        phaseVCAlloc();
        phaseSwitch();
        if (m_standalone)
        {
            bool all_done = true;
            for (auto g : m_gens) if (!g->done()) { all_done = false; break; }
            if (all_done && m_packets.empty())
            {
                for (auto &kv : m_nis) { Message m; if (kv.second->peekMessage(&m)) { all_done = false; break; } }
                if (all_done) ClockManager::getClockManager()->stopClock(this);
            }
        }
    }

    static inline uint32_t allVCs(int n) { return n >= 32 ? ~0u : ((1u << n) - 1); }
    uint64_t Network::linkKey(int router, int port) const
    {
        if (m_cfg.half_duplex && !m_topo->isLocalPort(router, port))
        {
            auto f = m_topo->link(router, port);
            if (f.router < router || (f.router == router && f.port < port)) return (uint64_t)f.router * 1024 + f.port;
        }
        return (uint64_t)router * 1024 + port;
    }

    // ---- phase 1: injection (spec §7.2)
    void Network::phaseInject()
    {
        const Topology &t = *m_topo;
        for (auto &kv : m_local)
        {
            LocalPort &lp = kv.second;
            if (lp.last_sent == m_tick) continue;
            struct Act { NetworkInterface *ni; int cls, vc; uint64_t seq; bool fresh; };
            std::vector<Act> acts; std::vector<Candidate> cands;
            // every class competes; the port goes to the oldest candidate (injection order), so an
            // endpoint never reorders its own messages across classes (spec s7.2)
            for (int cls = 0; cls < m_cfg.num_classes; cls++)
            {
                for (NetworkInterface *ni : lp.nis)
                {
                    // started packets of this NI on this class
                    for (int v = 0; v < m_cfg.vcs_per_class; v++)
                    {
                        OutVC &o = lp.vcs[cls][v];
                        if (o.holder == 0) continue;
                        Packet &p = m_packets.at(o.holder);
                        if (o.holder_ep != ni->id()) continue;        // not by msg.from: a protocol may forward the requester's id
                        if (p.next_flit < p.flits && o.credits > 0)
                        { cands.push_back({(int)acts.size(), (int)p.msg.owner, p.seq}); acts.push_back({ni, cls, v, p.seq, false}); }
                    }
                    // a new packet: the front of the class queue, on the lowest free VC its first-hop mask admits
                    if (!ni->injectQueue(cls).empty())
                    {
                        uint64_t seq = ni->injectQueue(cls).front(); Packet &p = m_packets.at(seq);
                        RouteContext ctx; ctx.router = lp.router; ctx.in_port = lp.port; ctx.in_vc = 0; ctx.dst_router = p.dst_router;
                        ctx.cls = cls; ctx.src_router = p.src_router; ctx.packet = &p; View view(this, lp.router); ctx.here = &view;
                        uint32_t mask = m_routing->nextHop(ctx).vc_mask & allVCs(m_cfg.vcs_per_class);
                        if (mask == 0) fatal("routing " + m_cfg.routing + " returned an empty vc_mask at injection for packet " + std::to_string(seq));
                        for (int v = 0; v < m_cfg.vcs_per_class; v++)
                        {
                            OutVC &o = lp.vcs[cls][v];
                            if ((mask & (1u << v)) && o.holder == 0 && o.credits > 0)
                            { cands.push_back({(int)acts.size(), (int)p.msg.owner, p.seq}); acts.push_back({ni, cls, v, seq, true}); break; }
                        }
                    }
                }
            }
            if (cands.empty()) continue;
            int w = cands.size() == 1 ? 0 : lp.arb->electIndex(m_tick, cands);
            if (w < 0) continue;
            Act &a = acts[w]; Packet &p = m_packets.at(a.seq); OutVC &o = lp.vcs[a.cls][a.vc];
            if (a.fresh)
            {
                a.ni->injectQueue(a.cls).pop_front();
                o.holder = a.seq; o.holder_ep = a.ni->id(); p.t_inject = m_tick; p.next_flit = 0;
                m_totals.injected_packets++;
                if (m_class_stats[a.cls].first_inject == 0) m_class_stats[a.cls].first_inject = m_tick;
                Logger::getLogger()->trace(p.msg, busRole(p.msg), busComp(p.msg), Logger::Phase::ENTER, (uint16_t)p.dst_endpoint);   // flags = destination: tells the copies of a multicast apart
            }
            Flit f; f.seq = a.seq; f.index = p.next_flit; f.head = p.next_flit == 0; f.tail = p.next_flit == p.flits - 1; f.arrived = m_tick;
            m_routers[lp.router].in[lp.port][a.cls][a.vc].fifo.push_back(f);     // local link: zero latency
            if (f.head) Logger::getLogger()->trace(p.msg, Logger::Role::NOC, (uint32_t)m_topo->routerId(lp.router), Logger::Phase::ENTER, (uint16_t)((lp.port << 8) | a.vc));
            o.credits--; p.next_flit++; m_totals.injected_flits++; lp.last_sent = m_tick; m_last_move = m_tick;
            if (f.tail) { o.holder = 0; o.holder_ep = -1; }
            (void)t;
        }
    }

    // ---- phase 2: arrivals and credit returns
    void Network::phaseArrivals()
    {
        while (!m_arrivals.empty() && m_arrivals.begin()->first <= m_tick)
        {
            Arrival a = m_arrivals.begin()->second; m_arrivals.erase(m_arrivals.begin());
            VCState &v = m_routers[a.router].in[a.port][a.cls][a.vc];
            if ((int)v.fifo.size() >= m_cfg.vc_depth)
                fatal("VC overflow at router " + std::to_string(m_topo->routerId(a.router)) + " port " + std::to_string(a.port) + " class " + std::to_string(a.cls) + " vc " + std::to_string(a.vc));
            v.fifo.push_back(a.flit);
            if (a.flit.head) Logger::getLogger()->trace(m_packets.at(a.flit.seq).msg, Logger::Role::NOC, (uint32_t)m_topo->routerId(a.router), Logger::Phase::ENTER, (uint16_t)((a.port << 8) | a.vc));
        }
        if (m_debug == "credit" && m_tick == 50) m_credits.emplace(m_tick, CreditEvt{false, 0, 0, 0, 0});   // one credit too many
        while (!m_credits.empty() && m_credits.begin()->first <= m_tick)
        {
            CreditEvt c = m_credits.begin()->second; m_credits.erase(m_credits.begin());
            OutVC &o = c.local ? m_local.at({c.router, c.port}).vcs[c.cls][c.vc] : m_routers[c.router].out[c.port].vcs[c.cls][c.vc];
            o.credits++;
            if (o.credits > m_cfg.vc_depth)
                fatal("credit counter above vc_depth at " + std::string(c.local ? "local port " : "router ") + std::to_string(m_topo->routerId(c.router)) + " port " + std::to_string(c.port) + " class " + std::to_string(c.cls) + " vc " + std::to_string(c.vc));
        }
    }

    // ---- phase 3: route computation (spec §8.2)
    void Network::phaseRoute()
    {
        const Topology &t = *m_topo;
        for (int r = 0; r < t.routers(); r++)
        {
            RouterState &R = m_routers[r]; View view(this, r);
            for (int q = 0; q < t.ports(r); q++) for (int c = 0; c < m_cfg.num_classes; c++) for (int w = 0; w < m_cfg.vcs_per_class; w++)
            {
                VCState &v = R.in[q][c][w];
                if (v.routed || v.fifo.empty() || !v.fifo.front().head) continue;
                Packet &p = m_packets.at(v.fifo.front().seq);
                RouteContext ctx; ctx.router = r; ctx.in_port = q; ctx.in_vc = w; ctx.dst_router = p.dst_router; ctx.cls = c; ctx.src_router = p.src_router; ctx.packet = &p; ctx.here = &view;
                Route rt = m_routing->nextHop(ctx);
                if (rt.out_port < 0 || rt.out_port >= t.ports(r))
                    fatal("routing " + m_cfg.routing + " returned port " + std::to_string(rt.out_port) + " at router " + std::to_string(t.routerId(r)) + " for packet " + std::to_string(p.seq) + " (msg " + std::to_string(p.msg.msg_id) + ")");
                rt.vc_mask &= allVCs(m_cfg.vcs_per_class);
                if (m_debug == "mask" && m_tick >= 50) rt.vc_mask = 0;
                if (rt.vc_mask == 0) fatal("routing " + m_cfg.routing + " returned an empty vc_mask at router " + std::to_string(t.routerId(r)) + " for packet " + std::to_string(p.seq));
                v.cur = p.seq; v.routed = true; v.out_port = rt.out_port; v.out_vc = -1; v.vc_mask = rt.vc_mask; v.head_arrival = v.fifo.front().arrived;
                p.hops++; p.pipe += t.implicitRouter(r) ? 0 : m_cfg.router_latency;
            }
        }
    }

    // ---- phase 4: VC allocation (spec §11.2)
    void Network::phaseVCAlloc()
    {
        const Topology &t = *m_topo;
        for (int r = 0; r < t.routers(); r++)
        {
            RouterState &R = m_routers[r];
            for (int p = 0; p < t.ports(r); p++)
            {
                bool local = t.isLocalPort(r, p);
                for (int c = 0; c < m_cfg.num_classes; c++) for (int ov = 0; ov < m_cfg.vcs_per_class; ov++)
                {
                    OutVC &o = R.out[p].vcs[c][ov];
                    if (o.holder != 0) continue;
                    std::vector<Candidate> cands; std::vector<VCState *> who;
                    for (int q = 0; q < t.ports(r); q++) for (int w = 0; w < m_cfg.vcs_per_class; w++)
                    {
                        VCState &v = R.in[q][c][w];
                        if (!v.routed || v.out_port != p || v.out_vc >= 0 || !(v.vc_mask & (1u << ov))) continue;
                        if (m_tick < v.head_arrival + (uint64_t)(t.implicitRouter(r) ? 0 : m_cfg.router_latency)) continue;   // implicit routers add no latency
                        Packet &pk = m_packets.at(v.cur);
                        if (local && !m_nis.at(pk.dst_endpoint)->canReserve(c)) continue;
                        cands.push_back({(int)who.size(), (int)pk.msg.owner, pk.seq}); who.push_back(&v);
                    }
                    if (cands.empty()) continue;
                    int w = cands.size() == 1 ? 0 : o.va->electIndex(m_tick, cands);
                    if (w < 0) continue;
                    VCState &v = *who[w]; v.out_vc = ov; o.holder = v.cur;
                    Logger::getLogger()->trace(m_packets.at(v.cur).msg, Logger::Role::NOC, (uint32_t)t.routerId(r), Logger::Phase::SERVICE, (uint16_t)((p << 8) | ov));
                    if (local) m_nis.at(m_packets.at(v.cur).dst_endpoint)->reserve(c);
                }
            }
        }
    }

    // ---- phase 5: switch allocation and link traversal (spec s11.3), ejection on the tail.
    // One arbitration per unit (an output port, or both directions of a half-duplex connection).
    void Network::phaseSwitch()
    {
        const Topology &t = *m_topo;
        for (auto &unit : m_sa_units)
        {
            int r0 = unit[0].first, p0 = unit[0].second;
            uint64_t key = linkKey(r0, p0);
            auto bz = m_link_busy.find(key);
            if (bz != m_link_busy.end() && bz->second >= m_tick) continue;      // link busy
            std::vector<Candidate> cands; std::vector<std::tuple<int, int, int, int>> who;
            for (auto &rp : unit)
            {
                int r = rp.first, p = rp.second; RouterState &R = m_routers[r];
                bool local = t.isLocalPort(r, p);
                int before = (int)cands.size();
                for (int q = 0; q < t.ports(r); q++)
                {
                    if (R.in_last_sent[q] == m_tick) continue;                        // one flit per input port per tick
                    for (int c = 0; c < m_cfg.num_classes; c++) for (int w = 0; w < m_cfg.vcs_per_class; w++)
                    {
                        VCState &v = R.in[q][c][w];
                        if (!v.routed || v.out_port != p || v.out_vc < 0 || v.fifo.empty() || v.fifo.front().seq != v.cur) continue;
                        if (!local && R.out[p].vcs[c][v.out_vc].credits <= 0) continue;
                        Packet &pk = m_packets.at(v.cur);
                        cands.push_back({(int)who.size(), (int)pk.msg.owner, pk.seq}); who.push_back({r, q, c, w});
                    }
                }
                R.out[p].attempts += cands.size() - before;
            }
            if (cands.empty()) continue;
            int w = cands.size() == 1 ? 0 : m_routers[r0].out[p0].sa->electIndex(m_tick, cands);
            if (w < 0) continue;
            for (int i = 0; i < (int)cands.size(); i++) if (i != w)
            {
                { Packet &lp = m_packets.at(cands[i].seq); lp.ahead++; lp.ahead_by[cands[w].owner]++; }
                auto [lr, lq, lc, lw] = who[i];
                m_routers[lr].out[m_routers[lr].in[lq][lc][lw].out_port].losses++;   // counted at the loser's port
            }
            auto [r, q, c, iv] = who[w];
            sendFlit(r, q, c, iv, m_routers[r].in[q][c][iv]);
        }
    }

    void Network::sendFlit(int r, int q, int c, int w, VCState &v)
    {
        const Topology &t = *m_topo;
        RouterState &R = m_routers[r];
        if (R.in_last_sent[q] == m_tick) fatal("input port " + std::to_string(q) + " of router " + std::to_string(t.routerId(r)) + " sent two flits in one tick");
        Flit f = v.fifo.front(); v.fifo.pop_front();
        R.in_last_sent[q] = m_tick; m_last_move = m_tick;
        int p = v.out_port; OutPort &op = R.out[p]; OutVC &ov = op.vcs[c][v.out_vc];
        bool local = t.isLocalPort(r, p);
        int L = local ? 1 : m_cfg.linkLatency(c);
        if (f.tail) Logger::getLogger()->trace(m_packets.at(f.seq).msg, Logger::Role::NOC, (uint32_t)t.routerId(r), Logger::Phase::EXIT, (uint16_t)((p << 8) | (m_packets.at(f.seq).flits & 0xff)));
        int occ = (local || m_cfg.link_model == "pipelined") ? 1 : L;          // ticks the link is held per flit
        m_link_busy[linkKey(r, p)] = m_tick + occ - 1;
        op.flits++; op.busy_ticks += occ;
        // credit back to whoever feeds input port q
        CreditEvt ce;
        if (t.isLocalPort(r, q)) ce = {true, r, q, c, w};
        else { auto up = t.link(r, q); ce = {false, up.router, up.port, c, w}; }
        if (m_cfg.credit_latency == 0)
        {
            OutVC &o = ce.local ? m_local.at({ce.router, ce.port}).vcs[c][w] : m_routers[ce.router].out[ce.port].vcs[c][w];
            if (++o.credits > m_cfg.vc_depth) fatal("credit counter above vc_depth (immediate return)");
        }
        else m_credits.emplace(m_tick + m_cfg.credit_latency, ce);
        if (!local)
        {
            ov.credits--;
            auto far = t.link(r, p);
            // serial (legacy) link: the transfer counts the sending tick, so the flit is at the far
            // side L-1 ticks later while the link is held for L ticks (spec s4.2)
            int arrive = m_cfg.link_model == "serial" ? std::max(1, L - 1) : L;
            f.arrived = m_tick + arrive;
            if (f.head) m_packets.at(f.seq).transit += arrive;
            m_arrivals.emplace(m_tick + arrive, Arrival{far.router, far.port, c, v.out_vc, f});
        }
        if (f.tail)
        {
            ov.holder = 0;
            uint64_t seq = v.cur;
            v.cur = 0; v.routed = false; v.out_port = -1; v.out_vc = -1; v.vc_mask = 0;
            if (local) eject(seq, r, p);
        }
    }

    void Network::eject(uint64_t seq, int r, int p)
    {
        Packet &pk = m_packets.at(seq);
        NetworkInterface *ni = m_nis.at(pk.dst_endpoint);
        uint64_t total = m_tick - pk.t_inject;
        int links = pk.hops - 1;
        uint64_t transfer = pk.pipe + pk.transit + (pk.flits - 1);        // what this packet would have taken unloaded
        pk.transfer_ticks = transfer; pk.wait_ticks = total >= transfer ? total - transfer : 0;
        ClassStats &cs = m_class_stats[pk.cls];
        cs.packets++; cs.sum_latency += total; cs.max_latency = std::max(cs.max_latency, total); cs.max_wait = std::max(cs.max_wait, pk.wait_ticks); cs.last_eject = m_tick;
        EndpointStats &es = m_ep_stats[pk.dst_endpoint];
        if (es.packets == 0) { es.first_owner = pk.msg.owner; es.first_seq = pk.seq; }
        es.packets++; es.sum_latency += total; es.max_latency = std::max(es.max_latency, total);
        int winner = -1; uint32_t wins = 0;
        for (auto &kv : pk.ahead_by) if (kv.second > wins) { wins = kv.second; winner = kv.first; }
        if (pk.ahead > es.max_ahead) { es.max_ahead = pk.ahead; es.max_ahead_winner = winner; }
        // trackers (spec s15.3): accumulated over the request's packets; untracked ids are ignored
        Logger *lg = Logger::getLogger();
        Logger::Role role = busRole(pk.msg);
        if (role != Logger::Role::MEM_BUS)      // the NoC trackers count coherence packets only (spec s25.5)
        {
            lg->accumulate(pk.msg.msg_id, Logger::Annot::NOC_HOPS, pk.hops);
            lg->accumulate(pk.msg.msg_id, Logger::Annot::NOC_WAIT, (int64_t)pk.wait_ticks);
            lg->accumulate(pk.msg.msg_id, Logger::Annot::NOC_TRANSFER, (int64_t)pk.transfer_ticks);
            lg->accumulate(pk.msg.msg_id, Logger::Annot::NOC_AHEAD, (int64_t)pk.ahead);
            lg->accumulateWinner(pk.msg.msg_id, winner, wins);
        }
        // the report's response-bus stamp is the copy that reaches the requester (a supply also goes
        // to the home; on a mesh the two cross different links and the home's copy may eject first);
        // memory packets stamp MEM_BUS both ways, as the point-to-point memory bus does
        if (role != Logger::Role::RESP_BUS || pk.dst_endpoint == (int)pk.msg.owner)
            Logger::getLogger()->event(pk.msg.msg_id, role, busComp(pk.msg), Logger::Phase::EXIT);
        Logger::getLogger()->trace(pk.msg, role, busComp(pk.msg), Logger::Phase::EXIT, (uint16_t)pk.dst_endpoint);
        ni->deliver(pk.cls, pk.msg);
        if (m_debug == "rx") ni->deliver(pk.cls, pk.msg);        // a second delivery without a reservation: must be fatal
        m_totals.ejected_packets++; m_totals.ejected_flits += pk.flits; m_totals.link_flit_hops += (uint64_t)links * pk.flits;
        m_packets.erase(seq);
        (void)r; (void)p;
    }

    // ------------------------------------------------------------------ observability
    // memory packets (spec s25.5) log as the memory bus does, comp 1; coherence packets as the L1-LLC bus
    Logger::Role Network::busRole(const Message &m)
    {
        switch (m.kind)
        {
            case Message::K_MEM_READ: case Message::K_MEM_WRITE: case Message::K_FILL: case Message::K_MEM_WACK:
                return Logger::Role::MEM_BUS;
            default:
                return m.data == NULL ? Logger::Role::REQ_BUS : Logger::Role::RESP_BUS;
        }
    }
    uint32_t Network::busComp(const Message &m) { return busRole(m) == Logger::Role::MEM_BUS ? 1u : 0u; }

    std::string Network::flitName(const Flit &f) const
    {
        auto it = m_packets.find(f.seq);
        std::ostringstream o; o << "seq " << f.seq;
        if (it != m_packets.end()) o << " msg " << it->second.msg.msg_id << " cls " << it->second.cls << " owner " << it->second.msg.owner << " -> " << it->second.dst_endpoint;
        return o.str();
    }
    std::string Network::dump() const
    {
        const Topology &t = *m_topo;
        std::ostringstream o;
        o << "Network tick " << m_tick << ": " << m_packets.size() << " packets in flight, " << m_arrivals.size() << " flits on links, " << m_credits.size() << " credits in return\n";
        for (int r = 0; r < t.routers(); r++)
        {
            const RouterState &R = m_routers[r];
            for (int q = 0; q < t.ports(r); q++) for (int c = 0; c < m_cfg.num_classes; c++) for (int w = 0; w < m_cfg.vcs_per_class; w++)
            {
                const VCState &v = R.in[q][c][w];
                if (v.fifo.empty() && !v.routed) continue;
                o << "  router " << t.routerId(r) << " in " << t.portName(r, q) << " cls " << c << " vc " << w << ": " << v.fifo.size() << " flits";
                if (v.routed)
                {
                    o << ", " << flitName(v.fifo.empty() ? Flit{v.cur} : v.fifo.front()) << " -> out " << t.portName(r, v.out_port);
                    if (v.out_vc >= 0) o << " vc " << v.out_vc << " credits " << R.out[v.out_port].vcs[c][v.out_vc].credits;
                    else o << " (waiting for a VC, mask 0x" << std::hex << v.vc_mask << std::dec << ")";
                }
                o << "\n";
            }
        }
        for (auto &kv : m_nis)
        {
            const NetworkInterface *ni = kv.second;
            o << "  endpoint " << kv.first << ":";
            for (int c = 0; c < m_cfg.num_classes; c++) o << " cls" << c << "[inject " << ni->injectQueue(c).size() << " rx " << ni->rxSize(c) << " reserved " << ni->reserved(c) << "]";
            o << "\n";
        }
        return o.str();
    }

    void Network::writeStats(const std::string &path) const
    {
        const Topology &t = *m_topo;
        std::ofstream f(path);
        if (!f) return;
        f << "section,key1,key2,key3,value1,value2,value3,value4\n";
        for (int r = 0; r < t.routers(); r++) for (int p = 0; p < t.ports(r); p++)
            f << "port," << t.routerId(r) << "," << p << "," << t.portName(r, p) << "," << m_routers[r].out[p].flits << "," << m_routers[r].out[p].busy_ticks << "," << m_routers[r].out[p].attempts << "," << m_routers[r].out[p].losses << "\n";
        for (int c = 0; c < m_cfg.num_classes; c++)
        {
            const ClassStats &s = m_class_stats[c];
            f << "class," << c << ",,," << s.packets << "," << (s.packets ? (double)s.sum_latency / s.packets : 0.0) << "," << s.max_latency << "," << s.max_wait << "\n";
            f << "classtime," << c << ",,," << s.first_inject << "," << s.last_eject << ",,\n";
        }
        for (auto &kv : m_nis)
        {
            auto es = m_ep_stats.count(kv.first) ? m_ep_stats.at(kv.first) : EndpointStats{};
            f << "endpoint," << kv.first << ",,," << kv.second->rxDelivered() << "," << kv.second->rxPopped() << "," << kv.second->pushRefused() << "," << es.packets << "\n";
            f << "received," << kv.first << ",,," << es.packets << "," << (es.packets ? (double)es.sum_latency / es.packets : 0.0) << "," << es.max_latency << "," << es.first_owner << "\n";
            f << "ahead," << kv.first << ",,," << es.max_ahead << "," << es.max_ahead_winner << ",,\n";
        }
        for (auto g : m_gens)
            f << "generator," << g->id() << ",,," << g->sent() << "," << g->received() << "," << g->retries() << "," << g->outOfOrder() << "\n";
        f << "totals,flits,,," << m_totals.injected_flits << "," << m_totals.ejected_flits << "," << (m_totals.injected_flits - m_totals.ejected_flits) << ",\n";
        f << "totals,packets,,," << m_totals.injected_packets << "," << m_totals.ejected_packets << "," << m_packets.size() << ",\n";
        f << "totals,linkflits,,," << m_totals.link_flit_hops << ",,,\n";
        f << "totals,ticks,,," << m_tick << ",,,\n";
        f << "config,," << m_cfg.topology << "," << m_cfg.routing << "," << m_cfg.flit_bytes << "," << m_cfg.vc_depth << "," << m_cfg.router_latency << "," << m_cfg.link_latency << "\n";
    }
    void Network::statsAtExit() { if (s_instance) s_instance->flushStats(); }
    void Network::flushStats()
    {
        if (m_stats_written || m_tick == 0) return;        // never ticked (unit tests): nothing to report
        m_stats_written = true;
        std::string dir = Logger::getLogger()->reportPath();
        if (dir.empty()) dir = ".";
        writeStats(dir + "/NetworkStats.csv");
    }
}}
