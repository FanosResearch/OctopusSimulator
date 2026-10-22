/*
 * File  :      test_negc.cpp
 * TL-NEG for TL-C (plan §6; docs/TileLink_Negative_Tests.md §4, "the doc"): one mutant per TL-C monitor
 * rule, 31 in all, plus the doc's controls ("with ... no violation") and the Release-vs-GrantAck case
 * of docs/TileLink_TLC_Tables.md §11 item 5. Harness (doc §1.1): link L0 w=64 a=32 z=3 o=2 i=2 level=C,
 * slave max_size 6, block_bytes 64, c_ordered 1 unless the row says 0, reset_cycles 100; ScriptedMaster
 * M (A, C, E offers; B and D ready) and ScriptedSlave S (B, D offers; A, C, E ready). X = 0x1000,
 * Y = 0x2000; every Acquire, Probe, ProbeAck and Release is size 6 at the block address, one beat on
 * w = 64. Setup (doc §1.3) starts at s0 = 100: SETUP-B/T(X) is Acquire at s0, GrantData at s0 + 1,
 * GrantAck at s0 + 2 and t0 = s0 + 4; two blocks (M-ID-2) run back to back with sources 0/1 and sinks
 * 0/1 and t0 = s0 + 8; SETUP-N has t0 = s0. No watchdog (the kernel is built directly).
 *
 * The scripts complete every transaction the doc leaves open (the GrantAck after a Grant, the
 * ReleaseAck after a Release, the ProbeAck after a Probe), conformingly and at the first legal cycle
 * the doc's timing allows, so that the end-of-run rule M-RESP-7 stays quiet. An offer-time mutant on a
 * request is offered once and rejected by the receiver, as in test/neg (the static rules are checked at
 * offer, catalogue §0.2), so nothing downstream of an illegal message adds reports of its own.
 *
 * Report mode: the expected rule fires, its first report is at the documented cycle, and every other
 * rule that fires is in the mutant's `also` list (the doc's "would also hold", plus the ones found here,
 * each marked DISCREPANCY). A control (rule "") must report nothing. Fatal mode: the binary re-runs
 * itself with `--fatal <name>`; the child exits 2 and its first stderr line names `fatal_first` (a
 * control exits 0).
 */
#include "TestMain.h"
#include "ScriptedSlave.h"
#include "tilelink/Monitor.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sys/wait.h>

using namespace octopus::tl;
using namespace octopus::tl::fixture;

namespace
{
    const char *g_argv0 = nullptr;
    const uint64_t X = 0x1000, Y = 0x2000, S0 = 100;

    enum class Setup { N, B, T, BB };

    struct Neg
    {
        Kernel k;
        ScriptedMaster *M;
        ScriptedSlave *S;
        Link *l;
        std::unique_ptr<Monitor> mon;
        uint64_t t0 = S0;
        Neg(MonitorOptions::Mode mode, bool c_ordered, bool aua) : k(KernelOptions{100, 1, DataMode::None})
        {
            M = static_cast<ScriptedMaster *>(&k.add(std::unique_ptr<Agent>(new ScriptedMaster("M"))));
            S = static_cast<ScriptedSlave *>(&k.add(std::unique_ptr<Agent>(new ScriptedSlave("S"))));
            LinkParams p; p.w = 64; p.a = 32; p.z = 3; p.o = 2; p.i = 2; p.level = Level::C;
            l = &k.connect("L0", p, *M, *S);
            MonitorOptions mo;
            mo.mode = mode;
            mo.print = mode == MonitorOptions::Report;   // fatal: tl::fatal prints the line itself
            mo.max_size = 6;
            mo.block_bytes = 64;
            mo.c_ordered = c_ordered;
            mo.allow_acquire_under_acquire = aua;
            mon.reset(new Monitor(k, *l, mo));
        }
        ~Neg() { mon.reset(); }
        uint64_t at(int rel) const { return t0 + uint64_t(rel); }

        // ---- messages (one beat each on w = 64)
        MsgPtr msg(Channel ch, unsigned op, unsigned param, uint64_t src, uint64_t addr, unsigned size = 6)
        { return makeMsg(k, ch, op, size, 64, addr, src, DataMode::None, param); }
        MsgPtr d(OpD op, unsigned param, uint64_t src, uint64_t sink, bool denied = false, unsigned size = 6)
        {
            MsgPtr m = msg(Channel::D, unsigned(op), param, src, 0, size);
            m->sink = sink;
            m->denied = denied;
            if (denied) for (auto &c : m->corrupt) c = m->hasData() ? 1 : 0;   // §4.4 (M-ERR-2 stays quiet)
            return m;
        }
        // ---- the master's offers (absolute cycle) and the slave's
        void mA(uint64_t t, OpA op, unsigned param, uint64_t src, uint64_t addr, unsigned size = 6) { M->at(t, Channel::A, msg(Channel::A, unsigned(op), param, src, addr, size)); }
        void mC(uint64_t t, OpC op, unsigned param, uint64_t src, uint64_t addr) { M->at(t, Channel::C, msg(Channel::C, unsigned(op), param, src, addr)); }
        void mE(uint64_t t, uint64_t sink) { MsgPtr e = msg(Channel::E, 0, 0, 0, 0, 0); e->sink = sink; M->at(t, Channel::E, e); }
        void sB(uint64_t t, OpB op, unsigned param, uint64_t src, uint64_t addr) { S->at(t, Channel::B, msg(Channel::B, unsigned(op), param, src, addr)); }
        void sD(uint64_t t, const MsgPtr &m) { S->at(t, Channel::D, m); }

        // ---- doc §1.3
        void setup(Setup s)
        {
            if (s == Setup::N) { t0 = S0; return; }
            if (s == Setup::BB)
            {
                mA(S0, OpA::AcquireBlock, unsigned(Grow::NtoB), 0, X);
                mA(S0 + 1, OpA::AcquireBlock, unsigned(Grow::NtoB), 1, Y);
                sD(S0 + 2, d(OpD::GrantData, unsigned(Cap::toB), 0, 0));
                sD(S0 + 3, d(OpD::GrantData, unsigned(Cap::toB), 1, 1));
                mE(S0 + 4, 0); mE(S0 + 5, 1);
                t0 = S0 + 8;
                return;
            }
            const bool t = s == Setup::T;
            mA(S0, OpA::AcquireBlock, unsigned(t ? Grow::NtoT : Grow::NtoB), 0, X);
            sD(S0 + 1, d(OpD::GrantData, unsigned(t ? Cap::toT : Cap::toB), 0, 0));
            mE(S0 + 2, 0);
            t0 = S0 + 4;
        }
    };

    struct Mutant
    {
        const char *name;          // test id
        const char *rule;          // the rule the doc says fires; "" = a control (nothing fires)
        Setup setup;
        int fire;                  // relative to t0
        std::vector<std::string> also;
        const char *fatal_first;   // first report in fatal mode ("" for a control)
        std::function<void(Neg &)> build;
        bool c_ordered = true;
        bool aua = false;          // allow_acquire_under_acquire
        int run = 20;              // cycles after t0
    };

    const unsigned NtoB = unsigned(Grow::NtoB), NtoT = unsigned(Grow::NtoT), BtoT = unsigned(Grow::BtoT);
    const unsigned toT = unsigned(Cap::toT), toB = unsigned(Cap::toB), toN = unsigned(Cap::toN);
    const unsigned TtoB = unsigned(Prune::TtoB), TtoN = unsigned(Prune::TtoN), BtoN = unsigned(Prune::BtoN);
    const unsigned TtoT = unsigned(Report::TtoT), BtoB = unsigned(Report::BtoB), NtoN = unsigned(Report::NtoN);

    std::vector<Mutant> mutants()
    {
        std::vector<Mutant> v;
        auto add = [&](Mutant m) { v.push_back(m); };
        // ------------------------------------------------------------ doc §4, in the doc's row order
        add({"M-LEVEL-3", "M-LEVEL-3", Setup::N, 0, {}, "M-LEVEL-3", [](Neg &n) {
                 n.M->at(n.at(0), Channel::C, n.msg(Channel::C, 3, 0, 0, X)); n.S->notReady(Channel::C, n.at(0)); }});
        // The GrantAck that completes the Grant copies the out-of-range sink, so M-ALIGN-7 also fires on E
        // at +2 (the same rule; the first report is the Grant's at +1).
        add({"M-ALIGN-7", "M-ALIGN-7", Setup::N, 1, {}, "M-ALIGN-7", [](Neg &n) {
                 n.mA(n.at(0), OpA::AcquireBlock, NtoB, 0, X); n.sD(n.at(1), n.d(OpD::GrantData, toB, 0, 4)); n.mE(n.at(2), 4); }});
        add({"M-PARAM-4-B", "M-PARAM-4", Setup::B, 0, {}, "M-PARAM-4", [](Neg &n) {
                 n.sB(n.at(0), OpB::Intent, unsigned(Intent::CBOInval), 0, X); n.M->notReady(Channel::B, n.at(0)); }});
        add({"M-PARAM-6", "M-PARAM-6", Setup::N, 0, {}, "M-PARAM-6", [](Neg &n) {
                 n.mA(n.at(0), OpA::AcquireBlock, 3, 0, X); n.S->notReady(Channel::A, n.at(0)); }});
        add({"M-PARAM-7", "M-PARAM-7", Setup::B, 0, {}, "M-PARAM-7", [](Neg &n) {
                 n.sB(n.at(0), OpB::ProbeBlock, 3, 0, X); n.M->notReady(Channel::B, n.at(0)); }});
        add({"M-PARAM-8", "M-PARAM-8", Setup::T, 1, {"M-PERM-2"}, "M-PARAM-8", [](Neg &n) {   // doc: M-PERM-2 also holds
                 n.sB(n.at(0), OpB::ProbeBlock, toN, 0, X); n.mC(n.at(1), OpC::ProbeAckData, NtoN, 0, X); }});
        // DISCREPANCY (doc §4 M-PARAM-9): the Grant toN is accepted, and toN is below the NtoB it answers, so
        // M-PARAM-11 also holds (later in the order: at the first beat's accept). The denied variant below is
        // exempt from M-PARAM-11 and fires M-PARAM-9 alone, as the doc says.
        add({"M-PARAM-9", "M-PARAM-9", Setup::N, 1, {"M-PARAM-11"}, "M-PARAM-9", [](Neg &n) {
                 n.mA(n.at(0), OpA::AcquireBlock, NtoB, 0, X); n.sD(n.at(1), n.d(OpD::Grant, toN, 0, 0)); n.mE(n.at(2), 0); }});
        add({"M-PARAM-10", "M-PARAM-10", Setup::B, 1, {}, "M-PARAM-10", [](Neg &n) {
                 n.mC(n.at(0), OpC::Release, BtoN, 0, X); n.sD(n.at(1), n.d(OpD::ReleaseAck, 1, 0, 0)); }});
        add({"M-PARAM-11", "M-PARAM-11", Setup::N, 1, {}, "M-PARAM-11", [](Neg &n) {
                 n.mA(n.at(0), OpA::AcquireBlock, NtoT, 0, X); n.sD(n.at(1), n.d(OpD::Grant, toB, 0, 0)); n.mE(n.at(2), 0); }});
        add({"M-RSV-2", "M-RSV-2", Setup::B, 1, {}, "M-RSV-2", [](Neg &n) {
                 n.mC(n.at(0), OpC::Release, BtoN, 0, X); n.sD(n.at(1), n.d(OpD::ReleaseAck, 0, 0, 0, true)); }});
        // doc: M-ERR-3 has no report of its own; the denied Grant leaves perm N, so the Release "from T" fires M-PERM-2.
        add({"M-ERR-3", "M-PERM-2", Setup::N, 3, {}, "M-PERM-2", [](Neg &n) {
                 n.mA(n.at(0), OpA::AcquireBlock, NtoT, 0, X); n.sD(n.at(1), n.d(OpD::Grant, toT, 0, 0, true)); n.mE(n.at(2), 0);
                 n.mC(n.at(3), OpC::Release, TtoN, 0, X); n.sD(n.at(4), n.d(OpD::ReleaseAck, 0, 0, 0)); }});
        add({"M-RESP-3", "M-RESP-3", Setup::B, 0, {}, "M-RESP-3", [](Neg &n) { n.mC(n.at(0), OpC::ProbeAck, BtoB, 0, X); }});
        add({"M-RESP-4", "M-RESP-4", Setup::T, 1, {}, "M-RESP-4", [](Neg &n) {
                 n.sB(n.at(0), OpB::ProbePerm, toN, 0, X); n.mC(n.at(1), OpC::ProbeAckData, TtoN, 0, X); }});
        add({"M-RESP-5", "M-RESP-5", Setup::N, 0, {}, "M-RESP-5", [](Neg &n) { n.mE(n.at(0), 1); }});
        add({"M-ID-2", "M-ID-2", Setup::BB, 1, {}, "M-ID-2", [](Neg &n) {
                 n.mC(n.at(0), OpC::Release, BtoN, 0, X); n.mC(n.at(1), OpC::Release, BtoN, 0, Y);
                 n.sD(n.at(2), n.d(OpD::ReleaseAck, 0, 0, 0)); n.sD(n.at(3), n.d(OpD::ReleaseAck, 0, 0, 0)); }});
        // The second Probe needs its own answer (M-RESP-7 otherwise): ProbeAck NtoN at +3, the master being N by then.
        add({"M-ID-3", "M-ID-3", Setup::B, 1, {"M-CONC-9"}, "M-ID-3", [](Neg &n) {   // doc: M-CONC-9 also holds
                 n.sB(n.at(0), OpB::ProbeBlock, toN, 0, X); n.sB(n.at(1), OpB::ProbeBlock, toN, 0, X);
                 n.mC(n.at(2), OpC::ProbeAck, BtoN, 0, X); n.mC(n.at(3), OpC::ProbeAck, NtoN, 0, X); }});
        // Both Grants need their GrantAck: sink 0 at +4 (the first Grant) and again at +5 (the second).
        add({"M-ID-4", "M-ID-4", Setup::N, 3, {}, "M-ID-4", [](Neg &n) {
                 n.mA(n.at(0), OpA::AcquireBlock, NtoB, 0, X); n.mA(n.at(1), OpA::AcquireBlock, NtoB, 1, Y);
                 n.sD(n.at(2), n.d(OpD::Grant, toB, 0, 0)); n.sD(n.at(3), n.d(OpD::Grant, toB, 1, 0)); n.mE(n.at(4), 0); n.mE(n.at(5), 0); }});
        auto conc1 = [](Neg &n) {
            n.mA(n.at(0), OpA::AcquireBlock, NtoB, 0, X); n.mA(n.at(1), OpA::AcquireBlock, NtoT, 1, X);
            n.sD(n.at(4), n.d(OpD::GrantData, toB, 0, 0)); n.mE(n.at(5), 0);
            n.sD(n.at(6), n.d(OpD::GrantData, toT, 1, 1)); n.mE(n.at(7), 1); };
        add({"M-CONC-1", "M-CONC-1", Setup::N, 1, {}, "M-CONC-1", conc1});
        add({"M-CONC-2", "M-CONC-2", Setup::N, 1, {}, "M-CONC-2", [](Neg &n) {
                 n.mA(n.at(0), OpA::Get, 0, 0, X, 3); n.mA(n.at(1), OpA::AcquireBlock, NtoB, 1, X);
                 n.sD(n.at(4), n.d(OpD::AccessAckData, 0, 0, 0, false, 3)); n.sD(n.at(5), n.d(OpD::GrantData, toB, 1, 0)); n.mE(n.at(6), 0); }});
        add({"M-CONC-3", "M-CONC-3", Setup::N, 1, {}, "M-CONC-3", [](Neg &n) {
                 n.mA(n.at(0), OpA::AcquireBlock, NtoB, 0, X); n.mA(n.at(1), OpA::Get, 0, 1, X, 3);
                 n.sD(n.at(4), n.d(OpD::GrantData, toB, 0, 0)); n.mE(n.at(5), 0); n.sD(n.at(6), n.d(OpD::AccessAckData, 0, 1, 0, false, 3)); }});
        add({"M-CONC-4", "M-CONC-4", Setup::B, 1, {}, "M-CONC-4", [](Neg &n) {
                 n.mA(n.at(0), OpA::AcquireBlock, BtoT, 0, X); n.mC(n.at(1), OpC::Release, BtoN, 0, X);
                 n.sD(n.at(2), n.d(OpD::ReleaseAck, 0, 0, 0)); n.sD(n.at(4), n.d(OpD::Grant, toT, 0, 0)); n.mE(n.at(5), 0); }});
        // DISCREPANCY (doc §4 M-CONC-5): the doc's ProbeAck TtoN starts from T, but the ReleaseData TtoN of +1
        // already left the master at N, so M-PERM-2 holds as well. The timing mutant proper sends the param a
        // conforming master would (NtoN); the doc's literal param is the variant M-CONC-5-TtoN below.
        add({"M-CONC-5", "M-CONC-5", Setup::T, 2, {}, "M-CONC-5", [](Neg &n) {
                 n.sB(n.at(0), OpB::ProbeBlock, toN, 0, X); n.mC(n.at(1), OpC::ReleaseData, TtoN, 0, X);
                 n.mC(n.at(2), OpC::ProbeAck, NtoN, 0, X); n.sD(n.at(4), n.d(OpD::ReleaseAck, 0, 0, 0)); }});
        add({"M-CONC-6", "M-CONC-6", Setup::B, 1, {}, "M-CONC-6", [](Neg &n) {
                 n.mC(n.at(0), OpC::Release, BtoN, 0, X); n.mA(n.at(1), OpA::AcquireBlock, NtoB, 0, X);
                 n.sD(n.at(4), n.d(OpD::ReleaseAck, 0, 0, 0)); n.sD(n.at(5), n.d(OpD::GrantData, toB, 0, 0)); n.mE(n.at(6), 0); }});
        add({"M-CONC-7", "M-CONC-7", Setup::B, 2, {}, "M-CONC-7", [](Neg &n) {
                 n.mA(n.at(0), OpA::AcquireBlock, BtoT, 0, X); n.sB(n.at(1), OpB::ProbeBlock, toN, 0, X);
                 n.sD(n.at(2), n.d(OpD::Grant, toT, 0, 0)); n.mE(n.at(3), 0); n.mC(n.at(5), OpC::ProbeAck, TtoN, 0, X); }});
        add({"M-CONC-8", "M-CONC-8", Setup::N, 2, {}, "M-CONC-8", [](Neg &n) {
                 n.mA(n.at(0), OpA::AcquireBlock, NtoB, 0, X); n.sD(n.at(1), n.d(OpD::GrantData, toB, 0, 0));
                 n.sB(n.at(2), OpB::ProbeBlock, toN, 0, X); n.mE(n.at(4), 0); n.mC(n.at(5), OpC::ProbeAck, BtoN, 0, X); }});
        add({"M-CONC-9", "M-CONC-9", Setup::T, 1, {}, "M-CONC-9", [](Neg &n) {
                 n.sB(n.at(0), OpB::ProbeBlock, toB, 0, X); n.sB(n.at(1), OpB::ProbeBlock, toN, 1, X);
                 n.mC(n.at(2), OpC::ProbeAckData, TtoB, 0, X); n.mC(n.at(3), OpC::ProbeAck, BtoN, 0, X); }});
        auto conc10 = [](OpB probe) { return [probe](Neg &n) {
            n.mA(n.at(0), OpA::Get, 0, 0, X, 3); n.sB(n.at(1), probe, toN, 0, X);
            n.sD(n.at(2), n.d(OpD::AccessAckData, 0, 0, 0, false, 3)); n.mC(n.at(4), OpC::ProbeAck, NtoN, 0, X); }; };
        add({"M-CONC-10", "M-CONC-10", Setup::N, 2, {}, "M-CONC-10", conc10(OpB::ProbeBlock)});
        auto conc11 = [](Neg &n) { n.sB(n.at(0), OpB::ProbeBlock, toT, 0, X); n.mC(n.at(1), OpC::ProbeAckData, TtoT, 0, X); };
        { Mutant m{"M-CONC-11", "M-CONC-11", Setup::T, 1, {}, "M-CONC-11", conc11}; m.c_ordered = false; add(m); }
        add({"M-PERM-1", "M-PERM-1", Setup::N, 0, {}, "M-PERM-1", [](Neg &n) {
                 n.mA(n.at(0), OpA::AcquireBlock, BtoT, 0, X); n.sD(n.at(1), n.d(OpD::Grant, toT, 0, 0)); n.mE(n.at(2), 0); }});
        add({"M-PERM-2", "M-PERM-2", Setup::B, 0, {}, "M-PERM-2", [](Neg &n) {
                 n.mC(n.at(0), OpC::Release, TtoN, 0, X); n.sD(n.at(1), n.d(OpD::ReleaseAck, 0, 0, 0)); }});
        add({"M-PERM-3", "M-PERM-3", Setup::T, 1, {}, "M-PERM-3", [](Neg &n) {
                 n.sB(n.at(0), OpB::ProbeBlock, toN, 0, X); n.mC(n.at(1), OpC::ProbeAck, TtoB, 0, X); }});

        // ------------------------------------------------------------ the doc's variants and controls
        add({"M-PARAM-9-denied", "M-PARAM-9", Setup::N, 1, {}, "M-PARAM-9", [](Neg &n) {   // doc: "with denied=1 also fires"
                 n.mA(n.at(0), OpA::AcquireBlock, NtoB, 0, X); n.sD(n.at(1), n.d(OpD::Grant, toN, 0, 0, true)); n.mE(n.at(2), 0); }});
        add({"M-PARAM-11-denied", "", Setup::N, 0, {}, "", [](Neg &n) {                      // doc: "with denied=1 no violation"
                 n.mA(n.at(0), OpA::AcquireBlock, NtoT, 0, X); n.sD(n.at(1), n.d(OpD::Grant, toB, 0, 0, true)); n.mE(n.at(2), 0); }});
        { Mutant m{"M-CONC-1-allowed", "", Setup::N, 0, {}, "", conc1}; m.aua = true; add(m); }   // doc: allow_acquire_under_acquire=1
        add({"M-CONC-5-TtoN", "M-CONC-5", Setup::T, 2, {"M-PERM-2"}, "M-CONC-5", [](Neg &n) {   // the doc's literal param
                 n.sB(n.at(0), OpB::ProbeBlock, toN, 0, X); n.mC(n.at(1), OpC::ReleaseData, TtoN, 0, X);
                 n.mC(n.at(2), OpC::ProbeAck, TtoN, 0, X); n.sD(n.at(4), n.d(OpD::ReleaseAck, 0, 0, 0)); }});
        add({"M-CONC-5-conforming", "", Setup::T, 0, {}, "", [](Neg &n) {                    // doc: ProbeAck held until +5
                 n.sB(n.at(0), OpB::ProbeBlock, toN, 0, X); n.mC(n.at(1), OpC::ReleaseData, TtoN, 0, X);
                 n.sD(n.at(4), n.d(OpD::ReleaseAck, 0, 0, 0)); n.mC(n.at(5), OpC::ProbeAck, NtoN, 0, X); }});
        add({"M-CONC-10-ProbePerm", "", Setup::N, 0, {}, "", conc10(OpB::ProbePerm)});       // doc: ProbePerm, no violation
        add({"M-CONC-11-ordered", "", Setup::T, 0, {}, "", conc11});                          // doc: c_ordered=1, no violation
        add({"M-PERM-1-from-B", "M-PERM-1", Setup::B, 0, {}, "M-PERM-1", [](Neg &n) {        // doc: SETUP-B then NtoB
                 n.mA(n.at(0), OpA::AcquireBlock, NtoB, 0, X); n.sD(n.at(1), n.d(OpD::GrantData, toB, 0, 0)); n.mE(n.at(2), 0); }});
        // §9.2 lists a pending AccessAck[Data] (Get, Put, Atomic) as blocking an Acquire or a Release; a
        // pending HintAck (Intent: prefetch or CMO) is not in the list. Monitor change (M-CONC-2, M-CONC-4).
        add({"M-CONC-2-intent", "", Setup::N, 0, {}, "", [](Neg &n) {
                 n.mA(n.at(0), OpA::Intent, unsigned(Intent::CBOFlush), 0, X); n.mA(n.at(1), OpA::AcquireBlock, NtoB, 1, X);
                 n.sD(n.at(4), n.d(OpD::HintAck, 0, 0, 0)); n.sD(n.at(5), n.d(OpD::GrantData, toB, 1, 0)); n.mE(n.at(6), 0); }});
        add({"M-CONC-4-intent", "", Setup::B, 0, {}, "", [](Neg &n) {
                 n.mA(n.at(0), OpA::Intent, unsigned(Intent::PrefetchWrite), 0, X); n.mC(n.at(1), OpC::Release, BtoN, 0, X);
                 n.sD(n.at(2), n.d(OpD::ReleaseAck, 0, 0, 0)); n.sD(n.at(4), n.d(OpD::HintAck, 0, 0, 0)); }});

        // ------------------------------------------------------------ Release vs GrantAck (TLC_Tables §11 item 5)
        // C and E are unordered, so a master's Release may reach the manager before its own GrantAck.
        // On the wire nothing forbids it: §9.2 Release forbids a Release while a Grant is *pending* (the
        // master has not received it), and once the Grant is received the Acquire is complete for the master
        // ("until it receives a Grant or GrantData"); the GrantAck is a response the master owes, which no
        // §9.2 sentence orders against a Release; the only rule keyed on the GrantAck binds the slave's
        // Probes (M-CONC-8). The permission the Release starts from is the Grant's (catalogue §12: applied
        // when the Grant is accepted, not at the GrantAck). Legal:
        auto rgLegal = [](int gack_at, int relack_at) { return [gack_at, relack_at](Neg &n) {
            n.mA(n.at(0), OpA::AcquireBlock, NtoT, 0, X); n.sD(n.at(1), n.d(OpD::GrantData, toT, 0, 0));
            MsgPtr e = n.msg(Channel::E, 0, 0, 0, 0, 0); e->sink = 0;
            for (int t = 2; t <= gack_at; t++) { n.M->at(n.at(t), Channel::E, e); if (t < gack_at) n.S->notReady(Channel::E, n.at(t)); }
            n.mC(n.at(2), OpC::ReleaseData, TtoN, 0, X);
            n.sD(n.at(relack_at), n.d(OpD::ReleaseAck, 0, 0, 0)); }; };
        add({"RG-release-before-grantack", "", Setup::N, 0, {}, "", rgLegal(4, 5)});    // GrantAck held (e_ready LOW) to +4
        add({"RG-release-with-grantack", "", Setup::N, 0, {}, "", rgLegal(2, 3)});      // both accepted at +2
        add({"RG-releaseack-before-grantack", "", Setup::N, 0, {}, "", rgLegal(4, 3)}); // the manager answers the Release first
        // Illegal 1: the Release issued in the cycle the Grant is received (accepted together): the Grant is
        // still pending (catalogue §0.3: through the cycle of its last beat), so §9.2 Release forbids it.
        add({"RG-release-with-grant", "M-CONC-4", Setup::B, 2, {}, "M-CONC-4", [](Neg &n) {
                 n.mA(n.at(0), OpA::AcquireBlock, BtoT, 0, X); n.sD(n.at(2), n.d(OpD::Grant, toT, 0, 0));
                 n.mC(n.at(2), OpC::Release, BtoN, 0, X); n.mE(n.at(3), 0); n.sD(n.at(3), n.d(OpD::ReleaseAck, 0, 0, 0)); }});
        // Illegal 2: the Release overtakes the GrantAck but starts from the permission before the Grant
        // (BtoN after a Grant toT): a master that only "commits" the Grant with its GrantAck.
        add({"RG-release-from-old-perm", "M-PERM-2", Setup::B, 2, {}, "M-PERM-2", [](Neg &n) {
                 n.mA(n.at(0), OpA::AcquireBlock, BtoT, 0, X); n.sD(n.at(1), n.d(OpD::Grant, toT, 0, 0));
                 MsgPtr e = n.msg(Channel::E, 0, 0, 0, 0, 0); e->sink = 0;
                 for (int t = 2; t <= 4; t++) { n.M->at(n.at(t), Channel::E, e); if (t < 4) n.S->notReady(Channel::E, n.at(t)); }
                 n.mC(n.at(2), OpC::Release, BtoN, 0, X); n.sD(n.at(5), n.d(OpD::ReleaseAck, 0, 0, 0)); }});
        return v;
    }

    const Mutant *find(const std::string &name)
    {
        static std::vector<Mutant> all = mutants();
        for (auto &m : all) if (name == m.name) return &m;
        return nullptr;
    }

    std::vector<Violation> run(const Mutant &mu, MonitorOptions::Mode mode)
    {
        Neg n(mode, mu.c_ordered, mu.aua);
        n.setup(mu.setup);
        mu.build(n);
        n.k.run(n.t0 + uint64_t(mu.run));
        n.mon->finish(n.k.cycle());
        return n.mon->log();
    }

    int g_fired = 0, g_fatal_ok = 0, g_mutants = 0;

    bool checkMutant(const Mutant &mu)
    {
        bool control = mu.rule[0] == 0;
        std::vector<Violation> log = run(mu, MonitorOptions::Report);
        bool ok;
        uint64_t expect_cycle = 0;
        if (control) ok = log.empty();
        else
        {
            expect_cycle = S0 + uint64_t(mu.fire) + (mu.setup == Setup::N ? 0 : mu.setup == Setup::BB ? 8 : 4);
            const Violation *first = nullptr;
            std::set<std::string> fired;
            for (auto &v : log) { fired.insert(v.rule); if (!first && v.rule == mu.rule) first = &v; }
            ok = first && first->cycle == expect_cycle;
            for (auto &r : fired)
                if (r != mu.rule && std::find(mu.also.begin(), mu.also.end(), r) == mu.also.end()) ok = false;
        }
        CHECK(ok);
        if (!ok)
        {
            std::printf("  %s: expected %s at cycle %llu, fired:", mu.name, control ? "nothing" : mu.rule, (unsigned long long)expect_cycle);
            for (auto &v : log) std::printf(" %s@%llu", v.rule.c_str(), (unsigned long long)v.cycle);
            std::printf("\n");
        }
        // fatal mode: the child exits 2 with the first rule reported (a control: exits 0)
        std::string err = std::string("negc_") + mu.name + ".stderr";
        std::string cmd = std::string("\"") + g_argv0 + "\" --fatal " + mu.name + " >/dev/null 2>" + err;
        int st = std::system(cmd.c_str());
        int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        std::ifstream in(err);
        std::string line;
        std::getline(in, line);
        bool fok;
        if (control) fok = code == 0 && line.empty();
        else
        {
            std::string want = std::string("TileLink: ") + mu.fatal_first + " ";
            fok = code == 2 && line.compare(0, want.size(), want) == 0;
        }
        CHECK(fok);
        if (!fok) std::printf("  %s fatal: exit %d, first line: %s\n", mu.name, code, line.c_str());
        return ok && fok;
    }
}

TEST(tl_c_mutants)
{
    // doc §4: the 31 TL-C mutants (every entry before the variants).
    int n = 0, pass = 0;
    for (auto &mu : mutants())
    {
        if (std::string(mu.name) == "M-PARAM-9-denied") break;
        n++;
        if (checkMutant(mu)) pass++;
    }
    CHECK_EQ(n, 31);
    g_mutants = n; g_fired = pass;
    std::printf("  %d/%d TL-C mutants: the expected rule in report mode and exit 2 with it in fatal mode\n", pass, n);
}

TEST(tl_c_variants_and_controls)
{
    int n = 0, pass = 0;
    bool started = false;
    for (auto &mu : mutants())
    {
        if (std::string(mu.name) == "M-PARAM-9-denied") started = true;
        if (!started || std::string(mu.name).compare(0, 3, "RG-") == 0) continue;
        n++;
        if (checkMutant(mu)) pass++;
    }
    std::printf("  %d/%d variants and controls as the doc says\n", pass, n);
}

TEST(release_vs_grantack)
{
    // docs/TileLink_TLC_Tables.md §11 item 5: the legal overtaking orders report nothing; the two illegal
    // forms report M-CONC-4 and M-PERM-2.
    int n = 0, pass = 0;
    for (auto &mu : mutants())
    {
        if (std::string(mu.name).compare(0, 3, "RG-") != 0) continue;
        n++;
        if (checkMutant(mu)) pass++;
    }
    CHECK_EQ(n, 5);
    std::printf("  %d/%d Release-vs-GrantAck cases\n", pass, n);
    g_fatal_ok = pass;
}

int main(int argc, char **argv)
{
    g_argv0 = argv[0];
    if (argc >= 3 && std::string(argv[1]) == "--fatal")
    {
        const Mutant *mu = find(argv[2]);
        if (!mu) { std::fprintf(stderr, "unknown mutant %s\n", argv[2]); return 1; }
        run(*mu, MonitorOptions::Fatal);
        return 0;   // no violation: the parent sees 0, not 2
    }
    int failed_cases = 0;
    for (auto &c : tltest::cases())
    {
        int before = tltest::failures();
        std::printf("[TL-NEG-C] %s\n", c.name);
        c.body();
        if (tltest::failures() != before) failed_cases++;
    }
    std::printf("TL-NEG-C: %zu cases, %d checks, %d failed\n", tltest::cases().size(), tltest::checks(), failed_cases);
    return failed_cases ? 1 : 0;
}
