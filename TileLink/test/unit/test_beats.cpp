/*
 * File  :      test_beats.cpp
 * TL-UNIT beat math (§4), mask derivation (§4.5, Figure 12), address alignment and the Msg
 * layout in the three data modes (plan §3.3).
 */
#include "TestMain.h"
#include "tilelink/Msg.h"

using namespace octopus::tl;

TEST(beat_count)
{
    // §4: max(1, 2^size / w) for data messages; one beat otherwise, whatever the size.
    CHECK_EQ(beatCount(true, 5, 8), 4u);     // Figure 6: F, size 5 on 8 bytes = 4 beats
    CHECK_EQ(beatCount(true, 0, 8), 1u);     // G, 1 byte
    CHECK_EQ(beatCount(true, 6, 8), 8u);     // H, 64 bytes
    CHECK_EQ(beatCount(false, 4, 8), 1u);    // J, a Get of 16 bytes stays one beat
    CHECK_EQ(beatCount(true, 3, 8), 1u);     // bus-sized
    CHECK_EQ(beatCount(true, 2, 8), 1u);     // smaller than the bus
    CHECK_EQ(beatCount(true, 12, 1), 4096u);
    CHECK_EQ(beatCount(true, 15, 4096), 8u); // the largest size (z = 4) on the widest bus
    CHECK_EQ(beatCount(false, 15, 1), 1u);   // dataless, 32 KB over a 1-byte bus: one beat
    for (unsigned w = 1; w <= 4096; w *= 2)
        for (unsigned size = 0; size <= 15; size++)
        {
            uint64_t bytes = uint64_t(1) << size;
            CHECK_EQ(beatCount(true, size, w), bytes <= w ? 1u : unsigned(bytes / w));
            CHECK_EQ(beatCount(false, size, w), 1u);
        }
}

TEST(alignment)
{
    // §4.5: (address & (2^size - 1)) == 0
    CHECK(aligned(0x40, 5)); CHECK(aligned(0x10, 4)); CHECK(aligned(0x58, 3)); CHECK(aligned(0x24, 2));
    CHECK(aligned(0x76, 1)); CHECK(aligned(0xc1, 0));   // Figure 11's six messages
    CHECK(!aligned(0x44, 5)); CHECK(!aligned(0x12, 2)); CHECK(!aligned(0x77, 1));
    CHECK(aligned(0, 15)); CHECK(aligned(0x8000, 15)); CHECK(!aligned(0x4000, 15));
}

static std::string maskOf(Channel ch, unsigned op, unsigned size, uint64_t addr, unsigned w, unsigned beat = 0)
{
    Msg m(ch, op, size, w);
    m.address = addr;
    m.deriveMask();
    return m.maskHex(beat);
}

TEST(mask_figure_12)
{
    // Figure 12, a 4-byte bus: PutFullData size 3 at 0x40 = 2 beats all HIGH; Get size 3 at
    // 0x40 = one beat, all HIGH (dataless, larger than the bus); size 1 at 0x62 = lanes 2,3.
    CHECK_EQ(maskOf(Channel::A, 0, 3, 0x40, 4, 0), std::string("0xf"));
    CHECK_EQ(maskOf(Channel::A, 0, 3, 0x40, 4, 1), std::string("0xf"));
    CHECK_EQ(maskOf(Channel::A, 4, 3, 0x40, 4), std::string("0xf"));
    CHECK_EQ(maskOf(Channel::A, 0, 1, 0x62, 4), std::string("0xc"));
    CHECK_EQ(maskOf(Channel::A, 1, 1, 0x62, 4), std::string("0xc"));   // PutPartial starts full, then may lower bits
    CHECK_EQ(maskOf(Channel::A, 4, 1, 0x62, 4), std::string("0xc"));   // dataless, smaller than the bus: same as data
    Msg get(Channel::A, 4, 3, 4);
    CHECK_EQ(get.beats(), 1u);
}

TEST(mask_figure_11)
{
    // Figure 11, a 16-byte bus: active lanes per message.
    CHECK_EQ(maskOf(Channel::A, 0, 5, 0x40, 16), std::string("0xffff"));   // 32 bytes, 2 beats, all lanes
    CHECK_EQ(maskOf(Channel::A, 0, 5, 0x40, 16, 1), std::string("0xffff"));
    CHECK_EQ(maskOf(Channel::A, 0, 4, 0x10, 16), std::string("0xffff"));   // bus-sized
    CHECK_EQ(maskOf(Channel::A, 0, 3, 0x58, 16), std::string("0xff00"));   // 8 bytes at lane 8
    CHECK_EQ(maskOf(Channel::A, 0, 2, 0x24, 16), std::string("0x00f0"));   // 4 bytes at lane 4
    CHECK_EQ(maskOf(Channel::A, 0, 1, 0x76, 16), std::string("0x00c0"));   // 2 bytes at lane 6
    CHECK_EQ(maskOf(Channel::A, 0, 0, 0xc1, 16), std::string("0x0002"));   // 1 byte at lane 1
    // narrow buses: w = 1 and 2 print one hex digit
    CHECK_EQ(maskOf(Channel::A, 0, 0, 0x5, 1), std::string("0x1"));
    CHECK_EQ(maskOf(Channel::A, 0, 0, 0x5, 2), std::string("0x2"));
    CHECK_EQ(maskOf(Channel::A, 0, 1, 0x4, 2), std::string("0x3"));
}

TEST(beat_addresses_and_layout)
{
    Msg m(Channel::A, 0, 5, 8, DataMode::Full);
    m.address = 0x40;
    CHECK_EQ(m.beats(), 4u);
    CHECK_EQ(m.bytes(), uint64_t(32));
    for (unsigned k = 0; k < 4; k++) CHECK_EQ(m.beatAddress(k), uint64_t(0x40 + 8 * k));   // §4.5: address + k*w
    CHECK_EQ(m.address, uint64_t(0x40));                                                  // the wire field is constant
    CHECK_EQ(m.mask.size(), size_t(32)); CHECK_EQ(m.data.size(), size_t(32)); CHECK_EQ(m.corrupt.size(), size_t(4));
    CHECK(m.stamp.empty());
    // Stamp mode: one 64-bit value per 8 bytes
    Msg s(Channel::D, 1, 6, 16, DataMode::Stamp);
    CHECK_EQ(s.beats(), 4u); CHECK_EQ(s.stampsPerBeat(), 2u); CHECK_EQ(s.stamp.size(), size_t(8)); CHECK(s.data.empty());
    Msg s4(Channel::D, 1, 4, 4, DataMode::Stamp);      // w < 8: one stamp per beat
    CHECK_EQ(s4.stampsPerBeat(), 1u); CHECK_EQ(s4.stamp.size(), size_t(4));
    // None mode: no data at all, and data messages still have a mask
    Msg n(Channel::A, 0, 5, 8, DataMode::None);
    CHECK(n.data.empty()); CHECK(n.stamp.empty()); CHECK_EQ(n.mask.size(), size_t(32));
    CHECK_EQ(n.dataHex(0), std::string("-"));
    // dataless messages carry no data in any mode (plan §3.3), but a mask (§4.5)
    Msg g(Channel::A, 4, 5, 8, DataMode::Full);
    CHECK(g.data.empty()); CHECK_EQ(g.beats(), 1u); CHECK_EQ(g.mask.size(), size_t(8)); CHECK_EQ(g.maskHex(0), std::string("0xff"));
    CHECK_EQ(g.dataHex(0), std::string("-"));
    // relayout after a size change
    g.size = 2; g.address = 0x24; g.layout(8, DataMode::None);
    CHECK_EQ(g.maskHex(0), std::string("0xf0"));
}

TEST(data_hex)
{
    Msg m(Channel::A, 0, 3, 4, DataMode::Full);
    m.address = 0x40;
    for (unsigned b = 0; b < 8; b++) m.data[b] = uint8_t(0x40 + b);
    // lane 3 first: beat 0 = 43 42 41 40
    CHECK_EQ(m.dataHex(0), std::string("0x43424140"));
    CHECK_EQ(m.dataHex(1), std::string("0x47464544"));
    Msg s(Channel::D, 1, 4, 8, DataMode::Stamp);
    s.stamp[0] = 1; s.stamp[1] = 0xabc;
    CHECK_EQ(s.dataHex(0), std::string("0x0000000000000001"));
    CHECK_EQ(s.dataHex(1), std::string("0x0000000000000abc"));
    CHECK_EQ(s.dataHex(5), std::string("-"));   // out of range
    m.corrupt[1] = 1;
    CHECK(!m.beatCorrupt(0)); CHECK(m.beatCorrupt(1)); CHECK(!m.beatCorrupt(9));
}

TEST(header_equality)
{
    Msg a(Channel::A, 0, 5, 8), b(Channel::A, 0, 5, 8);
    a.address = b.address = 0x40; a.source = b.source = 3;
    CHECK(a.header() == b.header());
    b.address = 0x48;
    CHECK(a.header() != b.header());
    b.address = 0x40; b.param = 1;
    CHECK(a.header() != b.header());
    DataMode dm;
    CHECK(parseDataMode("stamp", dm)); CHECK_EQ(int(dm), int(DataMode::Stamp));
    CHECK(!parseDataMode("bytes", dm));
    CHECK_EQ(std::string(dataModeName(DataMode::Full)), std::string("full"));
}

TL_TEST_MAIN("test_beats")
