/*
 * File  :      Policy.cpp
 * The TL-C policy engine: decoders and message builders (docs/TileLink_Plan.md §3.7;
 * docs/TileLink_TLC_Tables.md §2.2-§2.3, §3.2-§3.3, §6).
 */
#include "tilelink/coh/Policy.h"
#include <cstdio>

namespace octopus { namespace tl { namespace coh
{
    static const char *capName(unsigned c) { static const char *n[3] = {"toT", "toB", "toN"}; return c < 3 ? n[c] : "?"; }
    static const char *pruneReportName(unsigned p)
    {
        static const char *n[6] = {"TtoB", "TtoN", "BtoN", "TtoT", "BtoB", "NtoN"};
        return p < 6 ? n[p] : "?";
    }
    static bool parsePruneReport(const std::string &s, uint8_t &out)
    {
        for (unsigned p = 0; p < 6; p++) if (s == pruneReportName(p)) { out = uint8_t(p); return true; }
        return false;
    }
    static bool parseGrow(const std::string &s, uint8_t &out)
    {
        static const char *n[3] = {"NtoB", "NtoT", "BtoT"};
        for (unsigned p = 0; p < 3; p++) if (s == n[p]) { out = uint8_t(p); return true; }
        return false;
    }
    static bool startsWith(const std::string &s, const char *p, std::string &rest)
    {
        size_t n = std::string(p).size();
        if (s.compare(0, n, p) != 0) return false;
        rest = s.substr(n);
        return true;
    }

    std::string Transition::line() const
    {
        char b[32];
        std::snprintf(b, sizeof b, "0x%llx", (unsigned long long)block);
        std::string s = std::to_string(cycle) + " " + agent + " " + b + " " + from + " + " + event + " -> " + (cell.empty() ? "(empty)" : cell) + " -> " + to;
        if (client >= 0) s += " client=" + std::to_string(client);
        if (!msg.empty()) s += " [" + msg + "]";
        return s;
    }

    const char *l1OpName(L1Op op)
    {
        switch (op)
        {
            case L1Op::Stall: return "Stall"; case L1Op::Hit: return "Hit"; case L1Op::Fault: return "Fault";
            case L1Op::Error: return "Error"; case L1Op::AcquireBlock: return "AcquireBlock"; case L1Op::GrantAck: return "GrantAck";
            case L1Op::ProbeAck: return "ProbeAck"; case L1Op::ProbeAckData: return "ProbeAckData";
            case L1Op::Release: return "Release"; case L1Op::ReleaseData: return "ReleaseData";
        }
        return "?";
    }

    // ================================================================== L1
    L1Policy::L1Policy(const Table &t) : m_t(t)
    {
        m_ev_load = t.requireEvent("Load");
        m_ev_store = t.requireEvent("Store");
        m_ev_repl = t.requireEvent("Replacement");
        m_ev_probe[0] = t.requireEvent("ProbeBlock_toT");
        m_ev_probe[1] = t.requireEvent("ProbeBlock_toB");
        m_ev_probe[2] = t.requireEvent("ProbeBlock_toN");
        m_ev_grant[0] = t.requireEvent("Grant_toT");
        m_ev_grant[1] = t.requireEvent("Grant_toB");
        m_ev_grantdata[0] = t.requireEvent("GrantData_toT");
        m_ev_grantdata[1] = t.requireEvent("GrantData_toB");
        m_ev_denied = t.requireEvent("Grant_denied");
        m_ev_relack = t.requireEvent("ReleaseAck");
        m_st_I = t.requireState("I");
        // TLC_Tables §2.3: every action the table names, classified once.
        for (int id = 0; id < t.numActions(); id++)
        {
            const std::string &n = t.actionName(id);
            L1Action a;
            std::string rest;
            if (n == "Stall") a.op = L1Op::Stall;
            else if (n == "Hit") a.op = L1Op::Hit;
            else if (n == "Fault") a.op = L1Op::Fault;
            else if (n == "Error") a.op = L1Op::Error;
            else if (n == "GrantAck") a.op = L1Op::GrantAck;
            else if (startsWith(n, "AcquireBlock_", rest) && parseGrow(rest, a.param)) a.op = L1Op::AcquireBlock;
            else if (startsWith(n, "ProbeAckData_", rest) && parsePruneReport(rest, a.param)) a.op = L1Op::ProbeAckData;
            else if (startsWith(n, "ProbeAck_", rest) && parsePruneReport(rest, a.param)) a.op = L1Op::ProbeAck;
            else if (startsWith(n, "ReleaseData_", rest) && parsePruneReport(rest, a.param)) a.op = L1Op::ReleaseData;
            else if (startsWith(n, "Release_", rest) && parsePruneReport(rest, a.param)) a.op = L1Op::Release;
            else throw ConfigError("FSM table " + t.name() + ": the L1 policy engine has no implementation of action " + n);
            // §9.2 / plan §1.3: NtoN is illegal in ProbeAckData, Release and ReleaseData (M-PARAM-8);
            // refuse a table that would emit it rather than emit an illegal message.
            if ((a.op == L1Op::ProbeAckData || a.op == L1Op::Release || a.op == L1Op::ReleaseData) && a.param == unsigned(Report::NtoN))
                throw ConfigError("FSM table " + t.name() + ": action " + n + " would emit NtoN, illegal on that message (§9.3.6, §9.3.10, §9.3.11)");
            m_actions.push_back(a);
        }
    }

    Decoded L1Policy::decode(const MsgHeader &m) const
    {
        Decoded d;
        if (m.channel == Channel::B)
        {
            // TLC_Tables §2.2: ProbeBlock by Cap. ProbePerm and the B-channel Access/Hint messages
            // (update-based policies, plan §2 out of scope) are decode faults.
            if (m.opcode != unsigned(OpB::ProbeBlock))
            { d.fault = std::string("B ") + opcodeName(Channel::B, m.opcode) + " is not decoded by the L1 (TLC_Tables §2.2, §9)"; return d; }
            if (m.param > 2) { d.fault = "ProbeBlock param " + std::to_string(m.param) + " is not a Cap (§9.3.3)"; return d; }
            d.event = m_ev_probe[m.param];
            return d;
        }
        if (m.channel == Channel::D)
        {
            if (m.opcode == unsigned(OpD::Grant) || m.opcode == unsigned(OpD::GrantData))
            {
                // §2.2: denied first (param ignored, §9.3.7); then toN is a decode fault (plan §8 decision 7).
                if (m.denied) { d.event = m_ev_denied; return d; }
                if (m.param > 1)
                { d.fault = std::string(opcodeName(Channel::D, m.opcode)) + " " + capName(m.param) + " (plan §8 decision 7: never toN)"; return d; }
                d.event = m.opcode == unsigned(OpD::Grant) ? m_ev_grant[m.param] : m_ev_grantdata[m.param];
                return d;
            }
            if (m.opcode == unsigned(OpD::ReleaseAck)) { d.event = m_ev_relack; return d; }
            d.fault = std::string("D ") + opcodeName(Channel::D, m.opcode) + ": the L1 issues no uncached access (TLC_Tables §2.2)";
            return d;
        }
        d.fault = std::string("the L1 receives no message on channel ") + channelName(m.channel);
        return d;
    }

    bool L1Policy::build(int id, uint64_t block, unsigned lg_block, const MsgHeader *trigger, uint64_t source, MsgHeader &out) const
    {
        const L1Action &a = action(id);
        out = MsgHeader();
        out.size = uint8_t(lg_block);
        out.address = block;
        switch (a.op)
        {
            case L1Op::AcquireBlock:   // §9.3.1, Grow
                out.channel = Channel::A; out.opcode = uint8_t(OpA::AcquireBlock); out.param = a.param; out.source = source;
                return true;
            case L1Op::GrantAck:       // §9.3.9: e_sink copied from d_sink
                out.channel = Channel::E; out.opcode = uint8_t(OpE::GrantAck); out.size = 0; out.address = 0;
                out.sink = trigger ? trigger->sink : 0;
                return true;
            case L1Op::ProbeAck:       // §9.3.5: c_source copied from b_source, c_address = b_address, c_size = b_size
            case L1Op::ProbeAckData:   // §9.3.6
                out.channel = Channel::C;
                out.opcode = uint8_t(a.op == L1Op::ProbeAck ? OpC::ProbeAck : OpC::ProbeAckData);
                out.param = a.param;
                if (trigger) { out.source = trigger->source; out.address = trigger->address; out.size = trigger->size; }
                return true;
            case L1Op::Release:        // §9.3.10, own C namespace source
            case L1Op::ReleaseData:    // §9.3.11
                out.channel = Channel::C;
                out.opcode = uint8_t(a.op == L1Op::Release ? OpC::Release : OpC::ReleaseData);
                out.param = a.param; out.source = source;
                return true;
            default:
                return false;
        }
    }

    // ================================================================== manager
    const char *mgrOpName(MgrOp op)
    {
        switch (op)
        {
            case MgrOp::Stall: return "Stall"; case MgrOp::Fault: return "Fault"; case MgrOp::GetOut: return "Get_out";
            case MgrOp::PutFullDataOut: return "PutFullData_out"; case MgrOp::ForwardOut: return "ForwardOut";
            case MgrOp::ForwardIn: return "ForwardIn"; case MgrOp::SaveData: return "SaveData"; case MgrOp::Drop: return "Drop";
            case MgrOp::GrantData: return "GrantData"; case MgrOp::Grant: return "Grant"; case MgrOp::GrantDenied: return "Grant_denied";
            case MgrOp::ReleaseAck: return "ReleaseAck"; case MgrOp::ProbeAll: return "ProbeBlock_toN_all";
            case MgrOp::ProbeOwner: return "ProbeBlock_owner"; case MgrOp::SetOwner: return "SetOwner"; case MgrOp::AddSharer: return "AddSharer";
            case MgrOp::OwnerToSharer: return "OwnerToSharer"; case MgrOp::RemoveHolder: return "RemoveHolder"; case MgrOp::DecCount: return "DecCount";
            case MgrOp::Execute: return "Execute"; case MgrOp::AccessAckDataIn: return "AccessAckData_in"; case MgrOp::HintAck: return "HintAck";
            case MgrOp::HintAckDenied: return "HintAck_denied";
        }
        return "?";
    }

    bool answers(MgrOp op)
    {
        return op == MgrOp::GrantData || op == MgrOp::Grant || op == MgrOp::GrantDenied || op == MgrOp::AccessAckDataIn ||
               op == MgrOp::Execute || op == MgrOp::ForwardIn || op == MgrOp::HintAck || op == MgrOp::HintAckDenied;
    }

    ManagerPolicy::ManagerPolicy(const Table &t) : m_t(t)
    {
        m_st_N = t.requireState("N");
        m_ev_ntob = t.requireEvent("AcquireBlock_NtoB");
        m_ev_ntot = t.requireEvent("AcquireBlock_NtoT");
        m_ev_btot = t.requireEvent("AcquireBlock_BtoT");
        m_ev_btot_last = t.requireEvent("AcquireBlock_BtoT_last");
        m_ev_btot_non = t.requireEvent("AcquireBlock_BtoT_nonholder");
        m_ev_pa_n = t.requireEvent("ProbeAck_toN");
        m_ev_last_pa_n = t.requireEvent("last_ProbeAck_toN");
        m_ev_pad_n = t.requireEvent("ProbeAckData_toN");
        m_ev_pa_b = t.requireEvent("ProbeAck_toB");
        m_ev_pad_b = t.requireEvent("ProbeAckData_toB");
        m_ev_gack = t.requireEvent("GrantAck");
        m_ev_rel_b = t.requireEvent("Release_BtoN");
        m_ev_last_rel_b = t.requireEvent("last_Release_BtoN");
        m_ev_rel_t = t.requireEvent("Release_TtoN");
        m_ev_reld_t = t.requireEvent("ReleaseData_TtoN");
        m_ev_repl = t.requireEvent("Replacement");
        m_ev_aad = t.requireEvent("AccessAckData");
        m_ev_aad_den = t.requireEvent("AccessAckData_denied");
        m_ev_aa = t.requireEvent("AccessAck");
        m_ev_aa_den = t.requireEvent("AccessAck_denied");
        m_ev_get = t.requireEvent("Get_u");
        m_ev_put = t.requireEvent("PutAtomic_u");
        m_ev_prefetch = t.requireEvent("Intent_Prefetch");
        m_ev_inval = t.requireEvent("Intent_CBOInval");
        m_ev_clean = t.requireEvent("Intent_CBOClean");
        m_ev_flush = t.requireEvent("Intent_CBOFlush");
        // TLC_Tables §3.3: every action, classified once.
        for (int id = 0; id < t.numActions(); id++)
        {
            const std::string &n = t.actionName(id);
            MgrAction a;
            if (n == "Stall") a.op = MgrOp::Stall;
            else if (n == "Fault") a.op = MgrOp::Fault;
            else if (n == "Get_out") a.op = MgrOp::GetOut;
            else if (n == "PutFullData_out") a.op = MgrOp::PutFullDataOut;
            else if (n == "ForwardOut") a.op = MgrOp::ForwardOut;
            else if (n == "ForwardIn") a.op = MgrOp::ForwardIn;
            else if (n == "SaveData") a.op = MgrOp::SaveData;
            else if (n == "Drop") a.op = MgrOp::Drop;
            else if (n == "GrantData_toT") { a.op = MgrOp::GrantData; a.param = uint8_t(Cap::toT); }
            else if (n == "GrantData_toB") { a.op = MgrOp::GrantData; a.param = uint8_t(Cap::toB); }
            else if (n == "Grant_toT") { a.op = MgrOp::Grant; a.param = uint8_t(Cap::toT); }
            else if (n == "Grant_toB") { a.op = MgrOp::Grant; a.param = uint8_t(Cap::toB); }
            else if (n == "Grant_denied") a.op = MgrOp::GrantDenied;
            else if (n == "ReleaseAck") a.op = MgrOp::ReleaseAck;
            else if (n == "ProbeBlock_toN_all") { a.op = MgrOp::ProbeAll; a.param = uint8_t(Cap::toN); }
            else if (n == "ProbeBlock_toN_owner") { a.op = MgrOp::ProbeOwner; a.param = uint8_t(Cap::toN); }
            else if (n == "ProbeBlock_toB_owner") { a.op = MgrOp::ProbeOwner; a.param = uint8_t(Cap::toB); }
            else if (n == "SetOwner") a.op = MgrOp::SetOwner;
            else if (n == "AddSharer") a.op = MgrOp::AddSharer;
            else if (n == "OwnerToSharer") a.op = MgrOp::OwnerToSharer;
            else if (n == "RemoveHolder") a.op = MgrOp::RemoveHolder;
            else if (n == "DecCount") a.op = MgrOp::DecCount;
            else if (n == "Execute") a.op = MgrOp::Execute;
            else if (n == "AccessAckData_in") a.op = MgrOp::AccessAckDataIn;
            else if (n == "HintAck") a.op = MgrOp::HintAck;
            else if (n == "HintAck_denied") a.op = MgrOp::HintAckDenied;
            else throw ConfigError("FSM table " + t.name() + ": the manager policy engine has no implementation of action " + n);
            m_actions.push_back(a);
        }
    }

    uint8_t ManagerPolicy::requestedCap(const MsgHeader &acq)
    {
        // TLC_Tables §1.2 rule 8: NtoB asks for toB; NtoT and BtoT ask for toT.
        return acq.param == unsigned(Grow::NtoB) ? uint8_t(Cap::toB) : uint8_t(Cap::toT);
    }

    Decoded ManagerPolicy::decodeA(const MsgHeader &m, const DirView &d) const
    {
        Decoded r;
        switch (m.opcode)
        {
            case unsigned(OpA::AcquireBlock):
                if (m.param == unsigned(Grow::NtoB) || m.param == unsigned(Grow::NtoT))
                {
                    int ev = m.param == unsigned(Grow::NtoB) ? m_ev_ntob : m_ev_ntot;
                    // TLC_Tables §3.2: "From a listed holder it is a decode fault (M-PERM: NtoB starts
                    // from N)". Engine rule (T6): while Probes are outstanding on the block the sender
                    // may still be listed because its ProbeAck (C) has not arrived yet although its
                    // later Acquire (A) has: A and C are not ordered (§9.2), a Throttle reorders
                    // them. Every probing state Stalls inner A, so the event decodes as the param
                    // names it and is re-decoded once the ProbeAck removed the sender.
                    if (d.sender != Holder::None && d.probes == 0)
                    { r.fault = std::string("AcquireBlock ") + (ev == m_ev_ntob ? "NtoB" : "NtoT") + " from a listed " +
                                (d.sender == Holder::Tip ? "Tip" : "Branch") + " (TLC_Tables §3.2, M-PERM)"; return r; }
                    r.event = ev;
                    return r;
                }
                if (m.param == unsigned(Grow::BtoT))
                {
                    // §3.2: the requester a listed Branch with other holders -> BtoT; the only listed
                    // holder -> BtoT_last; not listed (pruned by a Probe after its Acquire,
                    // Figure 29) -> BtoT_nonholder.
                    if (d.sender == Holder::Branch) { r.event = d.holders > 1 ? m_ev_btot : m_ev_btot_last; return r; }
                    if (d.sender == Holder::None) { r.event = m_ev_btot_non; return r; }
                    // A BtoT from the listed Tip (TLC_Tables §3.4 T + BtoT: Fault, M-PERM). Deferred
                    // like NtoB above: a Tip probed toB answers TtoB, then upgrades; its BtoT may
                    // overtake that ProbeAck. Parked as nonholder and re-decoded after the ProbeAck.
                    if (d.probes > 0) { r.event = m_ev_btot_non; return r; }
                    r.fault = "AcquireBlock BtoT from the listed Tip (TLC_Tables §3.4, M-PERM)";
                    return r;
                }
                r.fault = "AcquireBlock param " + std::to_string(m.param) + " is not a Grow (§9.3.1)";
                return r;
            case unsigned(OpA::AcquirePerm):
                r.fault = "AcquirePerm is not supported by the TLC tables (TLC_Tables §9, plan §9)";
                return r;
            case unsigned(OpA::Get):
                r.event = m_ev_get;
                return r;
            case unsigned(OpA::PutFullData): case unsigned(OpA::PutPartialData):
            case unsigned(OpA::ArithmeticData): case unsigned(OpA::LogicalData):
                r.event = m_ev_put;
                return r;
            case unsigned(OpA::Intent):
                switch (m.param)
                {
                    case unsigned(Intent::PrefetchRead): case unsigned(Intent::PrefetchWrite): r.event = m_ev_prefetch; return r;
                    case unsigned(Intent::CBOInval): r.event = m_ev_inval; return r;
                    case unsigned(Intent::CBOClean): r.event = m_ev_clean; return r;
                    case unsigned(Intent::CBOFlush): r.event = m_ev_flush; return r;
                }
                r.fault = "Intent param " + std::to_string(m.param) + " is unassigned (Table 27; TLC_Tables §6)";
                return r;
        }
        r.fault = "A opcode " + std::to_string(m.opcode) + " unknown";
        return r;
    }

    Decoded ManagerPolicy::decodeC(const MsgHeader &m, const DirView &d) const
    {
        Decoded r;
        const std::string pn = pruneReportName(m.param);
        switch (m.opcode)
        {
            case unsigned(OpC::ProbeAck):
                if (m.param == unsigned(Prune::TtoN) || m.param == unsigned(Prune::BtoN) || m.param == unsigned(Report::NtoN))
                {
                    // §3.2: "any ProbeAck from a master that is not listed and not NtoN" is a fault.
                    if (d.sender == Holder::None && m.param != unsigned(Report::NtoN))
                    { r.fault = "ProbeAck " + pn + " from a master the directory does not list (TLC_Tables §3.2)"; return r; }
                    // §1.2 rule 4: the last_ event when the counter is 1 before the decrement.
                    r.event = d.probes == 1 ? m_ev_last_pa_n : m_ev_pa_n;
                    return r;
                }
                if (m.param == unsigned(Prune::TtoB))
                {
                    if (d.sender == Holder::None) { r.fault = "ProbeAck TtoB from a master the directory does not list (TLC_Tables §3.2)"; return r; }
                    if (d.probes != 1) { r.fault = "ProbeAck TtoB with probe counter " + std::to_string(d.probes) + " (must be 1: only the Tip is probed toB, TLC_Tables §3.2)"; return r; }
                    r.event = m_ev_pa_b;
                    return r;
                }
                r.fault = "ProbeAck " + pn + " (the manager never probes toT and probes only a Tip toB, TLC_Tables §3.2)";
                return r;
            case unsigned(OpC::ProbeAckData):
                if (m.param == unsigned(Prune::TtoN) || m.param == unsigned(Prune::TtoB))
                {
                    if (d.sender == Holder::None) { r.fault = "ProbeAckData " + pn + " from a master the directory does not list (TLC_Tables §3.2)"; return r; }
                    if (d.probes != 1) { r.fault = "ProbeAckData " + pn + " with probe counter " + std::to_string(d.probes) + " (must be 1: data comes only from the single probed Tip, TLC_Tables §3.2)"; return r; }
                    r.event = m.param == unsigned(Prune::TtoN) ? m_ev_pad_n : m_ev_pad_b;
                    return r;
                }
                r.fault = "ProbeAckData " + pn + " (no dirty Branch, Table 29; TtoT never used, §9.2; TLC_Tables §3.2)";
                return r;
            case unsigned(OpC::Release):
                if (m.param == unsigned(Prune::BtoN) && d.sender == Holder::Branch) { r.event = d.holders > 1 ? m_ev_rel_b : m_ev_last_rel_b; return r; }
                if (m.param == unsigned(Prune::TtoN) && d.sender == Holder::Tip) { r.event = m_ev_rel_t; return r; }
                r.fault = "Release " + pn + " from a master listed as " + (d.sender == Holder::Tip ? "Tip" : d.sender == Holder::Branch ? "Branch" : "nothing") +
                          " (TLC_Tables §3.2: no voluntary downgrade, M-PERM)";
                return r;
            case unsigned(OpC::ReleaseData):
                if (m.param == unsigned(Prune::TtoN) && d.sender == Holder::Tip) { r.event = m_ev_reld_t; return r; }
                r.fault = "ReleaseData " + pn + " from a master listed as " + (d.sender == Holder::Tip ? "Tip" : d.sender == Holder::Branch ? "Branch" : "nothing") +
                          " (TLC_Tables §3.2)";
                return r;
        }
        r.fault = std::string("C ") + opcodeName(Channel::C, m.opcode) + " is not decoded by the manager (update-based policies, plan §2)";
        return r;
    }

    Decoded ManagerPolicy::decodeOuterD(const MsgHeader &m) const
    {
        Decoded r;
        if (m.opcode == unsigned(OpD::AccessAckData)) { r.event = m.denied ? m_ev_aad_den : m_ev_aad; return r; }
        if (m.opcode == unsigned(OpD::AccessAck)) { r.event = m.denied ? m_ev_aa_den : m_ev_aa; return r; }
        r.fault = std::string("outer D ") + opcodeName(Channel::D, m.opcode) + " answers nothing the manager sends (TLC_Tables §3.2)";
        return r;
    }

    MsgHeader ManagerPolicy::probe(uint64_t block, unsigned lg_block, uint8_t cap, uint64_t source)
    {
        // §9.3.3 ProbeBlock: b_source = the targeted master, b_address = the block, b_size = the block.
        MsgHeader h;
        h.channel = Channel::B; h.opcode = uint8_t(OpB::ProbeBlock); h.param = cap;
        h.size = uint8_t(lg_block); h.address = block; h.source = source;
        return h;
    }

    MsgHeader ManagerPolicy::grant(const MgrAction &a, const MsgHeader &acq, uint64_t sink)
    {
        // §9.3.7, §9.3.8: d_source = a_source, d_size = a_size, a new d_sink (unique among the
        // unanswered Grants, §6.4); TLC_Tables §6: Grant toT, GrantData toB/toT, denied with the
        // requested cap (§1.2 rule 8), never toN (plan §8 decision 7).
        MsgHeader h;
        h.channel = Channel::D;
        h.source = acq.source; h.size = acq.size; h.address = acq.address; h.sink = sink;
        if (a.op == MgrOp::GrantDenied) { h.opcode = uint8_t(OpD::Grant); h.param = requestedCap(acq); h.denied = true; }
        else { h.opcode = uint8_t(a.op == MgrOp::GrantData ? OpD::GrantData : OpD::Grant); h.param = a.param; }
        return h;
    }

    MsgHeader ManagerPolicy::releaseAck(const MsgHeader &rel)
    {
        // §9.3.12: d_source copied from c_source, d_size from c_size, param and denied reserved 0.
        MsgHeader h;
        h.channel = Channel::D; h.opcode = uint8_t(OpD::ReleaseAck);
        h.source = rel.source; h.size = rel.size; h.address = rel.address;
        return h;
    }

    MsgHeader ManagerPolicy::accessAck(const MsgHeader &req, bool denied)
    {
        MsgHeader h;
        h.channel = Channel::D;
        if (req.opcode == unsigned(OpA::PutFullData) || req.opcode == unsigned(OpA::PutPartialData)) h.opcode = uint8_t(OpD::AccessAck);
        else if (req.opcode == unsigned(OpA::Intent)) h.opcode = uint8_t(OpD::HintAck);        // §8.2.4
        else h.opcode = uint8_t(OpD::AccessAckData);                                             // Get, atomics
        h.source = req.source; h.size = req.size; h.address = req.address; h.denied = denied;
        return h;
    }

    MsgHeader ManagerPolicy::outerGet(uint64_t block, unsigned lg_block)
    {
        MsgHeader h;
        h.channel = Channel::A; h.opcode = uint8_t(OpA::Get); h.size = uint8_t(lg_block); h.address = block;
        return h;
    }

    MsgHeader ManagerPolicy::outerPut(uint64_t block, unsigned lg_block)
    {
        MsgHeader h;
        h.channel = Channel::A; h.opcode = uint8_t(OpA::PutFullData); h.size = uint8_t(lg_block); h.address = block;
        return h;
    }
}}}
