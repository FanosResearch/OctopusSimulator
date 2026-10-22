/*
 * File  :      test_policy.cpp
 * The policy engine (plan §3.7) without a kernel: the decode tables of docs/TileLink_TLC_Tables.md
 * §2.2 (L1) and §3.2 (manager, with the param-class collapsing and the probe counter's last_ event),
 * the T6 deferral of the directory-consistency faults while Probes are outstanding, and the builders
 * (§2.3, §3.3, §6): every message every cell of the three tables emits is legal on its channel
 * (paramLegal), and the copied fields follow §7 (ProbeAck copies the Probe's source and address,
 * GrantAck the Grant's sink, a denied Grant carries the requested cap).
 */
#include "TestMain.h"
#include "tilelink/coh/Policy.h"
#include <sstream>

using namespace octopus::tl;
using namespace octopus::tl::coh;

namespace
{
    std::string path(const char *n) { return std::string(TL_TABLE_DIR) + "/" + n; }
    MsgHeader hdr(Channel ch, unsigned op, unsigned param = 0, bool denied = false)
    {
        MsgHeader h; h.channel = ch; h.opcode = uint8_t(op); h.param = uint8_t(param); h.denied = denied; h.size = 6; h.address = 0x40;
        return h;
    }
    std::string ev(const Table &t, const Decoded &d) { return d.ok() ? t.eventName(d.event) : "FAULT"; }
    DirView dv(Holder s, unsigned holders, unsigned probes) { DirView d; d.sender = s; d.holders = holders; d.probes = probes; return d; }
}

TEST(l1_decode)
{
    L1Policy p(Table::load(path("TLC_MESI_L1.csv")));
    const Table &t = p.table();
    CHECK_EQ(ev(t, p.decode(hdr(Channel::B, unsigned(OpB::ProbeBlock), unsigned(Cap::toN)))), std::string("ProbeBlock_toN"));
    CHECK_EQ(ev(t, p.decode(hdr(Channel::B, unsigned(OpB::ProbeBlock), unsigned(Cap::toB)))), std::string("ProbeBlock_toB"));
    CHECK_EQ(ev(t, p.decode(hdr(Channel::B, unsigned(OpB::ProbeBlock), unsigned(Cap::toT)))), std::string("ProbeBlock_toT"));
    CHECK_EQ(ev(t, p.decode(hdr(Channel::B, unsigned(OpB::ProbePerm), unsigned(Cap::toN)))), std::string("FAULT"));
    CHECK_EQ(ev(t, p.decode(hdr(Channel::B, unsigned(OpB::Get)))), std::string("FAULT"));
    CHECK_EQ(ev(t, p.decode(hdr(Channel::D, unsigned(OpD::Grant), unsigned(Cap::toT)))), std::string("Grant_toT"));
    CHECK_EQ(ev(t, p.decode(hdr(Channel::D, unsigned(OpD::Grant), unsigned(Cap::toB)))), std::string("Grant_toB"));
    CHECK_EQ(ev(t, p.decode(hdr(Channel::D, unsigned(OpD::GrantData), unsigned(Cap::toB)))), std::string("GrantData_toB"));
    CHECK_EQ(ev(t, p.decode(hdr(Channel::D, unsigned(OpD::GrantData), unsigned(Cap::toT)))), std::string("GrantData_toT"));
    CHECK_EQ(ev(t, p.decode(hdr(Channel::D, unsigned(OpD::GrantData), unsigned(Cap::toN)))), std::string("FAULT"));   // plan §8 decision 7
    CHECK_EQ(ev(t, p.decode(hdr(Channel::D, unsigned(OpD::GrantData), unsigned(Cap::toN), true))), std::string("Grant_denied"));   // param ignored
    CHECK_EQ(ev(t, p.decode(hdr(Channel::D, unsigned(OpD::Grant), unsigned(Cap::toT), true))), std::string("Grant_denied"));
    CHECK_EQ(ev(t, p.decode(hdr(Channel::D, unsigned(OpD::ReleaseAck)))), std::string("ReleaseAck"));
    CHECK_EQ(ev(t, p.decode(hdr(Channel::D, unsigned(OpD::AccessAckData)))), std::string("FAULT"));
    CHECK_EQ(t.eventName(p.decodeCpu(false).event), std::string("Load"));
    CHECK_EQ(t.eventName(p.decodeCpu(true).event), std::string("Store"));
    CHECK_EQ(t.eventName(p.decodeReplacement().event), std::string("Replacement"));
}

TEST(manager_decode_a)
{
    ManagerPolicy p(Table::load(path("TLC_MESI_LLC.csv")));
    const Table &t = p.table();
    auto A = [&](unsigned op, unsigned param, DirView d) { return ev(t, p.decodeA(hdr(Channel::A, op, param), d)); };
    const unsigned acq = unsigned(OpA::AcquireBlock);
    CHECK_EQ(A(acq, unsigned(Grow::NtoB), dv(Holder::None, 0, 0)), std::string("AcquireBlock_NtoB"));
    CHECK_EQ(A(acq, unsigned(Grow::NtoT), dv(Holder::None, 2, 0)), std::string("AcquireBlock_NtoT"));
    CHECK_EQ(A(acq, unsigned(Grow::NtoB), dv(Holder::Branch, 2, 0)), std::string("FAULT"));   // §3.2: from a listed holder
    CHECK_EQ(A(acq, unsigned(Grow::NtoT), dv(Holder::Tip, 1, 0)), std::string("FAULT"));
    // T6: the sender's ProbeAck is still in flight (probes > 0): decoded, the probing state Stalls it
    CHECK_EQ(A(acq, unsigned(Grow::NtoB), dv(Holder::Branch, 2, 2)), std::string("AcquireBlock_NtoB"));
    CHECK_EQ(A(acq, unsigned(Grow::BtoT), dv(Holder::Branch, 3, 0)), std::string("AcquireBlock_BtoT"));
    CHECK_EQ(A(acq, unsigned(Grow::BtoT), dv(Holder::Branch, 1, 0)), std::string("AcquireBlock_BtoT_last"));
    CHECK_EQ(A(acq, unsigned(Grow::BtoT), dv(Holder::None, 1, 0)), std::string("AcquireBlock_BtoT_nonholder"));
    CHECK_EQ(A(acq, unsigned(Grow::BtoT), dv(Holder::Tip, 1, 0)), std::string("FAULT"));
    CHECK_EQ(A(acq, unsigned(Grow::BtoT), dv(Holder::Tip, 1, 1)), std::string("AcquireBlock_BtoT_nonholder"));
    CHECK_EQ(A(unsigned(OpA::AcquirePerm), unsigned(Grow::NtoT), dv(Holder::None, 0, 0)), std::string("FAULT"));
    CHECK_EQ(A(unsigned(OpA::Get), 0, dv(Holder::None, 0, 0)), std::string("Get_u"));
    for (unsigned op : {unsigned(OpA::PutFullData), unsigned(OpA::PutPartialData), unsigned(OpA::ArithmeticData), unsigned(OpA::LogicalData)})
        CHECK_EQ(A(op, 0, dv(Holder::None, 0, 0)), std::string("PutAtomic_u"));
    CHECK_EQ(A(unsigned(OpA::Intent), 0, dv(Holder::None, 0, 0)), std::string("Intent_Prefetch"));
    CHECK_EQ(A(unsigned(OpA::Intent), 1, dv(Holder::None, 0, 0)), std::string("Intent_Prefetch"));
    CHECK_EQ(A(unsigned(OpA::Intent), 5, dv(Holder::None, 0, 0)), std::string("Intent_CBOInval"));
    CHECK_EQ(A(unsigned(OpA::Intent), 6, dv(Holder::None, 0, 0)), std::string("Intent_CBOClean"));
    CHECK_EQ(A(unsigned(OpA::Intent), 7, dv(Holder::None, 0, 0)), std::string("Intent_CBOFlush"));
    CHECK_EQ(A(unsigned(OpA::Intent), 3, dv(Holder::None, 0, 0)), std::string("FAULT"));
}

TEST(manager_decode_c_e_d)
{
    ManagerPolicy p(Table::load(path("TLC_MESI_LLC.csv")));
    const Table &t = p.table();
    auto C = [&](unsigned op, unsigned param, DirView d) { return ev(t, p.decodeC(hdr(Channel::C, op, param), d)); };
    const unsigned pa = unsigned(OpC::ProbeAck), pad = unsigned(OpC::ProbeAckData), rel = unsigned(OpC::Release), reld = unsigned(OpC::ReleaseData);
    // §3.2 param classes: TtoN, BtoN, NtoN -> toN; last_ when the counter is 1 (§1.2 rule 4)
    CHECK_EQ(C(pa, unsigned(Prune::BtoN), dv(Holder::Branch, 3, 2)), std::string("ProbeAck_toN"));
    CHECK_EQ(C(pa, unsigned(Prune::BtoN), dv(Holder::Branch, 3, 1)), std::string("last_ProbeAck_toN"));
    CHECK_EQ(C(pa, unsigned(Prune::TtoN), dv(Holder::Tip, 1, 1)), std::string("last_ProbeAck_toN"));
    CHECK_EQ(C(pa, unsigned(Report::NtoN), dv(Holder::None, 0, 1)), std::string("last_ProbeAck_toN"));   // after a merged Release (Figure 30)
    CHECK_EQ(C(pa, unsigned(Prune::BtoN), dv(Holder::None, 1, 1)), std::string("FAULT"));               // not listed and not NtoN
    CHECK_EQ(C(pa, unsigned(Prune::TtoB), dv(Holder::Tip, 1, 1)), std::string("ProbeAck_toB"));
    CHECK_EQ(C(pa, unsigned(Prune::TtoB), dv(Holder::Tip, 1, 2)), std::string("FAULT"));
    CHECK_EQ(C(pa, unsigned(Report::TtoT), dv(Holder::Tip, 1, 1)), std::string("FAULT"));
    CHECK_EQ(C(pa, unsigned(Report::BtoB), dv(Holder::Branch, 1, 1)), std::string("FAULT"));
    CHECK_EQ(C(pad, unsigned(Prune::TtoN), dv(Holder::Tip, 1, 1)), std::string("ProbeAckData_toN"));
    CHECK_EQ(C(pad, unsigned(Prune::TtoB), dv(Holder::Tip, 1, 1)), std::string("ProbeAckData_toB"));
    CHECK_EQ(C(pad, unsigned(Prune::TtoN), dv(Holder::Tip, 1, 2)), std::string("FAULT"));
    CHECK_EQ(C(pad, unsigned(Prune::BtoN), dv(Holder::Branch, 1, 1)), std::string("FAULT"));
    CHECK_EQ(C(rel, unsigned(Prune::BtoN), dv(Holder::Branch, 2, 0)), std::string("Release_BtoN"));
    CHECK_EQ(C(rel, unsigned(Prune::BtoN), dv(Holder::Branch, 1, 0)), std::string("last_Release_BtoN"));
    CHECK_EQ(C(rel, unsigned(Prune::TtoN), dv(Holder::Tip, 1, 0)), std::string("Release_TtoN"));
    CHECK_EQ(C(rel, unsigned(Prune::TtoN), dv(Holder::Branch, 1, 0)), std::string("FAULT"));
    CHECK_EQ(C(rel, unsigned(Prune::TtoB), dv(Holder::Tip, 1, 0)), std::string("FAULT"));
    CHECK_EQ(C(reld, unsigned(Prune::TtoN), dv(Holder::Tip, 1, 0)), std::string("ReleaseData_TtoN"));
    CHECK_EQ(C(reld, unsigned(Prune::TtoN), dv(Holder::None, 0, 0)), std::string("FAULT"));
    CHECK_EQ(C(unsigned(OpC::AccessAck), 0, dv(Holder::None, 0, 0)), std::string("FAULT"));
    CHECK_EQ(t.eventName(p.decodeE().event), std::string("GrantAck"));
    CHECK_EQ(ev(t, p.decodeOuterD(hdr(Channel::D, unsigned(OpD::AccessAckData)))), std::string("AccessAckData"));
    CHECK_EQ(ev(t, p.decodeOuterD(hdr(Channel::D, unsigned(OpD::AccessAckData), 0, true))), std::string("AccessAckData_denied"));
    CHECK_EQ(ev(t, p.decodeOuterD(hdr(Channel::D, unsigned(OpD::AccessAck)))), std::string("AccessAck"));
    CHECK_EQ(ev(t, p.decodeOuterD(hdr(Channel::D, unsigned(OpD::AccessAck), 0, true))), std::string("AccessAck_denied"));
    CHECK_EQ(ev(t, p.decodeOuterD(hdr(Channel::D, unsigned(OpD::HintAck)))), std::string("FAULT"));
}

TEST(builders_copy_fields)
{
    L1Policy p(Table::load(path("TLC_MESI_L1.csv")));
    const Table &t = p.table();
    MsgHeader probe = ManagerPolicy::probe(0x1c0, 6, uint8_t(Cap::toB), 12);
    CHECK(probe.channel == Channel::B && probe.opcode == unsigned(OpB::ProbeBlock) && probe.param == unsigned(Cap::toB) && probe.source == 12 && probe.size == 6);
    MsgHeader out;
    CHECK(p.build(t.requireAction("ProbeAckData_TtoB"), 0x1c0, 6, &probe, 99, out));
    CHECK(out.channel == Channel::C && out.opcode == unsigned(OpC::ProbeAckData) && out.param == unsigned(Prune::TtoB));
    CHECK_EQ(out.source, uint64_t(12));     // §9.3.6: copied from b_source, not the allocated one
    CHECK_EQ(out.address, uint64_t(0x1c0));
    MsgHeader acq;
    CHECK(p.build(t.requireAction("AcquireBlock_BtoT"), 0x80, 6, nullptr, 3, acq));
    CHECK(acq.channel == Channel::A && acq.opcode == unsigned(OpA::AcquireBlock) && acq.param == unsigned(Grow::BtoT) && acq.source == 3 && acq.address == 0x80);
    MgrAction denied; denied.op = MgrOp::GrantDenied;
    MsgHeader g = ManagerPolicy::grant(denied, acq, 5);
    CHECK(g.denied && g.opcode == unsigned(OpD::Grant) && g.param == unsigned(Cap::toT) && g.sink == 5 && g.source == 3);   // §1.2 rule 8
    acq.param = uint8_t(Grow::NtoB);
    CHECK_EQ(unsigned(ManagerPolicy::grant(denied, acq, 5).param), unsigned(Cap::toB));
    CHECK(p.build(t.requireAction("GrantAck"), 0x80, 6, &g, 0, out));
    CHECK(out.channel == Channel::E && out.sink == 5);
    MsgHeader ra = ManagerPolicy::releaseAck(probe);
    CHECK(ra.channel == Channel::D && ra.opcode == unsigned(OpD::ReleaseAck) && ra.param == 0 && !ra.denied);
    MsgHeader put = hdr(Channel::A, unsigned(OpA::PutPartialData)), intent = hdr(Channel::A, unsigned(OpA::Intent), 6), get = hdr(Channel::A, unsigned(OpA::Get));
    CHECK_EQ(unsigned(ManagerPolicy::accessAck(put, false).opcode), unsigned(OpD::AccessAck));
    CHECK_EQ(unsigned(ManagerPolicy::accessAck(get, false).opcode), unsigned(OpD::AccessAckData));
    CHECK_EQ(unsigned(ManagerPolicy::accessAck(intent, true).opcode), unsigned(OpD::HintAck));
}

TEST(every_emission_is_legal)
{
    // §6: whatever a cell emits carries a param legal for its message (paramLegal: Grow, Cap,
    // Prune/Report without NtoN on data/Release, Grant never toN, reserved 0).
    for (const char *f : {"TLC_MESI_L1.csv"})
    {
        L1Policy p(Table::load(path(f)));
        const Table &t = p.table();
        MsgHeader probe = ManagerPolicy::probe(0, 6, uint8_t(Cap::toN), 0), grant = hdr(Channel::D, unsigned(OpD::GrantData));
        unsigned n = 0;
        for (int a = 0; a < t.numActions(); a++)
        {
            MsgHeader out;
            if (!p.build(a, 0x40, 6, p.action(a).op == L1Op::GrantAck ? &grant : &probe, 1, out)) continue;
            n++;
            if (!paramLegal(out.channel, out.opcode, out.param)) std::printf("    %s: illegal param\n", t.actionName(a).c_str());
            CHECK(paramLegal(out.channel, out.opcode, out.param));
            CHECK(legalOn(Level::C, out.channel, out.opcode));
        }
        CHECK_EQ(n, 15u);
    }
    for (const char *f : {"TLC_MESI_LLC.csv", "TLC_MSI_LLC.csv"})
    {
        ManagerPolicy p(Table::load(path(f)));
        const Table &t = p.table();
        MsgHeader acq = hdr(Channel::A, unsigned(OpA::AcquireBlock), unsigned(Grow::NtoT));
        for (int a = 0; a < t.numActions(); a++)
        {
            const MgrAction &x = p.action(a);
            MsgHeader out;
            if (x.op == MgrOp::GrantData || x.op == MgrOp::Grant || x.op == MgrOp::GrantDenied) out = ManagerPolicy::grant(x, acq, 1);
            else if (x.op == MgrOp::ProbeAll || x.op == MgrOp::ProbeOwner) out = ManagerPolicy::probe(0, 6, x.param, 0);
            else continue;
            CHECK(paramLegal(out.channel, out.opcode, out.param));
        }
    }
}

TEST(unknown_action_refused)
{
    // an action the engine has no implementation of is refused when the policy is built
    std::istringstream in("EventNum,Event\n0,Load\n1,Store\n2,Replacement\n3,ProbeBlock_toN\n4,ProbeBlock_toB\n5,ProbeBlock_toT\n6,Grant_toB\n7,Grant_toT\n"
                          "8,GrantData_toB\n9,GrantData_toT\n10,Grant_denied\n11,ReleaseAck\n,\nActionNum,Action\n0,Teleport\n,\nStateNum,State\n0,I\n,\n"
                          "State,stable,isDataValid,Load,Store,Replacement,ProbeBlock_toN,ProbeBlock_toB,ProbeBlock_toT,Grant_toB,Grant_toT,GrantData_toB,GrantData_toT,Grant_denied,ReleaseAck\n"
                          "I,1,0,Teleport/\n");
    Table t = Table::parse(in, "x");
    bool threw = false;
    try { L1Policy p(t); } catch (ConfigError &e) { threw = std::string(e.what()).find("Teleport") != std::string::npos; }
    CHECK(threw);
}

TL_TEST_MAIN("TL-COH-ENGINE policy")
