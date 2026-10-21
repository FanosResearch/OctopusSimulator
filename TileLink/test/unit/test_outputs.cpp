/*
 * File  :      test_outputs.cpp
 * TL-UNIT: the transcript line format (plan §4) and TLStats.csv.
 */
#include "TestMain.h"
#include "tilelink/Kernel.h"
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace octopus::tl;

TEST(transcript_line)
{
    Msg m(Channel::A, 0, 3, 4, DataMode::Full);
    m.address = 0x40; m.source = 2; m.param = 0;
    m.deriveMask();
    for (unsigned b = 0; b < 8; b++) m.data[b] = uint8_t(0x40 + b);
    m.corrupt[1] = 1;
    CHECK_EQ(Transcript::line(17, "L", Channel::A, m, 0), std::string("17 L A PutFullData 0 3 2 0 0x40 0 0xf 0x43424140 0 0"));
    CHECK_EQ(Transcript::line(18, "L", Channel::A, m, 1), std::string("18 L A PutFullData 0 3 2 0 0x40 1 0xf 0x47464544 1 0"));
    Msg d(Channel::D, 1, 2, 4, DataMode::None);
    d.source = 2; d.sink = 5; d.denied = true; d.address = 0; d.deriveMask();
    // Tables 8-10: C, D and E have no mask field, so the column prints "-"
    CHECK_EQ(Transcript::line(20, "L", Channel::D, d, 0), std::string("20 L D AccessAckData 0 2 2 5 0x0 0 - - 0 1"));
    Msg g(Channel::A, 6, 6, 8, DataMode::Full);
    g.param = unsigned(Grow::NtoT); g.address = 0x1000; g.source = 7; g.deriveMask();
    CHECK_EQ(Transcript::line(1, "inner", Channel::A, g, 0), std::string("1 inner A AcquireBlock 1 6 7 0 0x1000 0 0xff - 0 0"));
    Msg e(Channel::E, 0, 0, 8);
    e.sink = 3;
    CHECK_EQ(Transcript::line(2, "inner", Channel::E, e, 0), std::string("2 inner E GrantAck 0 0 0 3 0x0 0 - - 0 0"));
    Msg c(Channel::C, 7, 3, 4, DataMode::Full);   // ReleaseData: data, corrupt, no mask
    c.address = 0x40; c.source = 1; c.param = unsigned(Prune::TtoN); c.deriveMask();
    CHECK_EQ(Transcript::line(3, "inner", Channel::C, c, 1), std::string("3 inner C ReleaseData 1 3 1 0 0x40 1 - 0x00000000 0 0"));
    CHECK_EQ(std::string(Transcript::header()), std::string("#cycle link ch opcode param size source sink address beat mask data corrupt denied"));
}

TEST(transcript_stream_and_file)
{
    std::ostringstream o;
    {
        Transcript t(o);
        Msg m(Channel::A, 4, 3, 8); m.address = 0x80; m.deriveMask();
        t.record(5, "L", Channel::A, m, 0);
        CHECK_EQ(t.lines(), uint64_t(1));
    }
    CHECK_EQ(o.str(), std::string(Transcript::header()) + "\n5 L A Get 0 3 0 0 0x80 0 0xff - 0 0\n");
    const char *path = "tl_transcript_test.txt";
    {
        Transcript t(path);
        Msg m(Channel::A, 4, 3, 8); m.address = 0x80; m.deriveMask();
        t.record(5, "L", Channel::A, m, 0);
    }
    std::ifstream in(path);
    std::stringstream s; s << in.rdbuf();
    CHECK_EQ(s.str(), o.str());
    std::remove(path);
}

TEST(stats_csv)
{
    Stats st;
    ChannelStats cs; cs.beats = 5; cs.busy = 9; cs.stall = 4;
    st.setChannel("L", Channel::A, cs);
    st.recordMessage("L", Channel::A, 0, 4);
    st.recordMessage("L", Channel::A, 0, 6);
    st.recordMessage("L", Channel::A, 4, 0);
    st.recordOperation("L", Channel::A, 4, 12);
    st.recordOutstanding("L", "A", 0); st.recordOutstanding("L", "A", 0); st.recordOutstanding("L", "A", 2);
    const LatencyStats *put = st.message("L", Channel::A, 0);
    CHECK(put != nullptr);
    if (put) { CHECK_EQ(put->count, uint64_t(2)); CHECK_EQ(put->mean(), 5.0); CHECK_EQ(put->max, uint64_t(6)); CHECK_EQ(put->min, uint64_t(4)); CHECK_EQ(put->bucket[2], uint64_t(2)); }
    CHECK(st.message("L", Channel::D, 0) == nullptr);
    CHECK(st.operation("L", Channel::A, 4) != nullptr);
    std::string csv = st.csv();
    CHECK(csv.find("channel,link,ch,beats,busy,stall\nchannel,L,A,5,9,4\n") != std::string::npos);
    CHECK(csv.find("message,L,A,PutFullData,2,5,6,4\n") != std::string::npos);
    CHECK(csv.find("message,L,A,Get,1,0,0,0\n") != std::string::npos);
    CHECK(csv.find("operation,L,A,Get,1,12,12,12\n") != std::string::npos);
    CHECK(csv.find("outstanding,L,A,0,2\n") != std::string::npos);
    CHECK(csv.find("outstanding,L,A,2,1\n") != std::string::npos);
    const char *path = "tl_stats_test.csv";
    st.write(path);
    std::ifstream in(path);
    std::stringstream s; s << in.rdbuf();
    CHECK_EQ(s.str(), csv);
    std::remove(path);
}

TEST(kernel_stats_null_system)
{
    Kernel k(KernelOptions{2, 1, DataMode::None});
    Agent &m = k.add(std::unique_ptr<Agent>(new NullMaster("m")));
    Agent &s = k.add(std::unique_ptr<Agent>(new NullSlave("s")));
    LinkParams p; p.level = Level::C;
    k.connect("L", p, m, s);
    k.run(10);
    k.finishStats();
    std::string csv = k.stats().csv();
    for (const char *ch : {"A", "B", "C", "D", "E"})
        CHECK(csv.find(std::string("channel,L,") + ch + ",0,0,0\n") != std::string::npos);
    CHECK_EQ(k.runUntilIdle(100), uint64_t(1));   // idle at once after reset
}

TL_TEST_MAIN("test_outputs")
