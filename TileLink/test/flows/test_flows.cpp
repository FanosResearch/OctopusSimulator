/*
 * File  :      test_flows.cpp
 * TL-FLOWS (plan §6): the spec's Figure 29 (two Acquires on one block, a Probe before the Grant; the
 * Grant to a master probed after its Acquire carries data; GrantAck serializes) and Figure 30 (a
 * voluntary Release merged with an ongoing Probe; the Release is acknowledged, the master answers the
 * Probe only after the ReleaseAck, the Grant waits for the counted ProbeAck), each as the message
 * sequence chart of docs/TileLink_TLC_Tables.md §5.1 / §5.2, with the timing forced by closing one
 * channel of one link for a window. Every chart line is asserted in order, as are the Grant type, the
 * values carried, the final states, and 0 Monitor / CoherenceCheck violations.
 */
#include "FlowTest.h"

using namespace octopus::tl;
using flow::Rig;

static const uint64_t X = 0x1c0;

// Brings the manager to I on X with no inner copy: c0 reads X (N -> T, c0 in E), then evicts it.
static void toI(Rig &r)
{
    r.op(0, CpuOp::Load, X, 110);
    r.op(0, CpuOp::Evict, X, 150);
    r.runTo(250);
    CHECK_EQ(r.mgr->blockState(X), std::string("I"));
    CHECK_EQ(r.cms[0]->lineState(X), std::string("I"));
}

TEST(figure29_two_acquires_probe_before_grant)
{
    Rig r;
    r.build(2);
    toI(r);
    size_t m = r.mark();
    // A = c0: Load X at 300, its A channel held until 330 ("delayed in the network").
    // B = c1: Store X at 301; its GrantAck held until 340, so that A's Acquire meets T_g (step 5).
    r.close(0, Channel::A, 300, 330);
    r.close(1, Channel::E, 300, 340);
    r.op(0, CpuOp::Load, X, 300);
    r.op(1, CpuOp::Store, X, 301);
    r.settle();
    CHECK(r.sequence({
        "c0 I + Load -> IS_a",
        "c1 I + Store -> IM_a",
        "llc I + AcquireBlock_NtoT -> T_g",                 // B's Acquire arrived first
        "c1 IM_a + GrantData_toT -> M",
        "llc T_g + AcquireBlock_NtoB -> Stall",              // blocked until B's GrantAck (step 5)
        "llc T_g + GrantAck -> T",
        "llc T + AcquireBlock_NtoB -> TB_p",                // A's parked Acquire re-decoded (steps 6, 7)
        "c1 M + ProbeBlock_toB -> S",                        // ProbeAckData TtoB
        "llc TB_p + ProbeAckData_toB -> B_g",
        "c0 IS_a + GrantData_toB -> S",                      // data after the probe
        "llc B_g + GrantAck -> B"}, m));
    CHECK_EQ(r.mgr->blockState(X), std::string("B"));
    CHECK_EQ(r.mgr->sharers(X).size(), size_t(2));
    CHECK_EQ(r.cms[0]->lineState(X), std::string("S"));
    CHECK_EQ(r.cms[1]->lineState(X), std::string("S"));
    // A reads what B wrote: owner -> manager (ProbeAckData) -> requester (GrantData), four hops
    CHECK_EQ(r.value(0, 2), r.value(1, 0));
    CHECK(r.value(1, 0) != 0);
    CHECK_EQ(r.violations(), uint64_t(0));
}

TEST(figure29_upgrade_pruned_requester_gets_data)
{
    Rig r;
    r.build(2);
    // both Branches on X: c0 reads (E), c1 reads (c0 probed toB: B{c0, c1})
    r.op(0, CpuOp::Load, X, 110);
    r.op(1, CpuOp::Load, X, 160);
    r.runTo(260);
    CHECK_EQ(r.mgr->blockState(X), std::string("B"));
    size_t m = r.mark();
    r.close(0, Channel::A, 300, 330);   // A's BtoT delayed: B's arrives first
    r.close(1, Channel::E, 300, 345);   // B's GrantAck delayed: A's BtoT meets T_g
    r.op(0, CpuOp::Store, X, 300);
    r.op(1, CpuOp::Store, X, 301);
    r.settle();
    CHECK(r.sequence({
        "c0 S + Store -> SM_a",
        "c1 S + Store -> SM_a",
        "llc B + AcquireBlock_BtoT -> BT_pu",               // probe A only
        "c0 SM_a + ProbeBlock_toN -> IM_a",                  // ProbeAck BtoN although its Grant is pending
        "llc BT_pu + last_ProbeAck_toN -> T_g",             // Grant toT without data: B was never probed
        "c1 SM_a + Grant_toT -> M",
        "llc T_g + AcquireBlock_BtoT_nonholder -> Stall",   // A no longer listed (TLC_Tables §5.1 writes BtoT)
        "llc T_g + GrantAck -> T",
        "llc T + AcquireBlock_BtoT_nonholder -> TT_p",
        "c1 M + ProbeBlock_toN -> I",                        // ProbeAckData TtoN
        "llc TT_p + ProbeAckData_toN -> T_g",               // GrantData: step 8
        "c0 IM_a + GrantData_toT -> M",
        "llc T_g + GrantAck -> T"}, m));
    CHECK_EQ(r.cms[0]->lineState(X), std::string("M"));
    CHECK_EQ(r.cms[1]->lineState(X), std::string("I"));
    // c0's Store went on top of the data that came with the GrantData (c1's store), so B's value is gone from no copy
    CHECK_EQ(r.violations(), uint64_t(0));
}

TEST(figure30_release_meets_probe)
{
    Rig r;
    r.build(2);
    r.op(1, CpuOp::Store, X, 110);       // B dirty on X: llc T{c1}, c1 M
    r.runTo(200);
    CHECK_EQ(r.cms[1]->lineState(X), std::string("M"));
    size_t m = r.mark();
    r.close(1, Channel::C, 300, 340);    // B's ReleaseData reaches the manager after A's Acquire
    r.op(0, CpuOp::Load, X, 300);
    r.op(1, CpuOp::Evict, X, 300);       // at the same time
    r.op(0, CpuOp::Load, X, 600);
    r.settle();
    CHECK(r.sequence({
        "c0 I + Load -> IS_a",
        "c1 M + Replacement -> MI_r",
        "llc T + AcquireBlock_NtoB -> TB_p",
        "c1 MI_r + ProbeBlock_toB -> Stall",                 // held: no ProbeAck before the ReleaseAck
        "llc TB_p + ReleaseData_TtoN -> TB_p",              // accepted, acknowledged, still waiting
        "c1 MI_r + ReleaseAck -> I",
        "c1 I + ProbeBlock_toB -> I",                        // ProbeAck NtoN
        "llc TB_p + last_ProbeAck_toN -> B_g",              // the counted ProbeAck
        "c0 IS_a + GrantData_toB -> S",
        "llc B_g + GrantAck -> B"}, m));
    // the Release's data answered the requester (§9.2)
    CHECK_EQ(r.value(0, 0), r.value(1, 0));
    CHECK_EQ(r.value(0, 1), r.value(1, 0));
    CHECK_EQ(r.mgr->sharers(X).size(), size_t(1));
    CHECK_EQ(r.violations(), uint64_t(0));
}

TEST(release_overtakes_own_grantack)
{
    // TLC_Tables §11.5: C and E are not ordered; a Release that reaches the manager before the GrantAck
    // of the same master is parked in T_g (the per-channel queues let the E message through).
    Rig r;
    r.build(1);
    r.close(0, Channel::E, 100, 200);
    r.op(0, CpuOp::Load, X, 110);
    r.op(0, CpuOp::Evict, X, 111);
    r.settle();
    CHECK(r.sequence({"llc NT_d + AccessAckData -> T_g", "c0 IS_a + GrantData_toT -> E", "c0 E + Replacement -> EI_r",
                      "llc T_g + Release_TtoN -> Stall", "llc T_g + GrantAck -> T", "llc T + Release_TtoN -> I", "c0 EI_r + ReleaseAck -> I"}));
    CHECK_EQ(r.violations(), uint64_t(0));
}

TL_TEST_MAIN("TL-FLOWS")
