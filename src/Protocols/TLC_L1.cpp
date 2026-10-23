/*
 * File  :      TLC_L1.cpp
 * The TL-C L1 handler: the library's L1 table and policy engine inside Octopus's L1 controller.
 * Specification: docs/TileLink_Plan.md s5.1-s5.3 I-A; the cells and their emissions:
 * docs/TileLink_TLC_Tables.md s2, s7.1, s8. The engine rules and their Octopus form: TLC_L1.h.
 */

#include "../../header/Protocols/TLC_L1.h"
#include "../../header/Interconnect/TileLink/TLPort.h"
#include <cstdio>
#include <sstream>

using namespace std;

namespace octopus
{
    namespace tc = tl::coh;

    TLC_L1::TLC_L1(CacheDataHandler *cache, const string &fsm_path, int id, int sharedMemId)
        : DirectoryProtocol(cache, tlc::tablePath(fsm_path), id, sharedMemId)
    {
        const string path = tlc::tablePath(fsm_path);
        try { m_policy.reset(new tc::L1Policy(tc::Table::load(path))); }
        catch (tl::ConfigError &e)
        {
            cerr << "TLC_L1 " << id << ": " << e.what() << endl;
            exit(2);
        }
        const tc::Table &t = m_policy->table();
        // the two readers of one file (TLC_Common.h) must agree, or the trace names the wrong rows
        if (m_fsm->numStates() != t.numStates() || m_fsm->numEvents() != t.numEvents())
        {
            cerr << "TLC_L1 " << id << ": FSMReader reads " << m_fsm->numStates() << " states and " << m_fsm->numEvents() << " events from " << path
                 << ", the policy engine " << t.numStates() << " and " << t.numEvents() << endl;
            exit(2);
        }
        m_stall_id = t.actionId("Stall");
        m_lg_block = tlc::lgOf(m_data_handler->getBlockSize());
    }

    TLC_L1::~TLC_L1()
    {
    }

    int TLC_L1::stateOf(uint64_t addr) const
    {
        GenericCacheLine line;
        return m_data_handler->readLineBits(addr, &line) ? line.state : m_policy->stateInvalid();
    }

    void TLC_L1::fault(const string &why, const Message &msg, int state, int event) const
    {
        const tc::Table &t = m_policy->table();
        ostringstream o;
        o << "TLC_L1 " << m_id << ": Fault: " << why << " (line 0x" << hex << blockOf(msg.addr) << dec << ", state "
          << (state >= 0 ? t.stateName(state) : string("?")) << ", event " << (event >= 0 ? t.eventName(event) : string("?")) << ", msg " << msg.msg_id
          << ", source " << (int)msg.source << ", from " << msg.from << ", kind " << (int)msg.kind << ", tl ch " << (int)msg.tl.channel
          << " opcode " << (int)msg.tl.opcode << " param " << (int)msg.tl.param << " denied " << msg.tl.denied << ")";
        cout << o.str() << endl;
        cerr << o.str() << endl;
        exit(2);
    }

    // TLC_Tables s2.2
    int TLC_L1::decode(const Message &msg) const
    {
        tc::Decoded d;
        switch (msg.source)
        {
            case Message::Source::LOWER_INTERCONNECT:      // the CPU: Load / Store (as MSIDirectory::readEvent)
                if (msg.complementary_value > 1) fault("a CPU message that is neither a Load nor a Store", msg, -1, -1);
                d = m_policy->decodeCpu(msg.complementary_value == 1);
                break;
            case Message::Source::SELF:                    // CacheController::checkReplacements' victim
                if (msg.owner != m_id) fault("a self-raised message of another controller", msg, -1, -1);
                d = m_policy->decodeReplacement();
                break;
            case Message::Source::UPPER_INTERCONNECT:      // the TileLink port: B (Probes) and D (Grants, ReleaseAck)
                if (msg.tl.channel != 2 && msg.tl.channel != 4)
                    fault("the TileLink port delivered a message that is not on B or D (an L1 receives nothing else; Message::tl.channel 0 = untagged)", msg, -1, -1);
                d = m_policy->decode(tlc::toHeader(msg));
                if (!d.ok()) fault("decode: " + d.fault, msg, stateOf(msg.addr), -1);
                break;
        }
        return d.event;
    }

    // CacheController::checkReplacements raises the Replacement when the fill moves the victim to the
    // write-back buffer, and the event runs later; a Probe toN may take the line meanwhile (S, E or M
    // in the buffer -> I, the buffer entry goes), and a new access may even bring the block back
    // (MSHR, then the array). A victim stays in the buffer until its Replacement has run, so a
    // Replacement whose line is not in the buffer is stale: a no-op, as the library's CacheMaster
    // treats its Evict access ("a no-op when the line is absent", CacheMaster.h). TLC_Tables s2.4's
    // (I, Replacement) Fault assumes the victim is still the line the event names.
    bool TLC_L1::staleReplacement(const Message &msg) const
    {
        return msg.source == Message::Source::SELF && m_data_handler->lineLocation(msg.addr) != CacheDataHandler::LineLocation::PWB;
    }

    FRFCFS_State TLC_L1::getRequestState(const Message &msg, FRFCFS_State)
    {
        if (staleReplacement(msg)) return FRFCFS_State::Ready;
        // TLC_Tables s1.2 rule 2: a Stall cell leaves the message in the queue
        return isStall(stateOf(msg.addr), decode(msg)) ? FRFCFS_State::NonReady : FRFCFS_State::Ready;
    }

    // every CPU Load and Store, with or without its bytes (the external CPU's Store carries them):
    // the queue's per-line order and the MSHR count (docs/TileLink_Review_Action_Plan.md problems 1, 3)
    bool TLC_L1::isCpuDemand(const Message &m) const
    {
        return m.source == Message::Source::LOWER_INTERCONNECT && m.tl.channel == 0 && m.complementary_value <= 1;
    }

    bool TLC_L1::isReadableState(int state)
    {
        const string &n = m_policy->table().stateName(state);
        return n == "S" || n == "E" || n == "M" || n == "SM_a";
    }

    Message TLC_L1::emission(int action, const Message &trigger, uint64_t msg_id) const
    {
        const uint64_t block = blockOf(trigger.addr);
        tl::MsgHeader th = tlc::toHeader(trigger), h;
        // L1Policy::build (TLC_Tables s2.3, s6): a ProbeAck copies the Probe's source, address and
        // size (S9.3.5), a GrantAck the Grant's sink (S9.3.9); an Acquire or Release gets its source
        // from the port's pool (TLPort), so the handler's own value is 0.
        m_policy->build(action, block, m_lg_block, trigger.tl.channel ? &th : nullptr, 0, h);
        Message m(msg_id, block, 0, tlc::REQUEST_TYPE_TL, (uint16_t)m_id);
        tlc::setHeader(m, h);
        m.to.push_back((uint16_t)homeOf(block));
        const tc::L1Action &a = m_policy->action(action);
        switch (a.op)   // TLC_Tables s8
        {
            case tc::L1Op::AcquireBlock:
                m.kind = a.param == uint8_t(tl::Grow::NtoB) ? Message::K_GETS : Message::K_GETM;
                m.complementary_value = a.param == uint8_t(tl::Grow::NtoB) ? tlc::REQUEST_TYPE_ACQUIRE_READ : tlc::REQUEST_TYPE_ACQUIRE_WRITE;
                break;
            case tc::L1Op::GrantAck:     m.kind = Message::K_GRANT_ACK; break;
            case tc::L1Op::ProbeAck:     m.kind = Message::K_ACK; break;
            case tc::L1Op::ProbeAckData: m.kind = Message::K_WB_DATA; break;
            case tc::L1Op::Release:      m.kind = a.param == uint8_t(tl::Prune::BtoN) ? Message::K_PUTS : Message::K_PUTE; break;
            case tc::L1Op::ReleaseData:  m.kind = Message::K_PUTM; break;
            default: break;
        }
        return m;
    }

    vector<ControllerAction> TLC_L1::processRequest(Message &msg, DebugPrint *dprint)
    {
        const tc::Table &t = m_policy->table();
        const int event = decode(msg);
        const int state = stateOf(msg.addr);
        const tc::Cell &cell = t.transition(state, event);
        const int next = cell.next;
        const bool stall = isStall(state, event);
        if (dprint)   // coherence transition hook (docs/Debugger.md): readable names + raw-trace FSM record
            dprint->transition(&msg, (uint32_t)m_id, state, event, stall ? state : next, stall, this->m_fsm);

        vector<ControllerAction> pre, pending, post, retry, out;
        auto act = [](vector<ControllerAction> &v, ControllerAction::Type type, Message *m) { v.push_back(ControllerAction{.type = type, .data = (void *)m}); };
        if (staleReplacement(msg)) return out;

        // TLC_Tables s1.2 rule 2: taken as ready, but the line moved meanwhile (the intake decides
        // once for an upper-interface message): back to the queue, re-decoded when the line changes
        if (stall)
        {
            act(out, ControllerAction::Type::STALL, new Message(msg));
            return out;
        }
        for (int id : cell.actions)
            if (m_policy->action(id).op == tc::L1Op::Fault) fault("a Fault cell of " + t.name(), msg, state, event);

        const bool cpu_event = event == m_policy->eventLoad() || event == m_policy->eventStore();
        const bool fill = msg.source == Message::Source::UPPER_INTERCONNECT && msg.tl.channel == 4 &&
                          msg.tl.opcode == uint8_t(tl::OpD::GrantData) && !msg.tl.denied && msg.data != NULL;
        const bool next_valid = t.dataValid(next);
        bool answered = false;
        for (int id : cell.actions)
        {
            const tc::L1Action &a = m_policy->action(id);
            switch (a.op)
            {
                case tc::L1Op::Hit:
                    if (cpu_event)
                    {
                        // rule 6: serve the CPU from the line, a Store writes it (MSIDirectory's Hit)
                        // C-PERM: the access is served from this cache's copy; its link must show the permission
                        if (TLPort *p = TLPort::forEndpoint(m_id)) p->accessPerformed(msg.addr, event == m_policy->eventStore());
                        act(post, ControllerAction::Type::HIT_Action, new Message(msg));
                        act(post, ControllerAction::Type::MODIFY_DATA, new Message(msg));
                    }
                    else
                    {
                        // rule 3: the Grant cell answers the parked access, from the GrantData's bytes
                        // or, for a Grant without data (SM_a + Grant_toT), from the line
                        Message *r = new Message(msg);
                        r->tl = Message::TLHeader();   // the CPU response is not a TileLink message
                        act(post, ControllerAction::Type::REMOVE_PENDING, r);
                        act(post, ControllerAction::Type::MODIFY_DATA, new Message(msg));
                    }
                    answered = true;
                    break;
                case tc::L1Op::Error:
                    // TLC_Tables s2.3, Octopus's policy (docs/TileLink_Denied_Grant_Retry_Plan.md): the denied
                    // Grant answers no CPU access. The cell's GrantAck and state update run as for any Grant;
                    // the line's parked access is then replayed for a new Acquire (after every other action
                    // of the cell), and the CPU is answered once, by the Grant that succeeds. The standalone
                    // CacheMaster instead completes its access with an error.
                    act(retry, ControllerAction::Type::TL_RETRY_PENDING, new Message(msg));
                    answered = true;
                    break;
                case tc::L1Op::AcquireBlock:
                case tc::L1Op::GrantAck:
                case tc::L1Op::ProbeAck:
                case tc::L1Op::Release:
                    // no data: straight to the TileLink port (BaseController::sendBusRequest)
                    act(post, ControllerAction::Type::SEND_BUS_MSG, new Message(emission(id, msg, msg.msg_id)));
                    break;
                case tc::L1Op::ProbeAckData:
                case tc::L1Op::ReleaseData:
                {
                    // the line's bytes (BaseController::performWriteBack reads them and addresses the
                    // home); before the state update when the cell leaves the line invalid
                    Message *m = new Message(emission(id, msg, msg.msg_id));
                    m->to.clear();
                    act(next_valid ? post : pre, ControllerAction::Type::WRITE_BACK, m);
                    break;
                }
                case tc::L1Op::Stall:
                case tc::L1Op::Fault:
                    break;   // handled above
            }
        }
        // TLC_Tables s1.2 rule 3: a CPU access that leaves a stable state for a transient one is parked
        if (cpu_event && !answered && t.stable(state) && !t.stable(next))
            act(pending, ControllerAction::Type::ADD_PENDING, new Message(msg));

        // the line's new state; a GrantData's bytes fill it (rule 6), nothing else writes the array here
        GenericCacheLine line;
        m_data_handler->readLineBits(msg.addr, &line);
        line.state = next;
        line.valid = next_valid;
        Message bits(msg);
        if (!fill && bits.data != NULL) { delete[] bits.data; bits.data = NULL; bits.data_size = 0; }
        ControllerAction update;
        update.type = ControllerAction::Type::UPDATE_CACHE_LINE;
        update.data = (void *)new uint8_t[sizeof(Message) + sizeof(GenericCacheLine)];
        new (update.data) Message(bits);
        new ((uint8_t *)update.data + sizeof(Message)) GenericCacheLine(line);

        out = pre;
        out.push_back(update);
        out.insert(out.end(), pending.begin(), pending.end());
        out.insert(out.end(), post.begin(), post.end());
        out.insert(out.end(), retry.begin(), retry.end());
        return out;
    }
}

// ---- class annotation (docs/NoC_Architecture_Spec.md s14.3) and admission (docs/TileLink_Plan.md s5.2)
namespace octopus
{
    bool TLC_L1::consumedBy(const string &e, MsgClass &o) const
    {
        o = MsgClass();
        if (e == "Load" || e == "Store" || e == "Replacement") return true;          // the CPU / the controller itself
        o.from_home = true; o.tl_channel = 2;
        if (e == "ProbeBlock_toB" || e == "ProbeBlock_toT") { o.kind = Message::K_FWD; return true; }
        if (e == "ProbeBlock_toN") { o.kind = Message::K_INV; return true; }        // K_FWD too, when the home serves an Acquire
        o.tl_channel = 4;
        if (e == "GrantData_toB" || e == "GrantData_toT") { o.kind = Message::K_RESP; o.data = true; return true; }
        if (e == "Grant_toB" || e == "Grant_toT" || e == "Grant_denied") { o.kind = Message::K_RESP; return true; }
        if (e == "ReleaseAck") { o.kind = Message::K_ACK; return true; }
        return false;
    }
    bool TLC_L1::emittedBy(const string &a, vector<MsgClass> &o) const
    {
        o.clear(); MsgClass m;
        if (a == "Stall" || a == "Hit" || a == "Fault" || a == "Error") return true;
        if (a == "AcquireBlock_NtoB") { m.kind = Message::K_GETS; m.tl_channel = 1; o.push_back(m); return true; }
        if (a == "AcquireBlock_NtoT" || a == "AcquireBlock_BtoT") { m.kind = Message::K_GETM; m.tl_channel = 1; o.push_back(m); return true; }
        if (a == "GrantAck") { m.kind = Message::K_GRANT_ACK; m.tl_channel = 5; o.push_back(m); return true; }
        m.tl_channel = 3;
        if (a.compare(0, 13, "ProbeAckData_") == 0) { m.kind = Message::K_WB_DATA; m.data = true; o.push_back(m); return true; }
        if (a.compare(0, 9, "ProbeAck_") == 0) { m.kind = Message::K_ACK; o.push_back(m); return true; }
        if (a == "Release_BtoN") { m.kind = Message::K_PUTS; o.push_back(m); return true; }
        if (a == "Release_TtoN") { m.kind = Message::K_PUTE; o.push_back(m); return true; }
        if (a == "ReleaseData_TtoN") { m.kind = Message::K_PUTM; m.data = true; o.push_back(m); return true; }
        return false;
    }

    // plan s5.2 point 1: the emissions of the cell the message will take now (TLC_Tables s7.1). The L1
    // never fans out, so every count is 1; a Stall cell is never popped and predicts nothing.
    bool TLC_L1::predictEmissions(const Message &msg, GenericCacheLine &, vector<MsgClass> &out)
    {
        out.clear();
        if (staleReplacement(msg)) return true;
        const tc::Table &t = m_policy->table();
        const int event = decode(msg), state = stateOf(msg.addr);
        if (isStall(state, event)) return true;
        for (int id : t.transition(state, event).actions)
        {
            vector<MsgClass> v;
            if (!emittedBy(t.actionName(id), v))
            {
                cerr << "TLC_L1 " << m_id << ": action " << t.actionName(id) << " of " << t.name() << " has no class annotation" << endl;
                exit(2);
            }
            out.insert(out.end(), v.begin(), v.end());
        }
        return true;
    }
}
