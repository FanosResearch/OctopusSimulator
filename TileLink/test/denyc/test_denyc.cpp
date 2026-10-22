/*
 * File  :      test_denyc.cpp
 * TL-DENY, TL-C part (plan §6). The manager's fill (outer Get) denied: NT_d + AccessAckData_denied ->
 * Grant_denied/N_g (TLC_Tables §3.4); the L1 answers IS_a/IM_a + Grant_denied -> Error/GrantAck/I.
 *   - every denied Grant is answered by a GrantAck (one E message per Grant on every master link);
 *   - the master's permission is unchanged (M-PERM exemption, M-ERR-3: the Monitor stays clean; the
 *     L1 line stays I; CoherenceCheck sees no permission);
 *   - the CPU access completes with an access error, and no other access is lost;
 *   - an uncached master's denied Get/Put/Atomic are forwarded back denied (N_u + *_denied ->
 *     ForwardIn), every beat of a denied data response corrupt (§4.4).
 * Behind the manager (an ErrorSlave: everything denied) and behind an outer Xbar (half the address
 * space on a RamSlave, half on an ErrorSlave: only that half denied, the rest coherent and clean).
 */
#include "TestMain.h"
#include "../fixtures/TlcSystem.h"

using namespace octopus::tl;

namespace
{
    struct Count { uint64_t grants = 0, gacks = 0, denied = 0; };

    void attachCount(tlc::System &s, Count &c)
    {
        for (unsigned m = 0; m < s.cms.size(); m++)
        {
            Link *l = s.k->link("L" + std::to_string(m));
            auto pd = l->chan(Channel::D).onAccept;
            l->chan(Channel::D).onAccept = [&c, pd](const ChannelState &ch, const Beat &b, uint64_t t)
            {
                if (pd) pd(ch, b, t);
                if (b.index == 0 && (b.msg->opcode == unsigned(OpD::Grant) || b.msg->opcode == unsigned(OpD::GrantData)))
                { c.grants++; if (b.msg->denied) c.denied++; }
            };
            auto pe = l->chan(Channel::E).onAccept;
            l->chan(Channel::E).onAccept = [&c, pe](const ChannelState &ch, const Beat &b, uint64_t t) { if (pe) pe(ch, b, t); c.gacks++; };
        }
    }
}

TEST(error_slave_behind_the_manager)
{
    for (uint64_t seed = 1; seed <= 3; seed++)
    {
        tlc::Options o;
        o.masters = 4; o.seed = seed; o.ops = 300; o.error_slave = true; o.uncached = 1; o.uncached_ops = 200;
        o.uncached_cfg = {"w_intent(i)=0"};
        tlc::System s;
        s.build(o);
        Count c;
        attachCount(s, c);
        s.run(o.max_cycles);
        CHECK(s.allDone());
        CHECK_EQ(s.monitorViolations(), uint64_t(0));
        if (s.monitorViolations()) std::printf("%s", s.monitorSummary().c_str());
        CHECK_EQ(s.cviol.size(), size_t(0));
        CHECK_EQ(s.wd->trips().size(), size_t(0));
        CHECK(c.grants > 0);
        CHECK_EQ(c.denied, c.grants);           // nothing can be fetched
        CHECK_EQ(c.gacks, c.grants);            // the GrantAck is still sent for every Grant
        uint64_t errors = 0, loads = 0, stores = 0;
        for (auto *m : s.cms)
        {
            errors += m->counters().errors; loads += m->counters().loads; stores += m->counters().stores;
            for (uint64_t a = 0; a < o.span; a += 64) CHECK_EQ(m->lineState(a), std::string("I"));   // permission unchanged
        }
        CHECK_EQ(loads + stores, uint64_t(0));
        CHECK(errors > 0);
        CHECK_EQ(s.opsDone(), uint64_t(4 * o.ops));
        CHECK_EQ(s.mgr->counters().denied_grants, c.denied);
        FuzzMaster *u = s.ums[0];
        CHECK_EQ(u->deniedResponses(), u->answered());
        CHECK_EQ(u->deniedCorruptBeats(), u->deniedDataBeats());   // §4.4
        CHECK(u->deniedDataBeats() > 0);
        std::printf("  seed %llu: %llu denied Grants, %llu GrantAcks, %llu CPU access errors, %llu uncached denied (%llu data beats, all corrupt)\n",
                    (unsigned long long)seed, (unsigned long long)c.denied, (unsigned long long)c.gacks, (unsigned long long)errors,
                    (unsigned long long)u->deniedResponses(), (unsigned long long)u->deniedDataBeats());
    }
}

TEST(error_slave_behind_an_outer_crossbar)
{
    for (uint64_t seed = 1; seed <= 3; seed++)
    {
        tlc::Options o;
        o.masters = 4; o.seed = seed; o.ops = 600; o.span = 1024; o.error_split = 512; o.uncached = 1; o.uncached_ops = 200;
        o.uncached_cfg = {"w_intent(i)=0"};
        tlc::System s;
        s.build(o);
        Count c;
        attachCount(s, c);
        s.run(o.max_cycles);
        CHECK(s.allDone());
        CHECK_EQ(s.monitorViolations(), uint64_t(0));
        if (s.monitorViolations()) std::printf("%s", s.monitorSummary().c_str());
        CHECK_EQ(s.cviol.size(), size_t(0));
        CHECK_EQ(s.vviol.size(), size_t(0));
        CHECK_EQ(s.wd->trips().size(), size_t(0));
        CHECK_EQ(c.gacks, c.grants);
        CHECK(c.denied > 0 && c.denied < c.grants);
        uint64_t errors = 0, done = 0;
        for (auto *m : s.cms)
        {
            errors += m->counters().errors; done += m->counters().loads + m->counters().stores;
            for (uint64_t a = 512; a < 1024; a += 64) CHECK_EQ(m->lineState(a), std::string("I"));   // nothing cached from the denied half
        }
        CHECK(errors > 0 && done > 0);
        CHECK_EQ(s.opsDone(), uint64_t(4 * o.ops));
        CHECK_EQ(s.ums[0]->deniedCorruptBeats(), s.ums[0]->deniedDataBeats());
        std::printf("  seed %llu: %llu Grants (%llu denied), %llu GrantAcks, %llu accesses done, %llu errors\n", (unsigned long long)seed,
                    (unsigned long long)c.grants, (unsigned long long)c.denied, (unsigned long long)c.gacks, (unsigned long long)done, (unsigned long long)errors);
    }
}

TL_TEST_MAIN("TL-DENY-C")
