/*
 * File  :      TLNetPort.cpp
 * TileLink over the Network engine: the TileLink boundary at each network interface. Specification:
 * docs/TileLink_Plan.md s5.2 (the port contract), s5.3 I-C (message-level, 5 and 7 classes); the
 * design (which crossbar-port function lives where, what the message-level links check) is in
 * TLNetPort.h.
 */
#include "../../../header/Interconnect/TileLink/TLNetPort.h"
#include "../../../header/Interconnect/TileLink/TileLinkInterconnect.h"
#include "../../../header/Interconnect/Network/Network.h"
#include "../../../header/Interconnect/Network/NetworkInterface.h"
#include "../../../header/HangDump.h"
#include "../../../header/Logger.h"
#include "tilelink/agents/Xbar.h"
#include "tilelink/CoherenceCheck.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace octopus
{
    using tl::Channel;

    static std::string describe(const Message &m)
    {
        std::ostringstream o;
        o << "msg " << m.msg_id << " addr 0x" << std::hex << m.addr << std::dec << " kind " << (int)m.kind
          << " ch " << TLPort::channelName(m.tl.channel) << " opcode " << (int)m.tl.opcode;
        if (m.tl.channel >= 1 && m.tl.channel <= 5) o << " (" << tl::opcodeName(Channel(m.tl.channel - 1), m.tl.opcode) << ")";
        o << " param " << (int)m.tl.param << " size " << (int)m.tl.size << " source " << m.tl.source << " sink " << m.tl.sink
          << " from " << m.from << " to";
        for (int d : m.to) o << " " << d;
        return o.str();
    }
    static bool isRelease(Channel ch, unsigned op) { return ch == Channel::C && (op == (unsigned)tl::OpC::Release || op == (unsigned)tl::OpC::ReleaseData); }
    static bool isGrant(Channel ch, unsigned op) { return ch == Channel::D && (op == (unsigned)tl::OpD::Grant || op == (unsigned)tl::OpD::GrantData); }

    // ------------------------------------------------------------------ TLNetPort
    TLNetPort::TLNetPort(TLNetBoundary *b, net::NetworkInterface *ni, int id, const std::string &name, tl::Role role, bool memory_side)
        : TLPort(nullptr, id, name, role, Depths(), memory_side), m_b(b), m_ni(ni)
    {
    }

    bool TLNetPort::peekMessage(Message *out_msg)
    {
        m_peek_local = !m_local.empty();
        if (m_peek_local) { out_msg->copy(m_local.front()); return true; }
        return m_ni->peekMessage(out_msg);
    }
    void TLNetPort::popFrontMessage()
    {
        if (m_peek_local) { m_local.pop_front(); m_peek_local = false; return; }
        m_ni->popFrontMessage();
    }

    bool TLNetPort::pushMessage2RX(Message &msg, MessageType type)
    {
        if (msg.tl.channel != 0) return m_ni->pushMessage2RX(msg, type);
        // an untagged local fill (BaseController::forwardFromMemWrite, the perfect LLC): the receive path
        // of D, as TLPort's pushMessage2RX puts it in its D queue; TLC_LLC decodes it as AccessAckData
        int oc = channelOf(type, false);
        if (!receives(oc)) fatal(std::string("an untagged pushMessage2RX on channel ") + channelName(oc) + ", which this port does not receive: " + describe(msg));
        if ((int)m_local.size() >= m_b->network().cfg().eject_depth) return false;
        m_local.push_back(msg);
        return true;
    }
    bool TLNetPort::canAcceptRX(MessageType type)
    {
        return receives(channelOf(type, false)) && (int)m_local.size() < m_b->network().cfg().eject_depth;
    }

    int  TLNetPort::txFreeSlotsCh(int oc) { return sends(oc) ? m_ni->txFreeSlotsCh(oc) : 0; }
    void TLNetPort::txReserveCh(int oc, int n) { if (sends(oc)) m_ni->txReserveCh(oc, n); }
    void TLNetPort::txReleaseCh(int oc, int n) { if (sends(oc)) m_ni->txReleaseCh(oc, n); }

    void TLNetPort::accessPerformed(uint64_t addr, bool store)
    {
        if (m_role != tl::Role::Master || !kernel()) return;
        if (tl::CoherenceCheck *c = tl::CoherenceCheck::of(*kernel())) c->accessPerformed(*link(), addr, store, m_b->tick());
    }

    void TLNetPort::present(const Message &m, Channel ch, uint64_t source, uint64_t sink)
    {
        tl::MsgPtr t = std::make_shared<tl::Msg>();
        t->channel = ch; t->opcode = m.tl.opcode; t->param = m.tl.param; t->size = m.tl.size; t->denied = m.tl.denied;
        t->address = m.tl.size >= 64 ? 0 : (m.addr & ~((uint64_t(1) << m.tl.size) - 1));
        t->source = source; t->sink = sink;
        t->layout(link()->params().w, tl::DataMode::None);
        t->id = kernel()->newMsgId();
        t->from_agent = id();
        const uint64_t now = m_b->tick();
        tl::ChannelState &c = link()->chan(ch);
        c.beginCycle(now);
        c.offer(t, 0);
        c.setReady(true);
        c.commit(now);
    }

    // The endpoint's TX: the TileLink checks of TLPort::pushMessage, the identifier this message allocates,
    // the crossbar's namespace on the network, the NI's push (atomic over a B's destinations), then the
    // beat on this port's link.
    bool TLNetPort::pushMessage(Message &msg, uint64_t cycle, MessageType type)
    {
        const int oc = msg.tl.channel;
        if (oc < 1 || oc > 5)
            fatal("pushMessage of a message with no TileLink channel (Message::tl.channel " + std::to_string(oc) +
                  "): a TileLink network interface carries only TileLink-tagged messages; " + describe(msg));
        if (!sends(oc))
            fatal(std::string("a ") + tl::roleName(m_role) + " port cannot send on channel " + channelName(oc) + " of link " + link()->name() +
                  " (" + link()->params().describe() + "): " + describe(msg));
        const Channel ch = Channel(oc - 1);
        const unsigned op = msg.tl.opcode;
        if (!tl::opcodeLegal(ch, op)) fatal("opcode " + std::to_string(op) + " is not defined on channel " + channelName(oc) + ": " + describe(msg));
        if (msg.to.empty()) fatal("a TileLink message needs its destination in Message::to: " + describe(msg));
        if (ch != Channel::B && msg.to.size() != 1) fatal("only a B message has several destinations: " + describe(msg));
        const bool has_data = tl::hasData(ch, op);
        if (msg.data != NULL && !has_data)
            fatal(std::string("the message carries data but ") + tl::opcodeName(ch, op) + " has no data field: " + describe(msg));
        if (has_data && msg.data != NULL && (1u << msg.tl.size) != msg.data_size)
            fatal("data_size " + std::to_string(msg.data_size) + " is not 2^size (size " + std::to_string(msg.tl.size) + "): " + describe(msg));

        // the identifier (plan s5.2): every A a source, a Release[Data] a source of the C pool, a Grant[Data] a sink
        IdPool *pool = ch == Channel::A ? poolFor(1) : isRelease(ch, op) ? poolFor(3) : isGrant(ch, op) ? poolFor(4) : nullptr;
        if (pool && (int)pool->tag.size() - pool->in_use < 1) { m_refused++; return false; }

        Message w;
        w.copy(msg);
        uint64_t lsrc = msg.tl.source, lsink = msg.tl.sink;          // the values on this port's link
        int id = -1;
        if (pool)
        {
            id = pool->alloc(ch == Channel::D ? msg.tl.sink : msg.tl.source);
            if (ch == Channel::D) lsink = (uint64_t)id; else lsrc = (uint64_t)id;
        }
        // the network's values: the crossbar's namespaces (TLNetPort.h)
        if (m_role == tl::Role::Master && (ch == Channel::A || ch == Channel::C)) w.tl.source = (uint32_t)(m_src_base + lsrc);
        if (m_role == tl::Role::Slave && ch == Channel::D) w.tl.sink = (uint32_t)(m_sink_base + lsink);
        if (ch == Channel::B) w.tl.source = 0;

        // the routing the crossbar would do must be Message::to (TLPort's check: a home map the address
        // map does not express is refused at its first message)
        int want = -1;
        if (ch == Channel::A || ch == Channel::C) want = m_b->endpointOfAddress(w.addr, m_memory_side);
        else if (ch == Channel::D) want = m_b->endpointOfSource(w.tl.source, m_memory_side);
        else if (ch == Channel::E) want = m_b->endpointOfSink(w.tl.sink);
        if (ch != Channel::B && want != (int)msg.to[0])
        {
            if (pool) { uint32_t t; pool->release((uint64_t)id, &t); }
            fatal("the message is addressed to endpoint " + std::to_string(msg.to[0]) + " but its " +
                  (ch == Channel::D ? "source" : ch == Channel::E ? "sink" : "address") + " belongs to " + std::to_string(want) +
                  " (the network's TileLink address map is the controllers' home map: home_ids / home_interleave_shift, mem_ids / mem_interleave_shift): " + describe(w));
        }
        if (!m_ni->pushMessage(w, cycle, type))
        {
            if (pool) { uint32_t t; pool->release((uint64_t)id, &t); }
            m_refused++;
            return false;
        }
        msg.from = w.from;
        if (ch == Channel::B)
            for (int d : msg.to) present(w, ch, m_b->sourceBaseOf(d), lsink);   // one Probe per target, its base source (TLPort)
        else
            present(w, ch, lsrc, lsink);
        m_sent++;
        return true;
    }

    // The endpoint's RX: the tail flit is in the NI's receive queue (accepted in the TileLink sense): the
    // beat on this port's link, the identifier its response completes freed, the handler's tag restored.
    void TLNetPort::delivered(int, Message &m)
    {
        const int oc = m.tl.channel;
        if (!receives(oc)) fatal(std::string("delivery on channel ") + channelName(oc) + ", which this port does not receive: " + describe(m));
        const Channel ch = Channel(oc - 1);
        if (m_role == tl::Role::Master && ch == Channel::D)
        {
            if (m.tl.source < m_src_base) fatal("a D whose source is below this master's range (base " + std::to_string(m_src_base) + "): " + describe(m));
            const uint64_t local = m.tl.source - m_src_base;
            present(m, ch, local, m.tl.sink);
            // the D answers an A (AccessAck[Data], HintAck, Grant[Data]) or a Release (ReleaseAck)
            IdPool *pool = m.tl.opcode == (uint8_t)tl::OpD::ReleaseAck ? poolFor(3) : poolFor(1);
            uint32_t tag = 0;
            if (!pool->release(local, &tag))
                fatal(std::string("a D ") + tl::opcodeName(ch, m.tl.opcode) + " with source " + std::to_string(local) + ", which is not outstanding: " + describe(m));
            m.tl.source = tag;
        }
        else if (m_role == tl::Role::Slave && ch == Channel::E)
        {
            if (m.tl.sink < m_sink_base) fatal("a GrantAck whose sink is below this slave's range (base " + std::to_string(m_sink_base) + "): " + describe(m));
            const uint64_t local = m.tl.sink - m_sink_base;
            present(m, ch, m.tl.source, local);
            uint32_t tag = 0;
            if (!poolFor(4)->release(local, &tag))
                fatal("a GrantAck with sink " + std::to_string(local) + ", which is not outstanding: " + describe(m));
            m.tl.sink = tag;
        }
        else
            present(m, ch, m.tl.source, m.tl.sink);
        m_delivered++;
    }

    std::string TLNetPort::dump() const
    {
        std::ostringstream o;
        o << "[TL] network port " << m_interface_id << " " << m_name << " " << tl::roleName(m_role) << (m_memory_side ? " memory-side" : "")
          << " delivered " << m_delivered << " sent " << m_sent << " refused " << m_refused << ", local fills " << m_local.size() << "\n";
        for (int oc : {1, 3, 4})
            if (const IdPool *p = poolFor(oc))
                o << "[TL]   ids (" << channelName(oc) << ") " << p->in_use << "/" << p->tag.size() << " in use, reserved " << p->reserved << "\n";
        return o.str();
    }

    // ------------------------------------------------------------------ TLNetBoundary
    TLNetBoundary *TLNetBoundary::s_instance = nullptr;
    void TLNetBoundary::fatal(const std::string &msg) { TileLinkInterconnect::fatal(msg); }

    int TLNetBoundary::geti(const std::string &k, int def) const
    {
        auto it = m_map.find(k);
        if (it == m_map.end()) return def;
        if (!std::holds_alternative<int>(it->second.value)) fatal("configuration key interconnect." + k + " must be an integer (i)");
        return std::get<int>(it->second.value);
    }
    std::string TLNetBoundary::gets(const std::string &k, const std::string &def) const
    {
        auto it = m_map.find(k);
        if (it == m_map.end()) return def;
        if (!std::holds_alternative<std::string>(it->second.value)) fatal("configuration key interconnect." + k + " must be a string (s)");
        return std::get<std::string>(it->second.value);
    }
    static unsigned bitsFor(uint64_t n) { unsigned b = 0; while ((uint64_t(1) << b) < n) b++; return b; }

    TLNetBoundary::TLNetBoundary(net::Network *net, const Configurable::ParametersMap &map, const Endpoints &e)
        : m_net(net), m_map(map), m_l1s(e.l1s), m_banks(e.banks), m_mem_ports(e.mem_ports), m_mems(e.mems)
    {
        if (!net->classifier().tileLink()) fatal("TileLink over the network needs num_classes 5 or 7 (docs/TileLink_Plan.md s5.3 I-C)");
        if (m_l1s.empty() || m_banks.empty()) fatal("TileLink over the network needs the L1s and the LLC banks");
        if (m_mem_ports.empty() != m_mems.empty() || (!m_mem_ports.empty() && m_mem_ports.size() != m_banks.size()))
            fatal("TileLink with memory on the network needs one memory port per LLC bank and the memory controllers");
        if (!m_mem_ports.empty() && net->cfg().num_classes != 7)
            fatal("TileLink with memory on the network needs num_classes 7 (the outer link's own classes, docs/TileLink_Plan.md s5.3 I-C), got " +
                  std::to_string(net->cfg().num_classes));
        // TLC_Tables s11.1 item 11: a bank's probe fan-out (an eviction of a block every cache holds)
        // reserves one B packet per cache before it runs; a shallower inject queue never admits it
        if (net->cfg().inject_depth < (int)m_l1s.size())
            fatal("inject_queue_depth " + std::to_string(net->cfg().inject_depth) + " is below the probe fan-out of " + std::to_string(m_l1s.size()) +
                  " caches (TLC_Tables s11.1 item 11): set inject_queue_depth(i) >= " + std::to_string(m_l1s.size()));

        const unsigned block = (unsigned)geti("block_bytes", 64);
        const unsigned o = (unsigned)geti("tl_o", 4), i = (unsigned)geti("tl_i", 4), mo = (unsigned)geti("tl_mem_o", 6);
        tl::LinkParams base; base.w = block; base.a = 64; base.z = 3; base.level = tl::Level::C;
        tl::LinkParams lp = base, sp = base, mp = base, dp = base;
        lp.o = o; lp.i = bitsFor((uint64_t)m_banks.size() << i);
        sp.o = bitsFor((uint64_t)m_l1s.size() << o); sp.i = i;
        mp.level = dp.level = tl::Level::UH; mp.i = dp.i = 0;
        mp.o = mo; dp.o = bitsFor((uint64_t)std::max<size_t>(1, m_mem_ports.size()) << mo);

        tl::KernelOptions ko;
        ko.reset_cycles = 0;                      // plan s3.2: no reset inside Octopus
        ko.data_mode = tl::DataMode::None;        // the links carry headers; the bytes travel in the packets
        m_kernel.reset(new tl::Kernel(ko));

        // the NoC as the library's Xbar (never stepped): the source and sink ranges and the address maps
        auto xbarConfig = [&](const std::vector<int> &slaves, const char *prefix, int shift) {
            tl::Config xc;
            xc.set("block_bytes(i)=" + std::to_string(block));
            if (slaves.size() > 1)
            {
                std::string line = "routes(vs)=";
                for (size_t k = 0; k < slaves.size(); k++)
                    line += (k ? "," : "") + std::string(prefix) + std::to_string(slaves[k]) + ":interleave:" + std::to_string(shift) + ":" +
                            std::to_string(slaves.size()) + ":" + std::to_string(k);
                xc.set(line);
            }
            return xc;
        };
        auto niOf = [&](int id) {
            if (!net->topology().isEndpoint(id)) fatal("endpoint " + std::to_string(id) + " of a TileLink role is not a network endpoint (component_ids)");
            return static_cast<net::NetworkInterface *>(net->getInterfaceFor(id));
        };
        auto add = [&](int id, tl::Role role, bool mem) {
            TLNetPort *p = new TLNetPort(this, niOf(id), id, "port" + std::to_string(id), role, mem);
            m_kernel->add(std::unique_ptr<tl::Agent>(p));
            m_ports[id] = p;
        };
        try
        {
            for (int id : m_l1s) add(id, tl::Role::Master, false);
            m_inner = new tl::Xbar("noc", xbarConfig(m_banks, "S", e.bank_shift));
            m_kernel->add(std::unique_ptr<tl::Agent>(m_inner));
            for (int id : m_banks) add(id, tl::Role::Slave, false);
            for (int id : m_l1s) m_link_endpoint[&m_kernel->connect("L" + std::to_string(id), lp, *m_ports[id], *m_inner)] = id;
            for (int id : m_banks) m_link_endpoint[&m_kernel->connect("S" + std::to_string(id), sp, *m_inner, *m_ports[id])] = id;
            if (!m_mem_ports.empty())
            {
                // memory on the network: the outer links, a second Xbar (plan s5.3 I-C: the NoC as two
                // decoupled agents, inner and outer, on disjoint classes)
                for (int id : m_mem_ports) add(id, tl::Role::Master, true);
                m_outer = new tl::Xbar("noc_out", xbarConfig(m_mems, "D", e.mem_shift));
                m_kernel->add(std::unique_ptr<tl::Agent>(m_outer));
                for (int id : m_mems) add(id, tl::Role::Slave, true);
                for (int id : m_mem_ports) m_link_endpoint[&m_kernel->connect("M" + std::to_string(id), mp, *m_ports[id], *m_outer)] = id;
                for (int id : m_mems) m_link_endpoint[&m_kernel->connect("D" + std::to_string(id), dp, *m_outer, *m_ports[id])] = id;
            }
        }
        catch (tl::ConfigError &x) { fatal(x.what()); }

        // plan s6: a monitor on every link; CoherenceCheck (SWMR, C-GRANT, C-PERM) on the L1s' links
        if (geti("tl_monitor", 1) != 0)
        {
            tl::MonitorOptions mo_;
            std::string mode = gets("tl_monitor_mode", "fatal");
            if (!tl::MonitorOptions::parseMode(mode, mo_.mode)) fatal("tl_monitor_mode must be fatal or report, got " + mode);
            mo_.block_bytes = block;
            m_monitors = tl::Monitor::attachAll(*m_kernel, mo_);
        }
        if (geti("tl_coherence_check", 1) != 0)
            m_kernel->addObserver(std::unique_ptr<tl::Observer>(new tl::CoherenceCheck(block)));
        m_kernel->elaborate();                    // link parameters, capabilities, the Xbars' maps; fatal on a refusal

        for (auto &kv : m_ports)
        {
            TLNetPort *p = kv.second;
            tl::Xbar *x = p->memorySide() ? m_outer : m_inner;
            if (p->role() == tl::Role::Master) p->setBases(x->sourceBase(*p->link()), 0);
            else p->setBases(0, x->sinkBase(*p->link()));
            p->ni()->setDeliverHook([p](int cls, Message &m) { p->delivered(cls, m); });
        }
        s_instance = this;
        static bool registered = false;
        if (!registered) { std::atexit(atExit); registered = true; }
        HangDump::add([this]() { std::cerr << dump(); });
        Logger::getLogger()->enableTileLinkColumns();
    }

    TLNetBoundary::~TLNetBoundary()
    {
        finish();
        m_monitors.clear();
        if (s_instance == this) s_instance = nullptr;
    }

    uint64_t TLNetBoundary::tick() const { return m_net->tick(); }

    CommunicationInterface *TLNetBoundary::getInterfaceFor(int id)
    {
        TLNetPort *p = port(id);
        if (!p) fatal("endpoint " + std::to_string(id) + " has no TileLink role on the network (an L1, an LLC bank, a memory port or a memory controller)");
        return p;
    }
    TLNetPort *TLNetBoundary::port(int id) const
    {
        auto it = m_ports.find(id);
        return it == m_ports.end() ? nullptr : it->second;
    }

    int TLNetBoundary::endpointOfAddress(uint64_t addr, bool outer) const
    {
        tl::Xbar *x = outer ? m_outer : m_inner;
        tl::Link *l = x ? x->routeOf(addr) : nullptr;
        if (!l) return -1;
        auto it = m_link_endpoint.find(l);
        return it == m_link_endpoint.end() ? -1 : it->second;
    }
    int TLNetBoundary::endpointOfSource(uint64_t source, bool outer) const
    {
        tl::Xbar *x = outer ? m_outer : m_inner;
        for (int id : outer ? m_mem_ports : m_l1s)
        {
            const tl::Link *l = m_ports.at(id)->link();
            uint64_t b = x->sourceBase(*l);
            if (source >= b && source - b < l->params().numSources()) return id;
        }
        return -1;
    }
    int TLNetBoundary::endpointOfSink(uint64_t sink) const
    {
        for (int id : m_banks)
        {
            const tl::Link *l = m_ports.at(id)->link();
            uint64_t b = m_inner->sinkBase(*l);
            if (sink >= b && sink - b < l->params().numSinks()) return id;
        }
        return -1;
    }
    uint64_t TLNetBoundary::sourceBaseOf(int master_id) const
    {
        TLNetPort *p = port(master_id);
        if (!p || p->role() != tl::Role::Master || p->memorySide()) fatal("a B message addressed to " + std::to_string(master_id) + ", which is not an L1 (a TileLink master)");
        return m_inner->sourceBase(*p->link());
    }

    std::string TLNetBoundary::dump() const
    {
        std::ostringstream o;
        o << "[TL] TileLink over the network: tick " << tick() << ", " << monitorViolations() << " monitor violations\n";
        for (auto &kv : m_ports) o << kv.second->dump();
        return o.str();
    }

    void TLNetBoundary::finish()
    {
        if (m_finished || !m_kernel || tick() == 0) return;   // tick 0: refused at construction, no verdict to print
        m_finished = true;
        bool report = !m_monitors.empty() && m_monitors[0]->options().mode == tl::MonitorOptions::Report;
        if (report) tl::Monitor::finishAll(m_monitors, tick());   // the end-of-run rules: report mode only, as TileLinkInterconnect
        if (!m_monitors.empty())
        {
            std::cerr << "TileLink: " << m_monitors.size() << " link monitors, " << monitorViolations() << " violations, " << tick()
                      << " cycles (message-level links at the network interfaces, docs/TileLink_Plan.md s5.3 I-C)" << std::endl;
            for (auto &m : m_monitors) if (m->violations()) std::cerr << "TileLink: " << m->summary() << std::endl;
        }
        if (tl::CoherenceCheck *c = tl::CoherenceCheck::of(*m_kernel))
        {
            const tl::CoherenceCheck::Counters &n = c->counters();
            std::cerr << "TileLink: coherence check: " << n.grants << " grants, " << n.loads << " loads and " << n.stores
                      << " stores served from a cache's copy checked (C-PERM), " << n.violations << " violations" << std::endl;
        }
    }
    void TLNetBoundary::atExit() { if (s_instance) s_instance->finish(); }
}
