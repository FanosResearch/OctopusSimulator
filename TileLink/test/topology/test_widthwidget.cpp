/*
 * File  :      test_widthwidget.cpp
 * TL-TOPO: WidthWidget (plan §3.5). Figure 11's six PutFullData messages (16-byte bus, the
 * byte in each lane = the low byte of its address, §4.5) converted 16 -> 4 and 4 -> 16, bytes
 * and masks compared beat by beat against the §4.5 layout; PutPartialData holes kept;
 * dataless masks re-derived; corrupt OR-ed on merge and copied on split (TL-ADAPT-ERR); D
 * responses repacked in both directions, sub-bus responses included; TL-C links legal
 * (source and sink untouched).
 */
#include "TopoTest.h"
#include "tilelink/agents/WidthWidget.h"

using namespace topo;

namespace
{
    struct Conv
    {
        Kernel k;
        Endpoint *m;
        WidthWidget *ww;
        Agent *s;
        Link *u, *d;
        Conv(unsigned w_up, unsigned w_down, bool ram, Level lvl = Level::UH) : k(opts())
        {
            m = endpoint(k, "m");
            ww = addAgent(k, new WidthWidget("W", Config()));
            if (ram) s = memslave(k, "r"); else s = endpoint(k, "s");
            u = &k.connect("U", lp(w_up, lvl, 4, lvl == Level::C ? 2 : 0), *m, *ww);
            d = &k.connect("D", lp(w_down, lvl, 4, lvl == Level::C ? 2 : 0), *ww, *s);
        }
        Endpoint *sink() const { return static_cast<Endpoint *>(s); }
        MemSlave *ram() const { return static_cast<MemSlave *>(s); }
    };

    struct Fig11 { unsigned size; uint64_t addr; };
    const Fig11 FIG11[6] = {{5, 0x40}, {4, 0x10}, {3, 0x58}, {2, 0x24}, {1, 0x76}, {0, 0xc1}};

    // §4.5: on a bus of w bytes, beat k lane l of an operation (addr, size) carries the byte
    // at laneBase + k*w + l; active iff inside the operation. Compares every lane of every beat.
    int compareLayout(const Endpoint::Got &g, unsigned w, bool data, const std::vector<uint8_t> *mask_pattern = nullptr)
    {
        const Msg &m = *g.m;
        int bad = 0;
        uint64_t base = m.address & ~uint64_t(w - 1);
        for (unsigned l = 0; l < w; l++)
        {
            uint64_t x = base + uint64_t(g.beat) * w + l;
            bool active = x >= m.address && x < m.address + m.bytes();
            bool want_mask = active && (!mask_pattern || (*mask_pattern)[size_t(x - m.address)]);
            if (m.mask[size_t(g.beat) * w + l] != (want_mask ? 1 : 0)) bad++;
            if (data && want_mask && m.beatData(g.beat)[l] != uint8_t(x)) bad++;
        }
        return bad;
    }

    unsigned expectBeats(unsigned size, unsigned w) { return (1u << size) <= w ? 1u : (1u << size) / w; }

    void figure11(unsigned w_up, unsigned w_down)
    {
        Conv c(w_up, w_down, false);
        for (uint64_t i = 0; i < 6; i++)
            c.m->push(make(Channel::A, unsigned(OpA::PutFullData), FIG11[i].size, FIG11[i].addr, i, w_up, DataMode::Full, lowByte));
        CHECK(c.k.runUntilIdle(100) < 100);
        auto beats = c.sink()->beats(Channel::A);
        unsigned total = 0;
        for (auto &f : FIG11) total += expectBeats(f.size, w_down);
        CHECK_EQ(beats.size(), size_t(total));
        size_t i = 0;
        for (uint64_t n = 0; n < 6; n++)
        {
            unsigned nb = expectBeats(FIG11[n].size, w_down);
            for (unsigned k = 0; k < nb && i < beats.size(); k++, i++)
            {
                const auto &g = beats[i];
                CHECK_EQ(g.m->source, n);                  // control fields untouched
                CHECK_EQ(g.m->size, uint8_t(FIG11[n].size));
                CHECK_EQ(g.m->address, FIG11[n].addr);
                CHECK_EQ(g.m->w, w_down);
                CHECK_EQ(g.beat, k);
                CHECK_EQ(compareLayout(g, w_down, true), 0);
            }
        }
    }
}

TEST(figure11_wide_to_narrow)  { figure11(16, 4); }
TEST(figure11_narrow_to_wide)  { figure11(4, 16); }
TEST(figure11_same_width)      { figure11(16, 16); }

TEST(merge_timing)
{
    // The merged beat leaves the cycle after its last part was accepted (plan §3.2). A 32-byte
    // Put on w=4 (8 beats at 0..7) becomes 2 beats on w=16: at 4 (parts 0..3) and 8 (4..7).
    Conv c(4, 16, false);
    c.m->push(make(Channel::A, unsigned(OpA::PutFullData), 5, 0x40, 0, 4, DataMode::Full, lowByte));
    CHECK(c.k.runUntilIdle(50) < 50);
    auto b = c.sink()->beats(Channel::A);
    CHECK_EQ(b.size(), size_t(2));
    if (b.size() == 2) { CHECK_EQ(b[0].cycle, uint64_t(4)); CHECK_EQ(b[1].cycle, uint64_t(8)); }
}

TEST(put_partial_mask_holes_survive)
{
    for (int dir = 0; dir < 2; dir++)
    {
        unsigned wu = dir ? 16 : 4, wd = dir ? 4 : 16;
        Conv c(wu, wd, false);
        MsgPtr p = make(Channel::A, unsigned(OpA::PutPartialData), 4, 0x20, 1, wu, DataMode::Full, lowByte);
        std::vector<uint8_t> pattern(16);
        for (unsigned x = 0; x < 16; x++) pattern[x] = uint8_t((x % 3) == 0);   // non-contiguous (§4.5 allows it)
        for (unsigned x = 0; x < 16; x++) { LanePos q = lanePos(*p, 0x20 + x); p->mask[size_t(q.beat) * wu + q.lane] = pattern[x]; }
        c.m->push(p);
        CHECK(c.k.runUntilIdle(50) < 50);
        auto b = c.sink()->beats(Channel::A);
        CHECK_EQ(b.size(), size_t(expectBeats(4, wd)));
        for (auto &g : b) CHECK_EQ(compareLayout(g, wd, true, &pattern), 0);
    }
}

TEST(dataless_mask_rederived)
{
    // §4.5 (Figure 12's rule): a Get larger than the bus is one beat with every mask bit HIGH;
    // a Get smaller than the bus has the aligned slice.
    Conv c(16, 4, false);
    c.m->push(make(Channel::A, unsigned(OpA::Get), 5, 0x40, 0, 16));
    c.m->push(make(Channel::A, unsigned(OpA::Get), 1, 0x62, 1, 16));
    CHECK(c.k.runUntilIdle(50) < 50);
    auto b = c.sink()->beats(Channel::A);
    CHECK_EQ(b.size(), size_t(2));
    if (b.size() == 2)
    {
        CHECK_EQ(b[0].m->beats(), 1u);
        CHECK_EQ(b[0].m->maskHex(0), std::string("0xf"));
        CHECK_EQ(b[1].m->maskHex(0), std::string("0xc"));     // lanes 2, 3 of a 4-byte bus
    }
    Conv n(4, 16, false);
    n.m->push(make(Channel::A, unsigned(OpA::Get), 1, 0x62, 0, 4));
    n.m->push(make(Channel::A, unsigned(OpA::Get), 3, 0x48, 1, 4));
    CHECK(n.k.runUntilIdle(50) < 50);
    auto bn = n.sink()->beats(Channel::A);
    CHECK_EQ(bn.size(), size_t(2));
    if (bn.size() == 2)
    {
        CHECK_EQ(bn[0].m->maskHex(0), std::string("0x000c"));    // lanes 2, 3 of 16
        CHECK_EQ(bn[1].m->maskHex(0), std::string("0xff00"));    // lanes 8..15
    }
}

TEST(adapt_err_corrupt_merge_and_split)
{
    // TL-ADAPT-ERR: one corrupt narrow beat makes the widened beat corrupt (OR, §4.4); a
    // corrupt wide beat makes every narrow slice corrupt (copy).
    Conv up(4, 16, false);
    MsgPtr p = make(Channel::A, unsigned(OpA::PutFullData), 5, 0x40, 0, 4, DataMode::Full, lowByte);
    p->corrupt[5] = 1;                                         // bytes 0x54..0x57: wide beat 1
    up.m->push(p);
    CHECK(up.k.runUntilIdle(50) < 50);
    auto bu = up.sink()->beats(Channel::A);
    CHECK_EQ(bu.size(), size_t(2));
    if (bu.size() == 2) { CHECK(!bu[0].m->beatCorrupt(0)); CHECK(bu[1].m->beatCorrupt(1)); }
    Conv dn(16, 4, false);
    MsgPtr q = make(Channel::A, unsigned(OpA::PutFullData), 5, 0x40, 0, 16, DataMode::Full, lowByte);
    q->corrupt[1] = 1;                                         // bytes 0x50..0x5f: narrow beats 4..7
    dn.m->push(q);
    CHECK(dn.k.runUntilIdle(50) < 50);
    auto bd = dn.sink()->beats(Channel::A);
    CHECK_EQ(bd.size(), size_t(8));
    for (size_t i = 0; i < bd.size(); i++) CHECK_EQ(bd[i].m->beatCorrupt(bd[i].beat), i >= 4);
}

TEST(d_responses_repacked_both_directions)
{
    for (int dir = 0; dir < 2; dir++)
    {
        unsigned wu = dir ? 4 : 16, wd = dir ? 16 : 4;
        Conv c(wu, wd, true);
        // Writes, then reads of the same bytes: Figure 11's addresses, the sub-bus ones too.
        for (uint64_t i = 0; i < 6; i++)
            c.m->push(make(Channel::A, unsigned(OpA::PutFullData), FIG11[i].size, FIG11[i].addr, i, wu, DataMode::Full, lowByte));
        for (uint64_t i = 0; i < 6; i++)
            c.m->push(make(Channel::A, unsigned(OpA::Get), FIG11[i].size, FIG11[i].addr, 8 + i, wu));
        c.ram()->corrupt_at.insert(0x45);                     // one corrupt RAM beat inside the 32-byte read
        CHECK(c.k.runUntilIdle(200) < 200);
        auto d = c.m->beats(Channel::D);
        unsigned want = 6;
        for (auto &f : FIG11) want += expectBeats(f.size, wu);
        CHECK_EQ(d.size(), size_t(want));
        for (auto &g : d)
        {
            if (g.m->opcode != unsigned(OpD::AccessAckData)) continue;
            const Fig11 &f = FIG11[g.m->source - 8];
            CHECK_EQ(g.m->size, uint8_t(f.size));
            CHECK_EQ(g.m->w, wu);
            // the bytes of the read at their §4.5 lanes, placed by the request's address
            uint64_t base = f.addr & ~uint64_t(wu - 1);
            for (unsigned l = 0; l < wu; l++)
            {
                uint64_t x = base + uint64_t(g.beat) * wu + l;
                if (x >= f.addr && x < f.addr + (1u << f.size)) CHECK_EQ(unsigned(g.m->beatData(g.beat)[l]), unsigned(x & 0xff));
            }
            if (f.addr == 0x40)
            {
                // The RAM beat carrying 0x45 is corrupt. RAM on w=4 (dir 0): narrow beat 1,
                // OR-ed into wide beat 0. RAM on w=16 (dir 1): wide beat 0, copied to narrow 0..3.
                bool want_c = dir == 0 ? g.beat == 0 : g.beat < 4;
                CHECK_EQ(g.m->beatCorrupt(g.beat), want_c);
            }
        }
    }
}

TEST(legal_on_tlc_links)
{
    // Plan §3.5: source and sink pass through, so TL-C is legal; B/C/E repacked too.
    Conv c(16, 4, false, Level::C);
    c.ww->validate();
    MsgPtr probe = make(Channel::B, unsigned(OpB::ProbeBlock), 6, 0x80, 3, 4);
    c.sink()->push(probe);
    MsgPtr pad = make(Channel::C, unsigned(OpC::ProbeAckData), 6, 0x80, 3, 16, DataMode::Full, lowByte);
    pad->param = uint8_t(Prune::TtoN);
    c.m->push(pad, 2);
    MsgPtr gd = make(Channel::D, unsigned(OpD::GrantData), 6, 0, 2, 4, DataMode::Full, lowByte);
    gd->sink = 3;
    c.sink()->push(gd);
    MsgPtr e = make(Channel::E, unsigned(OpE::GrantAck), 0, 0, 0, 16); e->sink = 3;
    c.m->push(e, 30);
    CHECK(c.k.runUntilIdle(100) < 100);
    auto b = c.m->messages(Channel::B);
    CHECK_EQ(b.size(), size_t(1));
    if (b.size() == 1) { CHECK_EQ(b[0].m->source, uint64_t(3)); CHECK_EQ(b[0].m->w, 16u); CHECK_EQ(b[0].m->maskHex(0), std::string("0xffff")); }
    auto cc = c.sink()->beats(Channel::C);
    CHECK_EQ(cc.size(), size_t(16));                           // 64 bytes on 4 lanes
    for (auto &g : cc) { CHECK_EQ(unsigned(g.m->beatData(g.beat)[1]), unsigned((0x80 + 4 * g.beat + 1) & 0xff)); CHECK_EQ(g.m->param, uint8_t(Prune::TtoN)); }
    auto dd = c.m->beats(Channel::D);
    CHECK_EQ(dd.size(), size_t(4));
    if (!dd.empty()) { CHECK_EQ(dd[0].m->sink, uint64_t(3)); CHECK_EQ(dd[0].m->source, uint64_t(2)); }
    for (auto &g : dd) CHECK_EQ(unsigned(g.m->beatData(g.beat)[15]), unsigned((16 * g.beat + 15) & 0xff));
    auto ee = c.sink()->messages(Channel::E);
    CHECK_EQ(ee.size(), size_t(1));
    if (ee.size() == 1) CHECK_EQ(ee[0].m->sink, uint64_t(3));
}

TL_TEST_MAIN("test_widthwidget")
