/*
 * File  :      TLC_LLC.cpp
 * The TL-C inclusive manager handler: the library's manager table and policy engine inside Octopus's
 * LLC controller. Specification: docs/TileLink_Plan.md s5.1-s5.3 I-A; the cells and their emissions:
 * docs/TileLink_TLC_Tables.md s3, s7.2, s8. The engine rules and their Octopus form: TLC_LLC.h.
 */

#include "../../header/Protocols/TLC_LLC.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>

using namespace std;

namespace octopus
{
    namespace tc = tl::coh;

    static vector<TLC_LLC *> s_llcs;   // for the end-of-run line
    static void reportAll() { for (TLC_LLC *l : s_llcs) l->report(); }

    TLC_LLC::TLC_LLC(CacheDataHandler *cache, const string &fsm_path, int id, int sharedMemId)
        : DirectoryProtocol(cache, tlc::tablePath(fsm_path), id, sharedMemId)
    {
        const string path = tlc::tablePath(fsm_path);
        try { m_policy.reset(new tc::ManagerPolicy(tc::Table::load(path))); }
        catch (tl::ConfigError &e)
        {
            cerr << "TLC_LLC " << id << ": " << e.what() << endl;
            exit(2);
        }
        const tc::Table &t = m_policy->table();
        if (m_fsm->numStates() != t.numStates() || m_fsm->numEvents() != t.numEvents())
        {
            cerr << "TLC_LLC " << id << ": FSMReader reads " << m_fsm->numStates() << " states and " << m_fsm->numEvents() << " events from " << path
                 << ", the policy engine " << t.numStates() << " and " << t.numEvents() << endl;
            exit(2);
        }
        m_stall_id = t.actionId("Stall");
        m_save_id = t.actionId("SaveData");
        m_lg_block = tlc::lgOf(m_data_handler->getBlockSize());
        if (const char *e = getenv("OCTOPUS_TL_DENY_FILLS")) m_deny_left = atol(e);
        static bool registered = false;
        if (!registered) { atexit(reportAll); registered = true; }
        s_llcs.push_back(this);
    }

    TLC_LLC::~TLC_LLC()
    {
        for (auto it = s_llcs.begin(); it != s_llcs.end(); ++it) if (*it == this) { s_llcs.erase(it); break; }
    }

    void TLC_LLC::report() const
    {
        fprintf(stderr, "TLC_LLC %d: acquires %llu, grants %llu, probes %llu, releases %llu, fills %llu, replacements %llu, write-backs %llu\n", m_id,
                (unsigned long long)m_c.acquires, (unsigned long long)m_c.grants, (unsigned long long)m_c.probes, (unsigned long long)m_c.releases,
                (unsigned long long)m_c.fills, (unsigned long long)m_c.replacements, (unsigned long long)m_c.writebacks);
        if (m_deny_left >= 0)
            fprintf(stderr, "TLC_LLC %d: %llu fills taken as denied (OCTOPUS_TL_DENY_FILLS)\n", m_id, (unsigned long long)m_c.denied);
    }

    void TLC_LLC::initializeCacheStates()
    {
        m_data_handler->initializeCacheStates(m_policy->stateN());
    }

    void TLC_LLC::createDefaultCacheLine(uint64_t, GenericCacheLine *cache_line)
    {
        // a preloaded line (perfect LLC, Initializable): the LLC copy only, no inner holder (state I)
        m_data_handler->initializeCacheLine(cache_line);
        cache_line->state = m_policy->table().requireState("I");
        cache_line->valid = true;
    }

    int TLC_LLC::stateOf(uint64_t addr) const
    {
        GenericCacheLine line;
        return m_data_handler->readLineBits(addr, &line) ? line.state : m_policy->stateN();
    }

    tc::DirView TLC_LLC::view(uint64_t block, int sender) const
    {
        tc::DirView v;
        const Dir *d = dir(block);
        if (!d) return v;
        v.sender = sender >= 0 && d->owner == sender ? tc::Holder::Tip : sender >= 0 && d->sharers.count(sender) ? tc::Holder::Branch : tc::Holder::None;
        v.holders = unsigned(d->sharers.size()) + (d->owner >= 0 ? 1 : 0);
        v.probes = d->probes;
        return v;
    }

    int TLC_LLC::requesterOf(const Message &msg) const
    {
        if (msg.source == Message::Source::LOWER_INTERCONNECT && msg.tl.channel == 1) return msg.from;
        const Dir *d = dir(blockOf(msg.addr));
        return d && d->parked ? d->req.from : -1;
    }

    vector<int> TLC_LLC::probeTargets(const tc::MgrAction &a, uint64_t block, int requester) const
    {
        // TLC_Tables s3.3: the Tip; or every holder except the requester
        vector<int> t;
        const Dir *d = dir(block);
        if (!d) return t;
        if (a.op == tc::MgrOp::ProbeOwner) { if (d->owner >= 0) t.push_back(d->owner); return t; }
        if (d->owner >= 0 && d->owner != requester) t.push_back(d->owner);
        for (int s : d->sharers) if (s != requester) t.push_back(s);
        return t;
    }

    void TLC_LLC::fault(const string &why, const Message &msg, int state, int event) const
    {
        const tc::Table &t = m_policy->table();
        ostringstream o;
        o << "TLC_LLC " << m_id << ": Fault: " << why << " (line 0x" << hex << blockOf(msg.addr) << dec << ", state "
          << (state >= 0 ? t.stateName(state) : string("?")) << ", event " << (event >= 0 ? t.eventName(event) : string("?")) << ", msg " << msg.msg_id
          << ", source " << (int)msg.source << ", from " << msg.from << ", kind " << (int)msg.kind << ", tl ch " << (int)msg.tl.channel
          << " opcode " << (int)msg.tl.opcode << " param " << (int)msg.tl.param << " sink " << msg.tl.sink;
        if (const Dir *d = dir(blockOf(msg.addr)))
        {
            o << "; directory: owner " << d->owner << ", sharers {";
            for (int s : d->sharers) o << " " << s;
            o << " }, probes " << d->probes << ", parked " << d->parked << ", grant " << d->grant_out;
        }
        o << ")";
        cout << o.str() << endl;
        cerr << o.str() << endl;
        exit(2);
    }

    // TLC_Tables s3.2
    int TLC_LLC::decode(const Message &msg) const
    {
        const uint64_t block = blockOf(msg.addr);
        tc::Decoded d;
        switch (msg.source)
        {
            case Message::Source::LOWER_INTERCONNECT:      // the inner link: A, C, E from the L1s
                if (msg.tl.channel == 1) d = m_policy->decodeA(tlc::toHeader(msg), view(block, msg.from));
                else if (msg.tl.channel == 3) d = m_policy->decodeC(tlc::toHeader(msg), view(block, msg.from));
                else if (msg.tl.channel == 5)
                {
                    // the GrantAck's sink is the one the block's unanswered Grant was given
                    const Dir *e = dir(block);
                    if (!e || !e->grant_out || e->grant_tag != msg.tl.sink)
                        fault("a GrantAck whose sink answers no Grant of the block", msg, stateOf(msg.addr), -1);
                    d = m_policy->decodeE();
                }
                else fault("an inner message that is not on A, C or E (Message::tl.channel 0 = untagged)", msg, stateOf(msg.addr), -1);
                break;
            case Message::Source::UPPER_INTERCONNECT:      // the memory side: the fill (the write's AccessAck never gets here, rule 7)
            {
                tl::MsgHeader h;
                if (msg.tl.channel == 4) h = tlc::toHeader(msg);
                else if (msg.tl.channel == 0 && (msg.kind == Message::K_FILL || msg.kind == Message::K_FILL_ROLLBACK) && msg.data != NULL)
                {
                    // a memory side that does not speak TileLink (the point-to-point memory bus, a fill
                    // from the write table): AccessAckData
                    h.channel = tl::Channel::D; h.opcode = uint8_t(tl::OpD::AccessAckData); h.size = uint8_t(m_lg_block);
                }
                else fault("a memory-side message that is neither a fill nor a TileLink D", msg, stateOf(msg.addr), -1);
                // the test hook: this fill taken as denied (decided at its first decode)
                if (m_deny_left >= 0 && h.opcode == uint8_t(tl::OpD::AccessAckData) && !h.denied)
                {
                    auto key = std::make_pair(msg.msg_id, block);
                    if (!m_deny_keys.count(key) && m_deny_left > 0) { m_deny_keys.insert(key); m_deny_left--; }
                    if (m_deny_keys.count(key)) h.denied = true;
                }
                d = m_policy->decodeOuterD(h);
            }
                break;
            case Message::Source::SELF:                    // CacheController::checkReplacements' victim
                if (msg.owner != m_id) fault("a self-raised message of another controller", msg, -1, -1);
                d = m_policy->decodeReplacement();
                break;
        }
        if (!d.ok()) fault("decode: " + d.fault, msg, stateOf(msg.addr), -1);
        return d.event;
    }

    FRFCFS_State TLC_LLC::getRequestState(const Message &msg, FRFCFS_State)
    {
        return isStall(stateOf(msg.addr), decode(msg)) ? FRFCFS_State::NonReady : FRFCFS_State::Ready;
    }

    vector<ControllerAction> TLC_LLC::processRequest(Message &msg, DebugPrint *dprint)
    {
        const tc::Table &t = m_policy->table();
        const uint64_t block = blockOf(msg.addr);
        const int event = decode(msg);
        const int state = stateOf(msg.addr);
        const tc::Cell &cell = t.transition(state, event);
        const int next = cell.next;
        const bool stall = isStall(state, event);
        if (dprint)   // coherence transition hook (docs/Debugger.md)
            dprint->transition(&msg, (uint32_t)m_id, state, event, stall ? state : next, stall, this->m_fsm);

        vector<ControllerAction> pre, pending, post, out;
        auto act = [](vector<ControllerAction> &v, ControllerAction::Type type, Message *m) { v.push_back(ControllerAction{.type = type, .data = (void *)m}); };
        if (stall)
        {
            act(out, ControllerAction::Type::STALL, new Message(msg));
            return out;
        }
        for (int id : cell.actions)
            if (m_policy->action(id).op == tc::MgrOp::Fault) fault("a Fault cell of " + t.name(), msg, state, event);

        if (msg.source == Message::Source::UPPER_INTERCONNECT && m_deny_keys.erase(std::make_pair(msg.msg_id, block))) m_c.denied++;
        Dir &d = m_dir[block];
        const bool consumed_a = msg.source == Message::Source::LOWER_INTERCONNECT && msg.tl.channel == 1;
        Message req_copy;
        const bool have_req = consumed_a || d.parked;
        if (have_req) req_copy = consumed_a ? msg : d.req;
        const int requester = have_req ? req_copy.from : -1;
        const bool save = m_save_id >= 0 && t.hasAction(state, event, m_save_id);
        if (save && msg.data == NULL) fault("SaveData with no data", msg, state, event);
        const bool next_valid = t.dataValid(next);
        const uint32_t bs = m_data_handler->getBlockSize();
        if (consumed_a) m_c.acquires++;
        if (event == m_policy->eventReplacement()) m_c.replacements++;
        if (msg.source == Message::Source::UPPER_INTERCONNECT) m_c.fills++;
        bool answered = false;

        for (int id : cell.actions)
        {
            const tc::MgrAction &a = m_policy->action(id);
            switch (a.op)
            {
                case tc::MgrOp::GetOut:
                {
                    // the outer Get (contract with the memory side: A, Get, size log2(block), no data),
                    // on the request's id: the Logger's Mem-Bus and DRAM stages
                    Message *m = new Message(have_req ? req_copy.msg_id : msg.msg_id, block, 0, 0, have_req ? req_copy.owner : (uint16_t)m_id);
                    m->to.push_back((uint16_t)homeOf(block));   // the memory controller of the line
                    m->kind = Message::K_MEM_READ;
                    tlc::setHeader(*m, tc::ManagerPolicy::outerGet(block, m_lg_block));
                    act(post, ControllerAction::Type::SEND_BUS_MSG, m);
                    break;
                }
                case tc::MgrOp::PutFullDataOut:
                {
                    // rule 7: PutFullData of the line (performWriteBack addresses memory and records the
                    // write for H1); the bytes this cell saved, else the line, read before the state
                    // update when the cell frees it
                    Message *m = new Message(msg.msg_id, block, 0, 0, (uint16_t)m_id);
                    m->kind = Message::K_MEM_WRITE;
                    tlc::setHeader(*m, tc::ManagerPolicy::outerPut(block, m_lg_block));
                    if (save) m->copy(msg.data, (uint16_t)bs);
                    act(!save && !next_valid ? pre : post, ControllerAction::Type::WRITE_BACK, m);
                    m_c.writebacks++;
                    break;
                }
                case tc::MgrOp::GrantData:
                case tc::MgrOp::Grant:
                case tc::MgrOp::GrantDenied:
                {
                    if (!have_req) fault("a Grant with no Acquire", msg, state, event);
                    // S9.3.7-8: source and size from the Acquire, a new sink (the TLPort allocates it
                    // from its pool and gives this tag back on the GrantAck)
                    const uint32_t tag = ++m_next_tag;
                    Message *m = new Message(req_copy.msg_id, block, 0, tlc::REQUEST_TYPE_TL, req_copy.owner);
                    m->to.push_back((uint16_t)req_copy.from);
                    m->kind = Message::K_RESP;
                    tlc::setHeader(*m, tc::ManagerPolicy::grant(a, tlc::toHeader(req_copy), tag));
                    if (a.op == tc::MgrOp::GrantData && save) m->copy(msg.data, (uint16_t)bs);
                    act(a.op == tc::MgrOp::GrantData && !save && !next_valid ? pre : post, ControllerAction::Type::TL_RESPOND, m);
                    d.grant_out = true;
                    d.grant_tag = tag;
                    answered = true;
                    m_c.grants++;
                    break;
                }
                case tc::MgrOp::ReleaseAck:
                {
                    Message *m = new Message(msg.msg_id, block, 0, tlc::REQUEST_TYPE_TL, (uint16_t)m_id);
                    m->to.push_back(msg.from);
                    m->kind = Message::K_ACK;
                    tlc::setHeader(*m, tc::ManagerPolicy::releaseAck(tlc::toHeader(msg)));
                    act(post, ControllerAction::Type::SEND_FWD_MESSAGE, m);
                    m_c.releases++;
                    break;
                }
                case tc::MgrOp::ProbeAll:
                case tc::MgrOp::ProbeOwner:
                {
                    vector<int> targets = probeTargets(a, block, requester);
                    if (targets.empty()) fault(string(tc::mgrOpName(a.op)) + " with no holder to probe (the counter would never reach 0)", msg, state, event);
                    Message *m = new Message(have_req ? req_copy.msg_id : msg.msg_id, block, 0, tlc::REQUEST_TYPE_TL, (uint16_t)m_id);
                    for (int c : targets) m->to.push_back((uint16_t)c);
                    // TLC_Tables s8: toB to the Tip is a Fwd-GetS; toN to the Tip a Fwd-GetM when it
                    // serves a request (Acquire or uncached access), an Inv for the Replacement or a CMO;
                    // toN to the Branches an Inv
                    const bool serves = have_req && req_copy.tl.opcode != uint8_t(tl::OpA::Intent);
                    m->kind = a.op == tc::MgrOp::ProbeOwner && (a.param == uint8_t(tl::Cap::toB) || serves) ? Message::K_FWD : Message::K_INV;
                    tlc::setHeader(*m, tc::ManagerPolicy::probe(block, m_lg_block, a.param, 0));   // the TLPort sets each copy's source
                    act(post, ControllerAction::Type::SEND_FWD_MESSAGE, m);
                    d.probes = unsigned(targets.size());   // rule 4
                    m_c.probes += targets.size();
                    break;
                }
                case tc::MgrOp::SetOwner:
                    if (requester < 0) fault("SetOwner with no requester", msg, state, event);
                    d.owner = requester;
                    d.sharers.clear();
                    break;
                case tc::MgrOp::AddSharer:
                    if (requester < 0) fault("AddSharer with no requester", msg, state, event);
                    d.sharers.insert(requester);
                    break;
                case tc::MgrOp::OwnerToSharer:
                    if (d.owner >= 0) d.sharers.insert(d.owner);
                    d.owner = -1;
                    break;
                case tc::MgrOp::RemoveHolder:
                    if (d.owner == (int)msg.from) d.owner = -1;
                    d.sharers.erase((int)msg.from);
                    break;
                case tc::MgrOp::DecCount:
                    if (d.probes == 0) fault("DecCount with the probe counter at 0", msg, state, event);
                    d.probes--;
                    break;
                case tc::MgrOp::SaveData:
                    break;   // the UPDATE_CACHE_LINE below carries the bytes
                case tc::MgrOp::ForwardOut:
                case tc::MgrOp::ForwardIn:
                case tc::MgrOp::Execute:
                case tc::MgrOp::AccessAckDataIn:
                case tc::MgrOp::HintAck:
                case tc::MgrOp::HintAckDenied:
                case tc::MgrOp::Drop:
                    fault(string("action ") + tc::mgrOpName(a.op) + " serves an uncached access or a CMO, which Octopus's L1 never issues (TLC_Tables s8; tlsim exercises it)",
                          msg, state, event);
                case tc::MgrOp::Stall:
                case tc::MgrOp::Fault:
                    break;   // handled above
            }
        }
        if (event == m_policy->eventGrantAck()) d.grant_out = false;
        // rule 3: an inner-A cell that does not answer parks its request; the answering cell releases it
        if (consumed_a && !answered)
        {
            if (d.parked) fault("a second request parked on the block", msg, state, event);
            d.parked = true;
            d.req = msg;
            act(pending, ControllerAction::Type::ADD_PENDING, new Message(msg));
        }
        else if (!consumed_a && answered && d.parked)
        {
            d.parked = false;
            d.req = Message();
        }

        // the block's new state; SaveData's bytes (rule 6) when the line stays
        GenericCacheLine line;
        m_data_handler->readLineBits(msg.addr, &line);
        line.state = next;
        // A transient state keeps the line tracked even without a valid copy (N_g: a denied Grant waiting
        // for its GrantAck; N_u): an invalid line that is not in the array loses its MSHR entry, and with
        // it the state, so the block would read N and a new Acquire open a second transaction while the
        // GrantAck of the first is still due (docs/TileLink_Denied_Grant_Retry_Plan.md).
        line.valid = next_valid || !t.stable(next);
        Message bits(msg);
        if (!(save && next_valid) && bits.data != NULL) { delete[] bits.data; bits.data = NULL; bits.data_size = 0; }
        ControllerAction update;
        update.type = ControllerAction::Type::UPDATE_CACHE_LINE;
        update.data = (void *)new uint8_t[sizeof(Message) + sizeof(GenericCacheLine)];
        new (update.data) Message(bits);
        new ((uint8_t *)update.data + sizeof(Message)) GenericCacheLine(line);

        if (next == m_policy->stateN() && !d.parked && d.owner < 0 && d.sharers.empty() && d.probes == 0 && !d.grant_out)
            m_dir.erase(block);

        out = pre;
        out.push_back(update);
        out.insert(out.end(), pending.begin(), pending.end());
        out.insert(out.end(), post.begin(), post.end());
        return out;
    }
}

// ---- class annotation (docs/NoC_Architecture_Spec.md s14.3) and admission (docs/TileLink_Plan.md s5.2)
namespace octopus
{
    bool TLC_LLC::consumedBy(const string &e, MsgClass &o) const
    {
        o = MsgClass();
        if (e == "Replacement") return true;                                   // the controller itself
        o.tl_channel = 1;
        if (e == "AcquireBlock_NtoB") { o.kind = Message::K_GETS; return true; }
        if (e.compare(0, 13, "AcquireBlock_") == 0) { o.kind = Message::K_GETM; return true; }
        if (e == "Get_u" || e == "PutAtomic_u" || e.compare(0, 7, "Intent_") == 0) { o.kind = Message::K_DEMAND; return true; }   // uncached (tlsim only)
        o.tl_channel = 3;
        if (e == "ProbeAck_toN" || e == "last_ProbeAck_toN" || e == "ProbeAck_toB") { o.kind = Message::K_ACK; return true; }
        if (e == "ProbeAckData_toN" || e == "ProbeAckData_toB") { o.kind = Message::K_WB_DATA; o.data = true; return true; }
        if (e == "Release_BtoN" || e == "last_Release_BtoN") { o.kind = Message::K_PUTS; return true; }
        if (e == "Release_TtoN") { o.kind = Message::K_PUTE; return true; }
        if (e == "ReleaseData_TtoN") { o.kind = Message::K_PUTM; o.data = true; return true; }
        if (e == "GrantAck") { o.kind = Message::K_GRANT_ACK; o.tl_channel = 5; return true; }
        o.tl_channel = 4; o.memory = true;
        if (e == "AccessAckData" || e == "AccessAckData_denied") { o.kind = Message::K_FILL; o.data = true; return true; }
        if (e == "AccessAck" || e == "AccessAck_denied") { o.kind = Message::K_MEM_WACK; return true; }
        return false;
    }
    bool TLC_LLC::emittedBy(const string &a, vector<MsgClass> &o) const
    {
        o.clear(); MsgClass m; m.from_home = true;
        if (a == "Stall" || a == "Fault" || a == "SaveData" || a == "Drop" || a == "SetOwner" || a == "AddSharer" || a == "OwnerToSharer" ||
            a == "RemoveHolder" || a == "DecCount") return true;
        m.tl_channel = 1; m.memory = true;
        if (a == "Get_out" || a == "ForwardOut") { m.kind = Message::K_MEM_READ; o.push_back(m); return true; }
        if (a == "PutFullData_out") { m.kind = Message::K_MEM_WRITE; m.data = true; o.push_back(m); return true; }
        m.memory = false; m.tl_channel = 2;
        if (a == "ProbeBlock_toN_all") { m.kind = Message::K_INV; m.count = -1; o.push_back(m); return true; }   // one per holder but the requester
        if (a == "ProbeBlock_toN_owner" || a == "ProbeBlock_toB_owner") { m.kind = Message::K_FWD; o.push_back(m); return true; }
        m.tl_channel = 4;
        if (a == "GrantData_toT" || a == "GrantData_toB") { m.kind = Message::K_RESP; m.data = true; o.push_back(m); return true; }
        if (a == "Grant_toT" || a == "Grant_denied") { m.kind = Message::K_RESP; o.push_back(m); return true; }
        if (a == "ForwardIn" || a == "Execute" || a == "AccessAckData_in") { m.kind = Message::K_RESP; o.push_back(m); return true; }
        if (a == "ReleaseAck" || a == "HintAck" || a == "HintAck_denied") { m.kind = Message::K_ACK; o.push_back(m); return true; }
        return false;
    }

    // plan s5.2 point 1: the emissions of the cell the message will take now (TLC_Tables s7.2), the
    // probe fan-out resolved to the holders it will probe (the line is blocked meanwhile: the set
    // cannot grow). A Stall cell is never popped and predicts nothing.
    bool TLC_LLC::predictEmissions(const Message &msg, GenericCacheLine &, vector<MsgClass> &out)
    {
        out.clear();
        const tc::Table &t = m_policy->table();
        const int event = decode(msg), state = stateOf(msg.addr);
        if (isStall(state, event)) return true;
        const int requester = requesterOf(msg);
        for (int id : t.transition(state, event).actions)
        {
            vector<MsgClass> v;
            if (!emittedBy(t.actionName(id), v))
            {
                cerr << "TLC_LLC " << m_id << ": action " << t.actionName(id) << " of " << t.name() << " has no class annotation" << endl;
                exit(2);
            }
            for (MsgClass &c : v)
                if (c.count < 0) c.count = (int)probeTargets(m_policy->action(id), blockOf(msg.addr), requester).size();
            out.insert(out.end(), v.begin(), v.end());
        }
        return true;
    }
}
