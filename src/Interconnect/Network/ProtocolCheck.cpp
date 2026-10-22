/*
 * File  :      ProtocolCheck.cpp
 * The protocol-and-network deadlock check (docs/NoC_Architecture_Spec.md s14.3): read every
 * controller's FSM table, classify what each cell consumes and what it emits onto the network,
 * and verify the consumption order RESPONSE > FORWARD > REQUEST: a cell may only emit a class
 * strictly higher than the one it consumes (the top class may emit itself: it is drained
 * unconditionally, ADMIT-RF). The verdict is printed once; a violation names the cell.
 * TileLink (5 or 7 classes, spec s6.3): the classes are the channels, in the order
 * A < B < C < D < E or A_in < B < C < A_out < D_out < D_in < E, and the same rule applies.
 * Every cell of an event the controller raises itself (Replacement) that emits is listed: its
 * emissions are ordered against nothing (docs/TileLink_Plan.md s5.3 I-C).
 */
#include "../../../header/Interconnect/Network/Network.h"
#include "../../../header/Interconnect/Network/Classifier.h"
#include "../../../header/CacheControllers/BaseController.h"
#include <iostream>
#include <set>
#include <sstream>
#include <typeinfo>

namespace octopus { namespace net
{
    Network::ProtocolVerdict Network::analyzeProtocols(const std::vector<ProtocolTable> &tables, bool memory_on_network) const
    {
        typedef CoherenceProtocolHandler::MsgClass MsgClass;
        const Classifier &cl = *m_classifier;
        auto className = [&](int c) { return cl.className(c); };
        bool tl_net = cl.tileLink();
        if (tl_net && memory_on_network && m_cfg.num_classes != 7)
            throw ConfigError("TileLink with memory on the network needs num_classes 7 (the outer link's own classes, spec s6.3), got " + std::to_string(m_cfg.num_classes));
        int home = m_home_ids.empty() ? -1 : m_home_ids[0];
        int memory = -1;                     // any endpoint that is neither a cache nor a home: the class rules of memory kinds ignore it
        int core = -1; for (int id : m_component_ids) if (!cl.isHome(id) && !cl.isMemory(id)) { core = id; break; }
        // the network's class of a message, without the fatal path (an unclassifiable annotation is skipped)
        auto netClass = [&](const Message &m, int dst) {
            if (m_debug == "memread-request" && m.kind == Message::K_MEM_READ) return (int)CLASS_REQUEST;
            return cl.classify(m, dst);
        };
        // a memory-side endpoint, for the outer link's TileLink messages (both ends memory-side)
        auto memEndpoint = [&]() {
            if (cl.memory().empty())
                throw ConfigError("TileLink with memory on the network: the memory-side endpoints are not known (memory_ids, or Network::setMemoryEndpoints before checkProtocols)");
            return *cl.memory().begin();
        };
        // an annotation is a network message if it names a kind or a TileLink channel
        auto onNet = [](const MsgClass &mc) { return mc.kind >= 0 || mc.tl_channel != 0; };
        // a message of the annotated shape, classified as the network would
        auto classOf = [&](const MsgClass &mc, const std::string &key) {
            if ((mc.tl_channel != 0) != tl_net)
                throw ConfigError(key + (mc.tl_channel != 0 ? " annotates TileLink channels but the network has " + std::to_string(m_cfg.num_classes) + " classes (TileLink needs num_classes 5 or 7)"
                                                            : " annotates Octopus message kinds but the network has " + std::to_string(m_cfg.num_classes) + " (TileLink) classes"));
            Message m(1, 0, 0, 0, (uint16_t)(mc.from_home ? home : core));
            m.kind = (uint8_t)(mc.kind >= 0 ? mc.kind : Message::K_UNKNOWN); m.from = (uint16_t)(mc.from_home ? home : core);
            uint8_t buf[64] = {0}; if (mc.data) m.copy(buf, 64);
            if (mc.tl_channel != 0)
            {
                m.tl.channel = (uint8_t)mc.tl_channel;
                if (mc.memory) { int mem = memEndpoint(); m.from = (uint16_t)mem; m.owner = (uint16_t)mem; return netClass(m, mem); }   // the outer link
                return netClass(m, mc.from_home ? core : home);
            }
            if (mc.kind == Message::K_MEM_READ || mc.kind == Message::K_MEM_WRITE || mc.kind == Message::K_FILL || mc.kind == Message::K_MEM_WACK)
                return netClass(m, mc.from_home ? memory : home);
            return netClass(m, mc.from_home ? core : home);
        };
        int top = m_cfg.num_classes - 1;
        ProtocolVerdict v;
        std::set<std::string> seen, self_seen;
        for (const ProtocolTable &t : tables)
        {
            CoherenceProtocolHandler *p = t.protocol; if (!p || !p->fsm()) continue;
            const char *role = t.llc ? "LLC" : "L1";
            if (!p->hasClassAnnotation()) { v.unannotated.push_back("controller " + std::to_string(t.id) + " (no class annotation in its protocol)"); continue; }
            std::string key = std::string(role) + ":" + typeid(*p).name();
            if (!seen.insert(key).second) continue;               // one table per protocol
            FSMReader *f = p->fsm();
            for (int s = 0; s < f->numStates(); s++)
                for (int e = 0; e < f->numEvents(); e++)
                {
                    int next; std::vector<int> actions; f->getTransition(s, e, next, actions);
                    if (actions.empty()) continue;
                    v.cells++;
                    MsgClass in;
                    std::string ev = f->getEventName(e);
                    if (!p->consumedBy(ev, in)) { v.unannotated.push_back(key + " event " + ev); continue; }
                    int cin = (!onNet(in) || (in.memory && !memory_on_network)) ? -1 : classOf(in, key + " event " + ev);
                    bool self = !onNet(in) && !in.memory && p->selfRaised(ev);    // raised by the controller itself (Replacement)
                    for (int a : actions)
                    {
                        std::string an = f->getActionName(a); std::vector<MsgClass> outs;
                        if (!p->emittedBy(an, outs)) { v.unannotated.push_back(key + " action " + an); continue; }
                        for (auto &o : outs)
                        {
                            if (!onNet(o) || (o.memory && !memory_on_network)) continue;
                            int cout_ = classOf(o, key + " action " + an); if (cout_ < 0) continue;
                            v.emitting++;
                            if (self)
                            {
                                std::string entry = std::string(role) + " (" + f->getStateName(s) + ", " + ev + ") emits " + className(cout_);
                                if (self_seen.insert(entry).second) v.self_emitters.push_back(entry);
                            }
                            if (cin < 0) continue;                                     // consumed from the CPU, the memory or the controller itself: no network class to order against
                            v.edges.insert({cin, cout_});
                            if (cout_ < cin || (cout_ == cin && cin != top))
                                v.violations.push_back(std::string(role) + " cell (" + f->getStateName(s) + ", " + ev + ") consumes " +
                                                       className(cin) + " and emits " + an + " in " + className(cout_));
                        }
                    }
                }
        }
        // the memory controller (spec s25.3): a read is answered with a fill, a write with an acknowledgement;
        // under TileLink (7 classes) a Get on A_out with an AccessAckData on D_out, a PutFullData with an AccessAck
        if (memory_on_network)
        {
            auto memClass = [&](int kind, bool data, int tl_channel) {
                Message m(1, 0, 0, 0, (uint16_t)home); m.kind = (uint8_t)kind; m.from = (uint16_t)home;
                uint8_t buf[64] = {0}; if (data) m.copy(buf, 64);
                if (tl_channel == 0) return netClass(m, memory);
                int mem = memEndpoint(); m.tl.channel = (uint8_t)tl_channel; m.from = (uint16_t)mem; m.owner = (uint16_t)mem;
                return netClass(m, mem);
            };
            struct Rule { int in_kind; bool in_data; int in_ch; int out_kind; bool out_data; int out_ch; const char *name; };
            const Rule octo[] = {
                {Message::K_MEM_READ, false, 0, Message::K_FILL, true, 0, "memory controller (read) emits a fill"},
                {Message::K_MEM_WRITE, true, 0, Message::K_MEM_WACK, false, 0, "memory controller (write) emits an acknowledgement"}};
            const Rule tl[] = {
                {Message::K_UNKNOWN, false, 1, Message::K_UNKNOWN, true, 4, "memory controller (Get) emits AccessAckData"},
                {Message::K_UNKNOWN, true, 1, Message::K_UNKNOWN, false, 4, "memory controller (PutFullData) emits AccessAck"}};
            for (auto &c : (tl_net ? tl : octo))
            {
                int cin = memClass(c.in_kind, c.in_data, c.in_ch), cout_ = memClass(c.out_kind, c.out_data, c.out_ch);
                v.cells++; v.emitting++; v.edges.insert({cin, cout_});
                if (cout_ < cin || (cout_ == cin && cin != top))
                    v.violations.push_back(std::string(c.name) + ": consumes " + className(cin) + " and emits " + className(cout_));
            }
        }
        std::ostringstream o;
        o << "Protocol-network check (" << m_cfg.num_classes << " classes): " << v.cells << " cells, " << v.emitting << " network emissions; edges";
        for (auto &e : v.edges) o << " " << className(e.first) << "->" << className(e.second);
        o << "; " << (v.violations.empty() ? "consumption order holds" : std::to_string(v.violations.size()) + " violation(s)");
        if (!v.unannotated.empty()) o << "; " << v.unannotated.size() << " unannotated (" << v.unannotated[0] << (v.unannotated.size() > 1 ? ", ..." : "") << ")";
        v.line = o.str();
        if (!v.self_emitters.empty())
        {
            std::ostringstream so; so << "self-consumed emitters: ";
            for (size_t i = 0; i < v.self_emitters.size(); i++) so << (i ? ", " : "") << v.self_emitters[i];
            v.self_line = so.str();
        }
        return v;
    }

    void Network::checkProtocols(const std::vector<BaseController *> &controllers, bool memory_on_network)
    {
        std::vector<ProtocolTable> tables;
        for (BaseController *ctrl : controllers)
        {
            CoherenceProtocolHandler *p = ctrl->protocol(); if (!p || !p->fsm()) continue;
            tables.push_back({ctrl->logRole() == Logger::Role::LLC, ctrl->id(), p});
        }
        ProtocolVerdict v;
        try { v = analyzeProtocols(tables, memory_on_network); }
        catch (ConfigError &e) { fatal(e.what()); }
        std::cout << v.line << std::endl;
        if (!v.self_line.empty()) std::cout << v.self_line << std::endl;
        if (v.violations.empty()) return;
        for (auto &s : v.violations) std::cerr << "  " << s << std::endl;
        if (m_cfg.deadlock_check == "warn") { std::cerr << "Network: deadlock_check = warn, continuing" << std::endl; return; }
        fatal("the protocol can emit a class no higher than the one it consumes on " + std::to_string(v.violations.size()) + " FSM cell(s), first: " + v.violations[0] +
              " (a request class waiting behind itself can deadlock, spec s14.3; deadlock_check = warn runs anyway)");
    }
}}
