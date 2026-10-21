/*
 * File  :      test_types.cpp
 * TL-UNIT encodings: every (channel, opcode, level) cell of plan §1.2, the response table,
 * data flags, names and the param ranges of plan §1.3.
 */
#include "TestMain.h"
#include "tilelink/Types.h"

using namespace octopus::tl;

namespace
{
    // Plan §1.2, one row per table line: channel, opcode, name, level, data, responses.
    struct Row { Channel ch; unsigned op; const char *name; Level level; bool data; std::vector<Response> resp; };
    const Channel A = Channel::A, B = Channel::B, C = Channel::C, D = Channel::D, E = Channel::E;
    const std::vector<Row> TABLE = {
        {A, 0, "PutFullData",    Level::UL, true,  {{D, 0}}},
        {B, 0, "PutFullData",    Level::UL, true,  {{C, 0}}},
        {A, 1, "PutPartialData", Level::UL, true,  {{D, 0}}},
        {B, 1, "PutPartialData", Level::UL, true,  {{C, 0}}},
        {A, 2, "ArithmeticData", Level::UH, true,  {{D, 1}}},
        {B, 2, "ArithmeticData", Level::UH, true,  {{C, 1}}},
        {A, 3, "LogicalData",    Level::UH, true,  {{D, 1}}},
        {B, 3, "LogicalData",    Level::UH, true,  {{C, 1}}},
        {A, 4, "Get",            Level::UL, false, {{D, 1}}},
        {B, 4, "Get",            Level::UL, false, {{C, 1}}},
        {A, 5, "Intent",         Level::UH, false, {{D, 2}}},
        {B, 5, "Intent",         Level::UH, false, {{C, 2}}},
        {A, 6, "AcquireBlock",   Level::C,  false, {{D, 4}, {D, 5}}},
        {A, 7, "AcquirePerm",    Level::C,  false, {{D, 4}}},
        {B, 6, "ProbeBlock",     Level::C,  false, {{C, 4}, {C, 5}}},
        {B, 7, "ProbePerm",      Level::C,  false, {{C, 4}}},
        {C, 0, "AccessAck",      Level::UL, false, {}},
        {D, 0, "AccessAck",      Level::UL, false, {}},
        {C, 1, "AccessAckData",  Level::UL, true,  {}},
        {D, 1, "AccessAckData",  Level::UL, true,  {}},
        {C, 2, "HintAck",        Level::UH, false, {}},
        {D, 2, "HintAck",        Level::UH, false, {}},
        {C, 4, "ProbeAck",       Level::C,  false, {}},
        {C, 5, "ProbeAckData",   Level::C,  true,  {}},
        {C, 6, "Release",        Level::C,  false, {{D, 6}}},
        {C, 7, "ReleaseData",    Level::C,  true,  {{D, 6}}},
        {D, 4, "Grant",          Level::C,  false, {{E, 0}}},
        {D, 5, "GrantData",      Level::C,  true,  {{E, 0}}},
        {D, 6, "ReleaseAck",     Level::C,  false, {}},
        {E, 0, "GrantAck",       Level::C,  false, {}},
    };
    bool defined(Channel ch, unsigned op)
    {
        for (auto &r : TABLE) if (r.ch == ch && r.op == op) return true;
        return false;
    }
}

TEST(every_table_cell)
{
    for (auto &r : TABLE)
    {
        CHECK(opcodeLegal(r.ch, r.op));
        CHECK_EQ(std::string(opcodeName(r.ch, r.op)), std::string(r.name));
        CHECK_EQ(int(levelOf(r.ch, r.op)), int(r.level));
        CHECK_EQ(hasData(r.ch, r.op), r.data);
        CHECK_EQ(isRequest(r.ch, r.op), !r.resp.empty());
        auto resp = responseOf(r.ch, r.op);
        CHECK_EQ(resp.size(), r.resp.size());
        for (size_t i = 0; i < resp.size() && i < r.resp.size(); i++)
        {
            CHECK_EQ(int(resp[i].ch), int(r.resp[i].ch));
            CHECK_EQ(unsigned(resp[i].opcode), unsigned(r.resp[i].opcode));
        }
        unsigned op = 99;
        CHECK(parseOpcode(r.ch, r.name, op));
        CHECK_EQ(op, r.op);
        // Table 12: a message name ending in Data carries data, and only those.
        std::string n = r.name;
        CHECK_EQ(n.size() >= 4 && n.compare(n.size() - 4, 4, "Data") == 0, r.data);
    }
}

TEST(undefined_opcodes)
{
    for (int c = 0; c < NUM_CHANNELS; c++)
        for (unsigned op = 0; op < 16; op++)
        {
            Channel ch = static_cast<Channel>(c);
            CHECK_EQ(opcodeLegal(ch, op), defined(ch, op));
            if (!defined(ch, op))
            {
                CHECK_EQ(std::string(opcodeName(ch, op)), std::string("?"));
                CHECK(!hasData(ch, op));
                CHECK(!isRequest(ch, op));
                CHECK(responseOf(ch, op).empty());
                CHECK(!paramLegal(ch, op, 0));
                for (int l = 0; l < 3; l++) CHECK(!legalOn(static_cast<Level>(l), ch, op));
            }
        }
    // the holes: C/D opcode 3, D opcode 7, E opcodes above 0
    CHECK(!opcodeLegal(C, 3)); CHECK(!opcodeLegal(D, 3)); CHECK(!opcodeLegal(D, 7)); CHECK(!opcodeLegal(E, 1));
}

TEST(legal_on_level)
{
    // Plan §1.1: TL-UL and TL-UH have A and D only; TL-C has all five.
    for (int l = 0; l < 3; l++)
    {
        Level level = static_cast<Level>(l);
        CHECK(channelExists(level, A)); CHECK(channelExists(level, D));
        CHECK_EQ(channelExists(level, B), level == Level::C);
        CHECK_EQ(channelExists(level, C), level == Level::C);
        CHECK_EQ(channelExists(level, E), level == Level::C);
        for (auto &r : TABLE)
        {
            bool expect = channelExists(level, r.ch) && int(r.level) <= l;
            CHECK_EQ(legalOn(level, r.ch, r.op), expect);
        }
    }
    // spot checks of the table
    CHECK(legalOn(Level::UL, A, 4)); CHECK(legalOn(Level::UL, A, 0)); CHECK(legalOn(Level::UL, D, 1));
    CHECK(!legalOn(Level::UL, A, 2)); CHECK(!legalOn(Level::UL, A, 5)); CHECK(!legalOn(Level::UL, D, 2));
    CHECK(legalOn(Level::UH, A, 2)); CHECK(legalOn(Level::UH, A, 5)); CHECK(!legalOn(Level::UH, A, 6));
    CHECK(!legalOn(Level::UH, B, 0));   // B does not exist below TL-C even for a UL message
    CHECK(legalOn(Level::C, B, 0)); CHECK(legalOn(Level::C, E, 0)); CHECK(legalOn(Level::C, C, 7));
}

TEST(names_and_parsing)
{
    CHECK_EQ(std::string(channelName(A)), std::string("A"));
    CHECK_EQ(std::string(channelName(E)), std::string("E"));
    Channel ch;
    CHECK(parseChannel("C", ch)); CHECK_EQ(int(ch), int(C));
    CHECK(!parseChannel("F", ch)); CHECK(!parseChannel("AB", ch));
    Level l;
    CHECK(parseLevel("UL", l)); CHECK_EQ(int(l), int(Level::UL));
    CHECK(parseLevel("TL-UH", l)); CHECK_EQ(int(l), int(Level::UH));
    CHECK(parseLevel("C", l)); CHECK_EQ(int(l), int(Level::C));
    CHECK(!parseLevel("X", l));
    CHECK_EQ(std::string(levelName(Level::UH)), std::string("UH"));
    unsigned op;
    CHECK(!parseOpcode(A, "ProbeBlock", op));   // B only
    CHECK(parseOpcode(B, "ProbeBlock", op)); CHECK_EQ(op, 6u);
    CHECK(!parseOpcode(C, "Grant", op));        // D only
    // priority and direction (§2.2, §4)
    CHECK(priority(A) < priority(B) && priority(B) < priority(C) && priority(C) < priority(D) && priority(D) < priority(E));
    CHECK(masterSends(A)); CHECK(!masterSends(B)); CHECK(masterSends(C)); CHECK(!masterSends(D)); CHECK(masterSends(E));
}

TEST(param_ranges)
{
    // Table 23 arithmetic 0..4, Table 25 logical 0..3
    for (unsigned p = 0; p < 8; p++) { CHECK_EQ(paramLegal(A, 2, p), p <= 4); CHECK_EQ(paramLegal(A, 3, p), p <= 3); }
    // Table 27 intent on A: 0,1,5,6,7; on B prefetch only (Table 51)
    for (unsigned p = 0; p < 8; p++)
    {
        CHECK_EQ(paramLegal(A, 5, p), p <= 1 || p >= 5);
        CHECK_EQ(paramLegal(B, 5, p), p <= 1);
    }
    // reserved params are 0 on Put, Get, AccessAck*, HintAck, ReleaseAck, GrantAck
    for (unsigned p = 1; p < 8; p++)
    {
        CHECK(!paramLegal(A, 0, p)); CHECK(!paramLegal(A, 1, p)); CHECK(!paramLegal(A, 4, p));
        CHECK(!paramLegal(D, 0, p)); CHECK(!paramLegal(D, 1, p)); CHECK(!paramLegal(D, 2, p)); CHECK(!paramLegal(D, 6, p));
        CHECK(!paramLegal(C, 0, p)); CHECK(!paramLegal(E, 0, p));
    }
    CHECK(paramLegal(A, 0, 0)); CHECK(paramLegal(D, 6, 0)); CHECK(paramLegal(E, 0, 0));
    // Grow NtoB/NtoT/BtoT on Acquire; Cap toT/toB/toN on Probe
    for (unsigned p = 0; p < 8; p++) { CHECK_EQ(paramLegal(A, 6, p), p <= 2); CHECK_EQ(paramLegal(A, 7, p), p <= 2); CHECK_EQ(paramLegal(B, 6, p), p <= 2); }
    // Prune 0..2 and Report 3..5 on ProbeAck; NtoN (5) illegal on ProbeAckData, Release, ReleaseData
    for (unsigned p = 0; p < 8; p++)
    {
        CHECK_EQ(paramLegal(C, 4, p), p <= 5);
        CHECK_EQ(paramLegal(C, 5, p), p <= 4);
        CHECK_EQ(paramLegal(C, 6, p), p <= 4);
        CHECK_EQ(paramLegal(C, 7, p), p <= 4);
    }
    // plan §8 decision 7: Grant and GrantData never carry toN (2)
    CHECK(paramLegal(D, 4, unsigned(Cap::toT))); CHECK(paramLegal(D, 4, unsigned(Cap::toB))); CHECK(!paramLegal(D, 4, unsigned(Cap::toN)));
    CHECK(paramLegal(D, 5, 1)); CHECK(!paramLegal(D, 5, 2));
    // enum values of plan §1.3
    CHECK_EQ(unsigned(Arith::ADD), 4u); CHECK_EQ(unsigned(Logic::SWAP), 3u); CHECK_EQ(unsigned(Intent::CBOInval), 5u);
    CHECK_EQ(unsigned(Intent::CBOFlush), 7u); CHECK_EQ(unsigned(Grow::BtoT), 2u); CHECK_EQ(unsigned(Prune::BtoN), 2u);
    CHECK_EQ(unsigned(Report::TtoT), 3u); CHECK_EQ(unsigned(Report::NtoN), 5u);
}

TL_TEST_MAIN("test_types")
