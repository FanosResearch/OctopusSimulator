/*
 * File  :      test_wave.cpp
 * TL-WAVE (plan §6): each spec waveform figure (test/golden/fig*.txt) is driven by the scenario of
 * test/golden/scenarios.md through a real link with the Monitor attached; the produced transcript
 * is compared with the golden's compared section (`# compare:`), beat for beat, fields written
 * `-` in the golden not compared; the first differing line is printed. 0 monitor violations.
 */
#include "TestMain.h"
#include "ScriptedSlave.h"
#include "tilelink/Monitor.h"
#include "tilelink/agents/RamSlave.h"
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

using namespace octopus::tl;
using namespace octopus::tl::fixture;

namespace
{
    std::vector<std::string> split(const std::string &s)
    {
        std::istringstream in(s);
        std::vector<std::string> v;
        std::string t;
        while (in >> t) v.push_back(t);
        return v;
    }
    uint64_t num(const std::string &s) { return std::strtoull(s.c_str(), nullptr, 0); }

    // The golden's compared section: the lines of [<compare>] (14 fields each).
    std::vector<std::vector<std::string>> goldenSection(const std::string &fig, std::string *which)
    {
        std::ifstream in(std::string(TL_GOLDEN_DIR) + "/" + fig + ".txt");
        std::string line, compare, section;
        std::vector<std::string> lines;
        while (std::getline(in, line)) lines.push_back(line);
        for (auto &l : lines)
            if (l.compare(0, 11, "# compare: ") == 0) compare = split(l.substr(11))[0];
        std::vector<std::vector<std::string>> out;
        bool in_sec = false;
        for (auto &l : lines)
        {
            if (!l.empty() && l[0] == '[') { in_sec = l == "[" + compare + "]"; continue; }
            if (!in_sec || l.empty() || l[0] == '#') continue;
            auto f = split(l);
            if (f.size() >= 14) out.push_back(f);
        }
        if (which) *which = compare;
        return out;
    }

    // Lane bytes of the transcript's data field ("0x" + lane w-1 first).
    std::vector<int> lanesOf(const std::string &hex, unsigned w)
    {
        std::vector<int> v(w, -1);
        if (hex.size() != 2 + 2 * size_t(w)) return v;
        for (unsigned l = 0; l < w; l++) v[l] = int(std::strtoul(hex.substr(2 + 2 * size_t(w - 1 - l), 2).c_str(), nullptr, 16));
        return v;
    }

    // Compares one golden field with the transcript's; "" when equal, else the reason.
    std::string fieldDiff(size_t i, const std::string &g, const std::string &t, const std::vector<std::string> &tl, unsigned w)
    {
        if (g == "-") return "";
        switch (i)
        {
            case 1: case 2: case 3: return g == t ? "" : "differs";
            case 11:
            {
                std::vector<int> lanes = lanesOf(t, w);
                if (g.compare(0, 2, "0x") != 0)
                {   // lane notation, lane 0 first, ".." = inactive (fig11)
                    for (unsigned l = 0; l < w && 2 * l + 1 < g.size(); l++)
                    {
                        std::string b = g.substr(2 * l, 2);
                        if (b == "..") continue;
                        if (lanes[l] != int(std::strtoul(b.c_str(), nullptr, 16))) return "lane " + std::to_string(l) + " differs";
                    }
                    return "";
                }
                // the little-endian value of the active lanes (fig18, fig22)
                unsigned size = unsigned(num(tl[5]));
                uint64_t addr = num(tl[8]), bytes = uint64_t(1) << size;
                unsigned first = bytes >= w ? 0 : unsigned(addr % w), n = unsigned(bytes >= w ? w : bytes);
                uint64_t v = 0;
                for (unsigned k = 0; k < n && k < 8; k++) if (lanes[first + k] >= 0) v |= uint64_t(lanes[first + k]) << (8 * k);
                return v == num(g) ? "" : "value " + std::to_string(v) + " != " + g;
            }
            default:
                if (t == "-") return "missing";
                return num(g) == num(t) ? "" : "differs";
        }
    }

    struct Wave
    {
        std::ostringstream tr;   // before the kernel: its Transcript writes here until the kernel is destroyed
        Kernel k;
        ScriptedMaster *m = nullptr;
        ScriptedSlave *ss = nullptr;
        RamSlave *rs = nullptr;
        Link *l = nullptr;
        std::unique_ptr<Monitor> mon;
        uint64_t t0;
        unsigned w;
        bool expect_mismatch = false;   // the comparator's own check: a difference must be found
        bool mismatch = false;

        Wave(unsigned w_, unsigned z, unsigned o, Level level, bool ram, int max_size, uint64_t t0_ = 100)
            : k(KernelOptions{100, 1, DataMode::Full}), t0(t0_), w(w_)
        {
            m = static_cast<ScriptedMaster *>(&k.add(std::unique_ptr<Agent>(new ScriptedMaster("M"))));
            Agent *s;
            if (ram)
            {
                rs = new RamSlave("S", Config());
                rs->latency = 1;   // scenarios.md §1.1: the first response beat in the cycle after the last request beat
                rs->max_size = max_size;
                s = &k.add(std::unique_ptr<Agent>(rs));
            }
            else s = ss = static_cast<ScriptedSlave *>(&k.add(std::unique_ptr<Agent>(new ScriptedSlave("S"))));
            LinkParams p; p.w = w; p.a = 32; p.z = z; p.o = o; p.i = 0; p.level = level;
            l = &k.connect("L0", p, *m, *s);
            MonitorOptions mo;
            mo.mode = MonitorOptions::Report;   // scenarios.md: fatal; report gives the same verdict with every line printed
            mo.max_size = max_size;
            mon.reset(new Monitor(k, *l, mo));
            k.enableTranscript(tr);
        }
        ~Wave() { mon.reset(); }

        // A request with data laid out in Full mode; data = low byte of each lane's address (scenarios.md §1.2).
        MsgPtr A(OpA op, unsigned size, uint64_t addr, uint64_t src, unsigned param = 0)
        {
            MsgPtr x = makeMsg(k, Channel::A, unsigned(op), size, w, addr, src, DataMode::Full, param);
            uint64_t base = addr & ~uint64_t(w - 1);
            for (size_t i = 0; i < x->data.size(); i++) x->data[i] = uint8_t(base + i);
            return x;
        }
        MsgPtr D(OpD op, unsigned size, uint64_t src, uint64_t addr = 0)
        {
            return makeMsg(k, Channel::D, unsigned(op), size, w, addr, src, DataMode::Full);
        }
        void ma(int c, const MsgPtr &x, unsigned beat = 0) { m->at(t0 + uint64_t(c), Channel::A, x, beat); }
        void sd(int c, const MsgPtr &x, unsigned beat = 0) { ss->at(t0 + uint64_t(c), Channel::D, x, beat); }
        void aLow(std::initializer_list<int> cs) { for (int c : cs) if (ss) ss->notReady(Channel::A, t0 + uint64_t(c)); }
        void dLow(std::initializer_list<int> cs) { for (int c : cs) m->notReady(Channel::D, t0 + uint64_t(c)); }

        // Runs to t0 + horizon, then compares with the golden; returns the number of differences.
        void check(const std::string &fig, int horizon = 40)
        {
            k.run(t0 + uint64_t(horizon));
            mon->finish(k.cycle());
            CHECK_EQ(mon->violations(), uint64_t(0));
            if (mon->violations()) std::printf("  %s\n", mon->summary().c_str());
            std::string which;
            auto gold = goldenSection(fig, &which);
            CHECK(!gold.empty());
            uint64_t last = 0;
            std::set<std::string> chans;
            for (auto &g : gold) { last = std::max<uint64_t>(last, num(g[0])); chans.insert(g[2]); }
            std::vector<std::vector<std::string>> got;
            std::istringstream in(tr.str());
            std::string line;
            while (std::getline(in, line))
            {
                if (line.empty() || line[0] == '#') continue;
                auto f = split(line);
                if (f.size() < 14 || f[1] != "L0" || !chans.count(f[2])) continue;
                uint64_t c = num(f[0]);
                if (c < t0 || c > t0 + last) continue;
                f[0] = std::to_string(c - t0);
                got.push_back(f);
            }
            size_t n = std::max(gold.size(), got.size());
            for (size_t i = 0; i < n; i++)
            {
                if (i >= gold.size() || i >= got.size())
                {
                    std::printf("  %s [%s] line %zu: %s\n", fig.c_str(), which.c_str(), i,
                                i >= got.size() ? "missing in the transcript" : "extra beat in the transcript");
                    mismatch = true;
                    if (!expect_mismatch) CHECK(false);
                    return;
                }
                for (size_t fi = 0; fi < 14; fi++)
                {
                    std::string why = fieldDiff(fi, gold[i][fi], got[i][fi], got[i], w);
                    if (why.empty()) continue;
                    std::string g, t;
                    for (auto &x : gold[i]) g += x + " ";
                    for (auto &x : got[i]) t += x + " ";
                    std::printf("  %s [%s] first difference, line %zu field %zu (%s):\n    golden:     %s\n    transcript: %s\n",
                                fig.c_str(), which.c_str(), i, fi, why.c_str(), g.c_str(), t.c_str());
                    mismatch = true;
                    if (!expect_mismatch) CHECK(false);
                    return;
                }
            }
            std::printf("  %s: %zu beats match [%s]\n", fig.c_str(), gold.size(), which.c_str());
        }
    };
}

TEST(fig06_channel_A_ready_valid)
{
    // scenarios.md §2: F G H I J K on w=8; H rejected and never sent.
    Wave s(8, 3, 3, Level::UH, false, 6);
    MsgPtr F = s.A(OpA::PutFullData, 5, 0x100, 0), G = s.A(OpA::PutFullData, 0, 0x201, 1), H = s.A(OpA::PutFullData, 6, 0x400, 2),
           I = s.A(OpA::PutFullData, 2, 0x304, 3), J = s.A(OpA::Get, 4, 0x500, 4), K = s.A(OpA::PutFullData, 1, 0x602, 5);
    s.ma(1, F, 0); s.ma(2, F, 0); s.ma(4, F, 1); s.ma(6, F, 1); s.ma(7, F, 2); s.ma(8, F, 3);
    s.ma(9, G); s.ma(11, H); s.ma(12, I); s.ma(14, J); s.ma(15, K);
    s.aLow({0, 1, 4, 11});
    // the five accepted requests answered after the window (M-RESP-7)
    s.ss->send(s.t0 + 20, Channel::D, s.D(OpD::AccessAck, 5, 0, 0x100));
    s.ss->send(s.t0 + 20, Channel::D, s.D(OpD::AccessAck, 0, 1, 0x201));
    s.ss->send(s.t0 + 20, Channel::D, s.D(OpD::AccessAck, 2, 3, 0x304));
    s.ss->send(s.t0 + 20, Channel::D, s.D(OpD::AccessAckData, 4, 4, 0x500));
    s.ss->send(s.t0 + 20, Channel::D, s.D(OpD::AccessAck, 1, 5, 0x602));
    s.check("fig06");
}

TEST(fig07_channel_D_ready_valid)
{
    // scenarios.md §3: t0 = 110; the requests are issued at run cycles 100..105.
    Wave s(8, 3, 3, Level::UH, false, -1, 110);
    MsgPtr g0 = s.A(OpA::Get, 5, 0x100, 0), g1 = s.A(OpA::Get, 0, 0x201, 1), g2 = s.A(OpA::Get, 4, 0x300, 2),
           p3 = s.A(OpA::PutFullData, 0, 0x401, 3), p4 = s.A(OpA::PutFullData, 4, 0x500, 4);
    s.m->at(100, Channel::A, g0); s.m->at(101, Channel::A, g1); s.m->at(102, Channel::A, g2); s.m->at(103, Channel::A, p3);
    s.m->at(104, Channel::A, p4, 0); s.m->at(105, Channel::A, p4, 1);
    MsgPtr L = s.D(OpD::AccessAckData, 5, 0, 0x100), M = s.D(OpD::AccessAckData, 0, 1, 0x201), N = s.D(OpD::AccessAckData, 4, 2, 0x300),
           O = s.D(OpD::AccessAck, 0, 3, 0x401), P = s.D(OpD::AccessAck, 4, 4, 0x500);
    s.sd(1, L, 0); s.sd(2, L, 0); s.sd(4, L, 1); s.sd(6, L, 1); s.sd(7, L, 2); s.sd(8, L, 3); s.sd(9, M);
    s.sd(11, N, 0); s.sd(12, O); s.sd(14, P); s.sd(15, N, 0); s.sd(16, N, 1);
    s.dLow({0, 1, 4, 11});
    s.check("fig07");
}

TEST(fig08_get_accessackdata_delay)
{
    Wave s(8, 3, 1, Level::UH, false, -1);
    MsgPtr g1 = s.A(OpA::Get, 5, 0x100, 0), g2 = s.A(OpA::Get, 5, 0x200, 1);
    s.ma(1, g1); s.ma(9, g2); s.ma(10, g2);
    s.aLow({0, 9});
    MsgPtr r1 = s.D(OpD::AccessAckData, 5, 0, 0x100), r2 = s.D(OpD::AccessAckData, 5, 1, 0x200);
    for (unsigned b = 0; b < 4; b++) { s.sd(4 + int(b), r1, b); s.sd(11 + int(b), r2, b); }
    s.check("fig08");
}

TEST(fig09_putfulldata_accessack_delay)
{
    Wave s(8, 3, 1, Level::UH, false, -1);
    MsgPtr p1 = s.A(OpA::PutFullData, 5, 0x100, 0), p2 = s.A(OpA::PutFullData, 5, 0x200, 1);
    for (unsigned b = 0; b < 4; b++) { s.ma(1 + int(b), p1, b); s.ma(9 + int(b), p2, b); }
    s.sd(7, s.D(OpD::AccessAck, 5, 0, 0x100));
    s.sd(10, s.D(OpD::AccessAck, 5, 1, 0x200));   // while Put#2's beats 1..3 are still being sent (§4.2.2)
    s.check("fig09");
}

TEST(fig10_atomic_bursts)
{
    Wave s(8, 3, 2, Level::UH, false, -1);
    MsgPtr a1 = s.A(OpA::ArithmeticData, 4, 0x100, 0, unsigned(Arith::ADD)), a2 = s.A(OpA::ArithmeticData, 4, 0x200, 1, unsigned(Arith::ADD)),
           a3 = s.A(OpA::ArithmeticData, 3, 0x308, 2, unsigned(Arith::ADD));   // drawn PutPartialData (fig10.txt FIGURE ERROR)
    s.ma(1, a1, 0); s.ma(2, a1, 1); s.ma(8, a2, 0); s.ma(9, a2, 1); s.ma(10, a2, 1); s.ma(13, a3);
    s.aLow({0, 9});
    MsgPtr r1 = s.D(OpD::AccessAckData, 4, 0, 0x100), r2 = s.D(OpD::AccessAckData, 4, 1, 0x200), r3 = s.D(OpD::AccessAckData, 3, 2, 0x308);
    s.sd(5, r1, 0); s.sd(6, r1, 1); s.sd(9, r2, 0); s.sd(11, r2, 1); s.sd(14, r3);
    s.check("fig10");
}

TEST(fig11_byte_lanes_16B)
{
    Wave s(16, 3, 3, Level::UH, true, 6);
    MsgPtr p0 = s.A(OpA::PutFullData, 5, 0x40, 0), p1 = s.A(OpA::PutFullData, 4, 0x10, 1), p2 = s.A(OpA::PutFullData, 3, 0x58, 2),
           p3 = s.A(OpA::PutFullData, 2, 0x24, 3), p4 = s.A(OpA::PutFullData, 1, 0x76, 4), p5 = s.A(OpA::PutFullData, 0, 0xc1, 5);
    s.ma(1, p0, 0); s.ma(2, p0, 1); s.ma(4, p1); s.ma(6, p2); s.ma(8, p3); s.ma(10, p4); s.ma(12, p5);
    s.check("fig11");
    CHECK_EQ(s.rs->readByte(0x5f), uint8_t(0x5f));
    CHECK_EQ(s.rs->readByte(0xc1), uint8_t(0xc1));
}

TEST(fig12_mask_4B)
{
    Wave s(4, 2, 3, Level::UH, true, 3);
    MsgPtr p0 = s.A(OpA::PutFullData, 3, 0x40, 0), p1 = s.A(OpA::PutPartialData, 3, 0x40, 1), g2 = s.A(OpA::Get, 3, 0x40, 2),
           p3 = s.A(OpA::PutFullData, 1, 0x62, 3), p4 = s.A(OpA::PutPartialData, 1, 0x62, 4), g5 = s.A(OpA::Get, 1, 0x62, 5);
    setMask(*p1, 0, 0xa); setMask(*p1, 1, 0x4); setMask(*p4, 0, 0x4);
    s.ma(1, p0, 0); s.ma(2, p0, 1); s.ma(4, p1, 0); s.ma(5, p1, 1); s.ma(7, g2); s.ma(9, p3); s.ma(11, p4); s.ma(13, g5);
    s.check("fig12");
}

TEST(fig17_out_of_order_responses)
{
    Wave s(8, 3, 2, Level::UH, false, -1);
    s.ma(1, s.A(OpA::PutFullData, 3, 0x100, 1)); s.ma(2, s.A(OpA::Get, 3, 0x108, 2));
    s.ma(3, s.A(OpA::Intent, 3, 0x110, 3, unsigned(Intent::PrefetchRead)));
    s.sd(6, s.D(OpD::HintAck, 3, 3, 0x110)); s.sd(7, s.D(OpD::AccessAck, 3, 1, 0x100)); s.sd(8, s.D(OpD::AccessAckData, 3, 2, 0x108));
    s.check("fig17");
}

TEST(fig18_tlul_get_put)
{
    Wave s(4, 2, 0, Level::UL, true, 2);
    MsgPtr p1 = s.A(OpA::PutFullData, 2, 0x0, 0), g1 = s.A(OpA::Get, 2, 0x0, 0), p2 = s.A(OpA::PutFullData, 2, 0x0, 0),
           pp = s.A(OpA::PutPartialData, 2, 0x0, 0), g2 = s.A(OpA::Get, 2, 0x0, 0);
    setData(*p1, 0, 0xab); setData(*p2, 0, 0x0); setData(*pp, 0, 0xf); setMask(*pp, 0, 0x3);
    s.ma(1, p1); s.ma(5, g1); s.ma(9, p2); s.ma(13, pp); s.ma(17, g2);
    s.check("fig18");
    CHECK_EQ(s.rs->readWord(0, 4), uint64_t(0xf));
}

TEST(fig22_tluh_atomic_hint)
{
    Wave s(8, 3, 0, Level::UH, true, 3);
    MsgPtr i0 = s.A(OpA::Intent, 3, 0x0, 0, unsigned(Intent::PrefetchWrite)), p1 = s.A(OpA::PutFullData, 3, 0x0, 0),
           a2 = s.A(OpA::ArithmeticData, 3, 0x0, 0, unsigned(Arith::ADD)), l3 = s.A(OpA::LogicalData, 3, 0x0, 0, unsigned(Logic::SWAP)),
           g4 = s.A(OpA::Get, 3, 0x0, 0);
    setData(*p1, 0, 0x1); setData(*a2, 0, 0x1); setData(*l3, 0, 0x3);
    s.ma(1, i0); s.ma(5, p1); s.ma(9, a2); s.ma(13, l3); s.ma(17, g4);
    s.check("fig22");
    CHECK_EQ(s.rs->readWord(0, 8), uint64_t(0x3));
}

TEST(comparator_detects_a_wrong_value)
{
    // The Figure 22 scenario with the ADD operand 0x2 instead of 0x1: every later data value is
    // off by one, so the comparator must report a difference (and the monitor still sees nothing).
    Wave s(8, 3, 0, Level::UH, true, 3);
    s.expect_mismatch = true;
    MsgPtr i0 = s.A(OpA::Intent, 3, 0x0, 0, unsigned(Intent::PrefetchWrite)), p1 = s.A(OpA::PutFullData, 3, 0x0, 0),
           a2 = s.A(OpA::ArithmeticData, 3, 0x0, 0, unsigned(Arith::ADD)), l3 = s.A(OpA::LogicalData, 3, 0x0, 0, unsigned(Logic::SWAP)),
           g4 = s.A(OpA::Get, 3, 0x0, 0);
    setData(*p1, 0, 0x1); setData(*a2, 0, 0x2); setData(*l3, 0, 0x3);
    s.ma(1, i0); s.ma(5, p1); s.ma(9, a2); s.ma(13, l3); s.ma(17, g4);
    s.check("fig22");
    CHECK(s.mismatch);
}

TL_TEST_MAIN("TL-WAVE")
