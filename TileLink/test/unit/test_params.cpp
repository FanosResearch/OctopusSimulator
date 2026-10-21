/*
 * File  :      test_params.cpp
 * TL-UNIT: Table 4 range validation with its exact messages, derived values, link channels.
 */
#include "TestMain.h"
#include "tilelink/Agent.h"
#include "tilelink/Link.h"
#include "tilelink/Params.h"

using namespace octopus::tl;

TEST(validate_messages)
{
    LinkParams p;
    CHECK_EQ(p.validate(), std::string(""));
    p.w = 3;    CHECK_EQ(p.validate(), std::string("w must be a power of two between 1 and 4096, got 3"));
    p.w = 0;    CHECK_EQ(p.validate(), std::string("w must be a power of two between 1 and 4096, got 0"));
    p.w = 8192; CHECK_EQ(p.validate(), std::string("w must be a power of two between 1 and 4096, got 8192"));
    p.w = 4096; CHECK_EQ(p.validate(), std::string(""));
    p.w = 1;    CHECK_EQ(p.validate(), std::string(""));
    p.a = 129;  CHECK_EQ(p.validate(), std::string("a must be between 0 and 128, got 129"));
    p.a = 0;    CHECK_EQ(p.validate(), std::string(""));
    p.a = 128;  CHECK_EQ(p.validate(), std::string(""));
    p.z = 0;    CHECK_EQ(p.validate(), std::string("z must be between 1 and 4, got 0"));
    p.z = 5;    CHECK_EQ(p.validate(), std::string("z must be between 1 and 4, got 5"));
    p.z = 4;    CHECK_EQ(p.validate(), std::string(""));
    p.o = 65;   CHECK_EQ(p.validate(), std::string("o must be between 0 and 64, got 65"));
    p.o = 64;   CHECK_EQ(p.validate(), std::string(""));
    p.i = 65;   CHECK_EQ(p.validate(), std::string("i must be between 0 and 64, got 65"));
    p.i = 0;    CHECK_EQ(p.validate(), std::string(""));
    // the first violation wins
    p.w = 5; p.z = 9;
    CHECK_EQ(p.validate(), std::string("w must be a power of two between 1 and 4096, got 5"));
}

TEST(derived_values)
{
    LinkParams p;
    p.w = 8; p.z = 3; p.o = 4; p.i = 2; p.a = 32;
    CHECK_EQ(p.maxSize(), 7u);
    p.z = 4; CHECK_EQ(p.maxSize(), 15u);
    p.z = 1; CHECK_EQ(p.maxSize(), 1u);
    CHECK_EQ(p.beatsOf(true, 5), 4u); CHECK_EQ(p.beatsOf(false, 5), 1u);
    CHECK_EQ(p.numSources(), uint64_t(16)); CHECK_EQ(p.numSinks(), uint64_t(4));
    p.o = 0; CHECK_EQ(p.numSources(), uint64_t(1));
    p.o = 64; CHECK_EQ(p.numSources(), ~uint64_t(0));
    CHECK(p.addressFits(0xffffffffull)); CHECK(!p.addressFits(0x100000000ull));
    p.a = 0; CHECK(p.addressFits(0)); CHECK(!p.addressFits(1));
    p.a = 128; CHECK(p.addressFits(~uint64_t(0)));
    p.level = Level::UL;
    CHECK(p.hasChannel(Channel::A)); CHECK(p.hasChannel(Channel::D)); CHECK(!p.hasChannel(Channel::B));
    p.level = Level::C;
    CHECK(p.hasChannel(Channel::B)); CHECK(p.hasChannel(Channel::C)); CHECK(p.hasChannel(Channel::E));
    p.w = 8; p.a = 32; p.z = 3; p.o = 4; p.i = 2; p.level = Level::UH;
    CHECK_EQ(p.describe(), std::string("TL-UH w=8 a=32 z=3 o=4 i=2"));
}

TEST(link_channels)
{
    NullMaster m("m"); NullSlave s("s");
    LinkParams ul; ul.level = Level::UH;
    Link l1("l1", ul, &m, &s);
    CHECK(l1.has(Channel::A)); CHECK(l1.has(Channel::D));
    CHECK(!l1.has(Channel::B)); CHECK(!l1.has(Channel::C)); CHECK(!l1.has(Channel::E));
    int n = 0; l1.forEachChannel([&](ChannelState &) { n++; });
    CHECK_EQ(n, 2);
    LinkParams c; c.level = Level::C;
    Link l2("l2", c, &m, &s);
    n = 0; l2.forEachChannel([&](ChannelState &) { n++; });
    CHECK_EQ(n, 5);
    // §4: senders and receivers
    CHECK(l2.sender(Channel::A) == &m); CHECK(l2.receiver(Channel::A) == &s);
    CHECK(l2.sender(Channel::B) == &s); CHECK(l2.receiver(Channel::B) == &m);
    CHECK(l2.sender(Channel::C) == &m); CHECK(l2.sender(Channel::D) == &s); CHECK(l2.sender(Channel::E) == &m);
    CHECK_EQ(l2.chan(Channel::E).name(), std::string("l2.E"));
    CHECK_EQ(l2.chan(Channel::B).linkName(), std::string("l2"));
    CHECK(l2.chan(Channel::B).link() == &l2);
}

TL_TEST_MAIN("test_params")
