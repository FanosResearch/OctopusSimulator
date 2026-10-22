/*
 * File  :      test_valuecheck.cpp
 * TL-TOPO: ValueCheck (plan §3.6), the admissible-set rule, driven by a scripted master and a
 * scripted slave so that the acknowledgement order and the returned values are exactly the
 * ones under test: the W1 = 1 / W2 = 2 overlapping writes (a read may return 1 or 2, not 3);
 * a clean write collapsing the set; a write outstanding during a read's interval; an atomic
 * chain (old value checked, new value computed from the returned old value, a wrong old value
 * caught once); a denied write adding nothing; a masked PutPartialData; corrupt and denied
 * read beats not checked; Stamp mode; and a checker shared by two links.
 */
#include "TopoTest.h"
#include "tilelink/ValueCheck.h"

using namespace topo;

namespace
{
    struct VC
    {
        Kernel k;
        Endpoint *m, *s;
        Link *l;
        ValueCheck vc;
        std::vector<ValueViolation> v;
        DataMode mode;
        explicit VC(DataMode md = DataMode::Full) : k(opts()), mode(md)
        {
            m = endpoint(k, "m");
            s = endpoint(k, "s");
            l = &k.connect("L", lp(8), *m, *s);
            vc.attach(*l);
            vc.onViolation = [this](const ValueViolation &x) { v.push_back(x); };
        }
        // A requests and D responses, each at a scripted cycle.
        void put(uint64_t at, uint64_t src, uint64_t addr, unsigned size, std::function<uint64_t(uint64_t)> val, bool partial = false)
        {
            m->push(make(Channel::A, partial ? unsigned(OpA::PutPartialData) : unsigned(OpA::PutFullData), size, addr, src, 8, mode, val), at);
        }
        void get(uint64_t at, uint64_t src, uint64_t addr, unsigned size) { m->push(make(Channel::A, unsigned(OpA::Get), size, addr, src, 8, mode), at); }
        void atomic(uint64_t at, uint64_t src, uint64_t addr, unsigned size, unsigned op, unsigned param, std::function<uint64_t(uint64_t)> val)
        {
            MsgPtr a = make(Channel::A, op, size, addr, src, 8, mode, val);
            a->param = uint8_t(param);
            m->push(a, at);
        }
        void ack(uint64_t at, uint64_t src, unsigned size, bool denied = false)
        {
            MsgPtr d = make(Channel::D, unsigned(OpD::AccessAck), size, 0, src, 8, mode);
            d->denied = denied;
            s->push(d, at);
        }
        MsgPtr data(uint64_t at, uint64_t src, uint64_t addr, unsigned size, std::function<uint64_t(uint64_t)> val)
        {
            MsgPtr d = make(Channel::D, unsigned(OpD::AccessAckData), size, addr, src, 8, mode, val);
            s->push(d, at);
            return d;
        }
        void run() { CHECK(k.runUntilIdle(200) < 200); }
    };
    std::function<uint64_t(uint64_t)> c(uint64_t v) { return [v](uint64_t) { return v; }; }
    std::vector<uint64_t> setOf(const ValueCheck &vc, uint64_t a) { std::vector<uint64_t> v; vc.admissible(a, v); return v; }
}

TEST(overlapping_writes_admit_both_values)
{
    // Plan §3.6: W1 = 1 then W2 = 2 to one byte, both outstanding, W2 acknowledged first. A
    // later read may return 1 or 2 (§6.5: the slave chose either order); 3 is rejected.
    VC t;
    t.put(0, 0, 0x40, 0, c(1));
    t.put(1, 1, 0x40, 0, c(2));
    t.ack(3, 1, 0);                    // W2 first
    t.ack(4, 0, 0);                    // then W1
    t.get(6, 2, 0x40, 0); t.data(8, 2, 0x40, 0, c(1));
    t.get(7, 3, 0x40, 0); t.data(9, 3, 0x40, 0, c(2));
    t.get(8, 4, 0x40, 0); t.data(10, 4, 0x40, 0, c(3));
    t.run();
    CHECK_EQ(t.v.size(), size_t(1));
    if (t.v.size() == 1)
    {
        CHECK_EQ(t.v[0].rule, std::string("V-READ"));
        CHECK_EQ(t.v[0].got, uint64_t(3));
        CHECK_EQ(t.v[0].address, uint64_t(0x40));
        CHECK(t.v[0].admissible == (std::vector<uint64_t>{1, 2}));
        CHECK_EQ(t.v[0].link, std::string("L"));
        CHECK(t.v[0].describe().find("V-READ cycle 10 link L address 0x40 got 0x3 admissible {0x1, 0x2}") != std::string::npos);
    }
    CHECK(setOf(t.vc, 0x40) == (std::vector<uint64_t>{1, 2}));
    CHECK_EQ(t.vc.counters().writes, uint64_t(2));
    CHECK_EQ(t.vc.counters().reads, uint64_t(3));
    CHECK_EQ(t.vc.counters().violations, uint64_t(1));
    CHECK_EQ(t.vc.outstanding(), size_t(0));
}

TEST(clean_write_collapses_the_set)
{
    VC t;
    t.put(0, 0, 0x40, 0, c(1)); t.ack(2, 0, 0);
    t.put(4, 1, 0x40, 0, c(5)); t.ack(6, 1, 0);    // no other write outstanding during [4, 6]
    t.get(8, 2, 0x40, 0); t.data(10, 2, 0x40, 0, c(1));
    t.get(9, 3, 0x40, 0); t.data(11, 3, 0x40, 0, c(5));
    t.run();
    CHECK_EQ(t.v.size(), size_t(1));
    if (t.v.size() == 1) { CHECK_EQ(t.v[0].got, uint64_t(1)); CHECK(t.v[0].admissible == (std::vector<uint64_t>{5})); }
    CHECK(setOf(t.vc, 0x40) == (std::vector<uint64_t>{5}));
    // a byte never written admits any value
    std::vector<uint64_t> any;
    CHECK(!t.vc.admissible(0x41, any));
}

TEST(write_outstanding_during_a_read)
{
    // Plan §3.6: the set at the read's issue plus the writes outstanding during its interval.
    VC t;
    t.put(0, 0, 0x40, 0, c(1)); t.ack(2, 0, 0);
    t.get(4, 1, 0x40, 0);                           // issued with the set {1}
    t.put(5, 2, 0x40, 0, c(2));                     // outstanding while the read is
    t.data(8, 1, 0x40, 0, c(2));                    // the slave served the read after the write: legal
    t.ack(9, 2, 0);
    t.get(11, 3, 0x40, 0); t.data(13, 3, 0x40, 0, c(1));   // after W(2) was acknowledged: 1 is stale
    t.run();
    CHECK_EQ(t.v.size(), size_t(1));
    if (t.v.size() == 1) { CHECK_EQ(t.v[0].cycle, uint64_t(13)); CHECK_EQ(t.v[0].got, uint64_t(1)); }
}

TEST(atomic_chain)
{
    // A 4-byte word = 10; ADD 5 (old 10); ADD 3 (old 15); a read of 18 passes, 15 fails; an
    // atomic returning a wrong old value fails once (V-ATOMIC) and the value it writes is
    // computed from the returned old value, so the read after it is not blamed again.
    VC t;
    auto word = [](uint64_t v) { return [v](uint64_t x) { return (v >> (8 * (x & 3))) & 0xff; }; };
    t.put(0, 0, 0x50, 2, word(10)); t.ack(2, 0, 2);
    t.atomic(4, 1, 0x50, 2, unsigned(OpA::ArithmeticData), unsigned(Arith::ADD), word(5));  t.data(6, 1, 0x50, 2, word(10));
    t.atomic(8, 2, 0x50, 2, unsigned(OpA::ArithmeticData), unsigned(Arith::ADD), word(3));  t.data(10, 2, 0x50, 2, word(15));
    t.get(12, 3, 0x50, 2); t.data(14, 3, 0x50, 2, word(18));
    t.get(15, 4, 0x50, 2); t.data(17, 4, 0x50, 2, word(15));
    t.atomic(20, 5, 0x50, 2, unsigned(OpA::ArithmeticData), unsigned(Arith::ADD), word(1)); t.data(22, 5, 0x50, 2, word(99));
    t.get(24, 6, 0x50, 2); t.data(26, 6, 0x50, 2, word(100));
    // logical: XOR 0xff on byte 0 (per byte), then MAXU against 0x7f
    t.atomic(28, 7, 0x50, 2, unsigned(OpA::LogicalData), unsigned(Logic::XOR), word(0xff)); t.data(30, 7, 0x50, 2, word(100));
    t.get(32, 8, 0x50, 2); t.data(34, 8, 0x50, 2, word(100 ^ 0xff));
    t.atomic(36, 9, 0x50, 2, unsigned(OpA::ArithmeticData), unsigned(Arith::MIN), word(0xfffffff0)); t.data(38, 9, 0x50, 2, word(100 ^ 0xff));
    t.get(40, 10, 0x50, 2); t.data(42, 10, 0x50, 2, word(0xfffffff0));   // MIN signed: -16 < 155
    t.run();
    CHECK_EQ(t.v.size(), size_t(2));
    if (t.v.size() == 2)
    {
        CHECK_EQ(t.v[0].rule, std::string("V-READ")); CHECK_EQ(t.v[0].got, uint64_t(15)); CHECK(t.v[0].admissible == (std::vector<uint64_t>{18}));
        CHECK_EQ(t.v[1].rule, std::string("V-ATOMIC")); CHECK_EQ(t.v[1].got, uint64_t(99));
    }
    CHECK_EQ(t.vc.counters().atomics, uint64_t(5));
}

TEST(denied_write_adds_nothing)
{
    VC t;
    t.put(0, 0, 0x60, 0, c(7)); t.ack(2, 0, 0);
    t.put(4, 1, 0x60, 0, c(9)); t.ack(6, 1, 0, true);           // denied: no effect (§4.4)
    t.get(8, 2, 0x60, 0); t.data(10, 2, 0x60, 0, c(9));
    t.get(9, 3, 0x60, 0); t.data(11, 3, 0x60, 0, c(7));
    t.run();
    CHECK_EQ(t.v.size(), size_t(1));
    if (t.v.size() == 1) { CHECK_EQ(t.v[0].got, uint64_t(9)); CHECK(t.v[0].admissible == (std::vector<uint64_t>{7})); }
    CHECK_EQ(t.vc.counters().denied, uint64_t(1));
}

TEST(masked_put_partial)
{
    // PutPartialData writes its masked bytes only (§4.5).
    VC t;
    auto full = [](uint64_t x) { static const uint8_t b[4] = {0x44, 0x33, 0x22, 0x11}; return uint64_t(b[x & 3]); };
    t.put(0, 0, 0x70, 2, full); t.ack(2, 0, 2);
    MsgPtr pp = make(Channel::A, unsigned(OpA::PutPartialData), 2, 0x70, 1, 8, DataMode::Full, [](uint64_t x) { return (x & 1) ? 0xaau + (x & 2) : 0x99u; });
    pp->mask[0] = 0; pp->mask[2] = 0;                           // lanes 1 and 3 only
    t.m->push(pp, 4); t.ack(6, 1, 2);
    auto good = [](uint64_t x) { static const uint8_t b[4] = {0x44, 0xaa, 0x22, 0xac}; return uint64_t(b[x & 3]); };
    auto bad = [](uint64_t x) { static const uint8_t b[4] = {0x99, 0xaa, 0x22, 0xac}; return uint64_t(b[x & 3]); };
    t.get(8, 2, 0x70, 2); t.data(10, 2, 0x70, 2, good);
    t.get(9, 3, 0x70, 2); t.data(11, 3, 0x70, 2, bad);
    t.run();
    CHECK_EQ(t.v.size(), size_t(1));
    if (t.v.size() == 1) { CHECK_EQ(t.v[0].address, uint64_t(0x70)); CHECK_EQ(t.v[0].got, uint64_t(0x99)); }
}

TEST(corrupt_and_denied_reads_not_checked)
{
    VC t;
    t.put(0, 0, 0x40, 0, c(1)); t.ack(2, 0, 0);
    t.get(4, 1, 0x40, 0); MsgPtr d1 = t.data(6, 1, 0x40, 0, c(3)); d1->corrupt[0] = 1;
    t.get(5, 2, 0x40, 0); MsgPtr d2 = t.data(7, 2, 0x40, 0, c(3)); d2->denied = true; d2->corrupt[0] = 1;
    t.run();
    CHECK(t.v.empty());
    CHECK_EQ(t.vc.counters().bytes_skipped, uint64_t(2));
}

TEST(stamp_mode)
{
    // Plan §3.3: under `stamp` the stamp is the byte value (one per 8-byte group).
    VC t(DataMode::Stamp);
    t.put(0, 0, 0x80, 3, c(1));
    t.put(1, 1, 0x80, 3, c(2));
    t.ack(3, 1, 3); t.ack(4, 0, 3);
    t.get(6, 2, 0x80, 0); t.data(8, 2, 0x80, 0, c(1));
    t.get(7, 3, 0x84, 0); t.data(9, 3, 0x84, 0, c(2));
    t.get(8, 4, 0x80, 3); t.data(10, 4, 0x80, 3, c(3));        // all 8 bytes carry stamp 3
    t.run();
    CHECK_EQ(t.v.size(), size_t(8));
    for (auto &x : t.v) { CHECK_EQ(x.got, uint64_t(3)); CHECK(x.admissible == (std::vector<uint64_t>{1, 2})); }
}

TEST(shared_by_two_links)
{
    // One memory seen through two links: a write acknowledged on link 1 is visible to a read
    // on link 2 (one byte model; outstanding tables per link, so the same source on both
    // links is no conflict).
    Kernel k(opts());
    Endpoint *m1 = endpoint(k, "m1"), *s1 = endpoint(k, "s1"), *m2 = endpoint(k, "m2"), *s2 = endpoint(k, "s2");
    Link &l1 = k.connect("L1", lp(8), *m1, *s1), &l2 = k.connect("L2", lp(8), *m2, *s2);
    ValueCheck vc;
    std::vector<ValueViolation> v;
    vc.onViolation = [&](const ValueViolation &x) { v.push_back(x); };
    // a hook installed before keeps being called (chained, plan: the checker never changes behaviour)
    int prior = 0;
    l1.chan(Channel::A).onAccept = [&](const ChannelState &, const Beat &, uint64_t) { prior++; };
    vc.attach(l1); vc.attach(l2);
    m1->push(make(Channel::A, unsigned(OpA::PutFullData), 0, 0x40, 0, 8, DataMode::Full, c(4)), 0);
    s1->push(make(Channel::D, unsigned(OpD::AccessAck), 0, 0, 0, 8), 2);
    m2->push(make(Channel::A, unsigned(OpA::Get), 0, 0x40, 0, 8), 4);
    s2->push(make(Channel::D, unsigned(OpD::AccessAckData), 0, 0x40, 0, 8, DataMode::Full, c(5)), 6);
    CHECK(k.runUntilIdle(100) < 100);
    CHECK_EQ(v.size(), size_t(1));
    if (v.size() == 1) { CHECK_EQ(v[0].link, std::string("L2")); CHECK(v[0].admissible == (std::vector<uint64_t>{4})); }
    CHECK_EQ(prior, 1);
}

TL_TEST_MAIN("test_valuecheck")
