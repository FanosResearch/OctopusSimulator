/*
 * File  :      ProtocolCheck.cpp
 * The protocol-and-network deadlock check (docs/NoC_Architecture_Spec.md s14.3): read every
 * controller's FSM table, classify what each cell consumes and what it emits onto the network,
 * and verify the consumption order RESPONSE > FORWARD > REQUEST: a cell may only emit a class
 * strictly higher than the one it consumes (the top class may emit itself: it is drained
 * unconditionally, ADMIT-RF). The verdict is printed once; a violation names the cell.
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
    void Network::checkProtocols(const std::vector<BaseController *> &controllers, bool memory_on_network)
    {
        static const char *cname[] = {"REQUEST", "FORWARD", "RESPONSE"};
        auto className = [&](int c) { return m_cfg.num_classes == 2 ? (c == 0 ? "REQUEST" : "DATA") : cname[c]; };
        int home = m_home_ids.empty() ? -1 : m_home_ids[0];
        int memory = -1;                     // any endpoint that is neither a cache nor a home: the class rules of memory kinds ignore it
        int core = -1; for (int id : m_component_ids) if (!m_classifier->isHome(id)) { core = id; break; }
        // the network's class of a message, without the fatal path (an unclassifiable annotation is skipped)
        auto netClass = [&](const Message &m, int dst) {
            if (m_debug == "memread-request" && m.kind == Message::K_MEM_READ) return (int)CLASS_REQUEST;
            return m_classifier->classify(m, dst);
        };
        // a message of the annotated shape, classified as the network would
        auto classOf = [&](const CoherenceProtocolHandler::MsgClass &mc) {
            Message m(1, 0, 0, 0, (uint16_t)(mc.from_home ? home : core));
            m.kind = (uint8_t)mc.kind; m.from = (uint16_t)(mc.from_home ? home : core);
            uint8_t buf[64] = {0}; if (mc.data) m.copy(buf, 64);
            if (mc.kind == Message::K_MEM_READ || mc.kind == Message::K_MEM_WRITE || mc.kind == Message::K_FILL || mc.kind == Message::K_MEM_WACK)
                return netClass(m, mc.from_home ? memory : home);
            return netClass(m, mc.from_home ? core : home);
        };
        int top = m_cfg.num_classes - 1;
        std::set<std::string> seen; std::vector<std::string> violations, unannotated; int cells = 0, emitting = 0;
        std::set<std::pair<int, int>> edges;
        for (BaseController *ctrl : controllers)
        {
            CoherenceProtocolHandler *p = ctrl->protocol(); if (!p || !p->fsm()) continue;
            if (!p->hasClassAnnotation()) { unannotated.push_back("controller " + std::to_string(ctrl->id()) + " (no class annotation in its protocol)"); continue; }
            std::string key = std::string(ctrl->logRole() == Logger::Role::LLC ? "LLC" : "L1") + ":" + typeid(*p).name();
            if (!seen.insert(key).second) continue;               // one table per protocol
            FSMReader *f = p->fsm();
            for (int s = 0; s < f->numStates(); s++)
                for (int e = 0; e < f->numEvents(); e++)
                {
                    int next; std::vector<int> actions; f->getTransition(s, e, next, actions);
                    if (actions.empty()) continue;
                    cells++;
                    CoherenceProtocolHandler::MsgClass in;
                    std::string ev = f->getEventName(e);
                    if (!p->consumedBy(ev, in)) { unannotated.push_back(key + " event " + ev); continue; }
                    int cin = (in.kind < 0 || (in.memory && !memory_on_network)) ? -1 : classOf(in);
                    for (int a : actions)
                    {
                        std::string an = f->getActionName(a); std::vector<CoherenceProtocolHandler::MsgClass> outs;
                        if (!p->emittedBy(an, outs)) { unannotated.push_back(key + " action " + an); continue; }
                        for (auto &o : outs)
                        {
                            if (o.kind < 0 || (o.memory && !memory_on_network)) continue;
                            int cout_ = classOf(o); if (cout_ < 0) continue;
                            emitting++;
                            if (cin < 0) continue;                                     // consumed from the CPU or the memory: no network class to order against
                            edges.insert({cin, cout_});
                            if (cout_ < cin || (cout_ == cin && cin != top))
                                violations.push_back(std::string(ctrl->logRole() == Logger::Role::LLC ? "LLC" : "L1") + " cell (" + f->getStateName(s) + ", " + ev + ") consumes " +
                                                     className(cin) + " and emits " + an + " in " + className(cout_));
                        }
                    }
                }
        }
        // the memory controller (spec s25.3): a read is answered with a fill, a write with an acknowledgement
        if (memory_on_network)
        {
            auto memClass = [&](int kind, bool data) {
                Message m(1, 0, 0, 0, (uint16_t)home); m.kind = (uint8_t)kind; m.from = (uint16_t)home;
                uint8_t buf[64] = {0}; if (data) m.copy(buf, 64);
                return netClass(m, memory);
            };
            const struct { int in_kind; bool in_data; int out_kind; bool out_data; const char *name; } mc[] = {
                {Message::K_MEM_READ, false, Message::K_FILL, true, "memory controller (read) emits a fill"},
                {Message::K_MEM_WRITE, true, Message::K_MEM_WACK, false, "memory controller (write) emits an acknowledgement"}};
            for (auto &c : mc)
            {
                int cin = memClass(c.in_kind, c.in_data), cout_ = memClass(c.out_kind, c.out_data);
                cells++; emitting++; edges.insert({cin, cout_});
                if (cout_ < cin || (cout_ == cin && cin != top))
                    violations.push_back(std::string(c.name) + ": consumes " + className(cin) + " and emits " + className(cout_));
            }
        }
        std::ostringstream o;
        o << "Protocol-network check (" << m_cfg.num_classes << " classes): " << cells << " cells, " << emitting << " network emissions; edges";
        for (auto &e : edges) o << " " << className(e.first) << "->" << className(e.second);
        o << "; " << (violations.empty() ? "consumption order holds" : std::to_string(violations.size()) + " violation(s)");
        if (!unannotated.empty()) o << "; " << unannotated.size() << " unannotated (" << unannotated[0] << (unannotated.size() > 1 ? ", ..." : "") << ")";
        std::cout << o.str() << std::endl;
        if (violations.empty()) return;
        for (auto &v : violations) std::cerr << "  " << v << std::endl;
        if (m_cfg.deadlock_check == "warn") { std::cerr << "Network: deadlock_check = warn, continuing" << std::endl; return; }
        fatal("the protocol can emit a class no higher than the one it consumes on " + std::to_string(violations.size()) + " FSM cell(s), first: " + violations[0] +
              " (a request class waiting behind itself can deadlock, spec s14.3; deadlock_check = warn runs anyway)");
    }
}}
