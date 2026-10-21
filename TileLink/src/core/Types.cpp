/*
 * File  :      Types.cpp
 * The encoding tables of TileLink 1.9.3 (Tables 11-13, 23, 25, 27, 31 and plan §1.2, §1.3)
 * and the single fatal path.
 */
#include "tilelink/Types.h"
#include <cstdlib>
#include <iostream>

namespace octopus { namespace tl
{
    void fatal(const std::string &msg)
    {
        std::cout.flush();
        std::cerr << "TileLink: " << msg << std::endl;
        std::cerr.flush();
        std::exit(2);
    }

    // ------------------------------------------------------------------ names
    const char *channelName(Channel ch)
    {
        static const char *names[NUM_CHANNELS] = {"A", "B", "C", "D", "E"};
        return names[static_cast<int>(ch)];
    }
    const char *levelName(Level l)
    {
        switch (l) { case Level::UL: return "UL"; case Level::UH: return "UH"; default: return "C"; }
    }
    bool parseLevel(const std::string &s0, Level &out)
    {
        std::string s = s0.compare(0, 3, "TL-") == 0 ? s0.substr(3) : s0;
        if (s == "UL") { out = Level::UL; return true; }
        if (s == "UH") { out = Level::UH; return true; }
        if (s == "C")  { out = Level::C;  return true; }
        return false;
    }
    bool parseChannel(const std::string &s, Channel &out)
    {
        if (s.size() != 1 || s[0] < 'A' || s[0] > 'E') return false;
        out = static_cast<Channel>(s[0] - 'A');
        return true;
    }

    // Table 13, by channel and opcode; nullptr = not defined on that channel.
    static const char *const AB_NAMES[8] = {"PutFullData", "PutPartialData", "ArithmeticData", "LogicalData",
                                            "Get", "Intent", nullptr, nullptr};
    static const char *opcodeNameOrNull(Channel ch, unsigned op)
    {
        if (op > 7) return nullptr;
        switch (ch)
        {
            case Channel::A: return op == 6 ? "AcquireBlock" : op == 7 ? "AcquirePerm" : AB_NAMES[op];
            case Channel::B: return op == 6 ? "ProbeBlock" : op == 7 ? "ProbePerm" : AB_NAMES[op];
            case Channel::C:
            {
                static const char *const n[8] = {"AccessAck", "AccessAckData", "HintAck", nullptr,
                                                 "ProbeAck", "ProbeAckData", "Release", "ReleaseData"};
                return n[op];
            }
            case Channel::D:
            {
                static const char *const n[8] = {"AccessAck", "AccessAckData", "HintAck", nullptr,
                                                 "Grant", "GrantData", "ReleaseAck", nullptr};
                return n[op];
            }
            case Channel::E: return op == 0 ? "GrantAck" : nullptr;   // Table 10: no opcode field
        }
        return nullptr;
    }
    const char *opcodeName(Channel ch, unsigned op)
    {
        const char *n = opcodeNameOrNull(ch, op);
        return n ? n : "?";
    }
    bool parseOpcode(Channel ch, const std::string &name, unsigned &op)
    {
        for (unsigned o = 0; o < 8; o++)
        {
            const char *n = opcodeNameOrNull(ch, o);
            if (n && name == n) { op = o; return true; }
        }
        return false;
    }

    // ------------------------------------------------------------------ rules
    bool opcodeLegal(Channel ch, unsigned op) { return opcodeNameOrNull(ch, op) != nullptr; }

    bool hasData(Channel ch, unsigned op)
    {
        if (!opcodeLegal(ch, op)) return false;
        // Table 12: PutFullData, PutPartialData, ArithmeticData, LogicalData on A/B;
        // AccessAckData on C/D; ProbeAckData, ReleaseData on C; GrantData on D.
        switch (ch)
        {
            case Channel::A: case Channel::B: return op <= 3;
            case Channel::C: return op == 1 || op == 5 || op == 7;
            case Channel::D: return op == 1 || op == 5;
            case Channel::E: return false;
        }
        return false;
    }

    bool isRequest(Channel ch, unsigned op)
    {
        if (!opcodeLegal(ch, op)) return false;
        switch (ch)
        {
            case Channel::A: case Channel::B: return true;      // every A/B message is a request
            case Channel::C: return op == 6 || op == 7;         // Release, ReleaseData (Table 14 "request")
            case Channel::D: return op == 4 || op == 5;         // Grant, GrantData (§5.1: both)
            case Channel::E: return false;
        }
        return false;
    }

    Level levelOf(Channel ch, unsigned op)
    {
        // Table 12 groups: TL-UL = Get, Put, AccessAck[Data]; TL-UH = Atomics, Intent, HintAck;
        // TL-C = everything of the Acquire/Probe/Release operations.
        switch (ch)
        {
            case Channel::A: case Channel::B:
                if (op == 0 || op == 1 || op == 4) return Level::UL;
                if (op == 2 || op == 3 || op == 5) return Level::UH;
                return Level::C;
            case Channel::C: case Channel::D:
                if (op == 0 || op == 1) return Level::UL;
                if (op == 2) return Level::UH;
                return Level::C;
            case Channel::E: return Level::C;
        }
        return Level::C;
    }

    bool channelExists(Level level, Channel ch)
    {
        // §3.3-§3.7: A and D mandatory; B, C, E are TL-C.
        if (ch == Channel::A || ch == Channel::D) return true;
        return level == Level::C;
    }

    bool legalOn(Level level, Channel ch, unsigned op)
    {
        return opcodeLegal(ch, op) && channelExists(level, ch) &&
               static_cast<int>(levelOf(ch, op)) <= static_cast<int>(level);
    }

    std::vector<Response> responseOf(Channel ch, unsigned op)
    {
        // Table 12, rightmost column. A requests are answered on D; B requests on C; C Release
        // on D (ReleaseAck); D Grant on E (GrantAck).
        std::vector<Response> r;
        if (!isRequest(ch, op)) return r;
        auto D = [&](unsigned o) { r.push_back({Channel::D, uint8_t(o)}); };
        auto C = [&](unsigned o) { r.push_back({Channel::C, uint8_t(o)}); };
        switch (ch)
        {
            case Channel::A:
                switch (op)
                {
                    case 0: case 1: D(0); break;                 // Put* -> AccessAck
                    case 2: case 3: case 4: D(1); break;         // Atomic, Get -> AccessAckData
                    case 5: D(2); break;                         // Intent -> HintAck
                    case 6: D(4); D(5); break;                   // AcquireBlock -> Grant or GrantData
                    case 7: D(4); break;                         // AcquirePerm -> Grant
                }
                break;
            case Channel::B:
                switch (op)
                {
                    case 0: case 1: C(0); break;
                    case 2: case 3: case 4: C(1); break;
                    case 5: C(2); break;
                    case 6: C(4); C(5); break;                   // ProbeBlock -> ProbeAck or ProbeAckData
                    case 7: C(4); break;                         // ProbePerm -> ProbeAck
                }
                break;
            case Channel::C: D(6); break;                        // Release* -> ReleaseAck
            case Channel::D: r.push_back({Channel::E, 0}); break; // Grant* -> GrantAck
            case Channel::E: break;
        }
        return r;
    }

    bool paramLegal(Channel ch, unsigned op, unsigned param)
    {
        if (!opcodeLegal(ch, op)) return false;
        switch (ch)
        {
            case Channel::A: case Channel::B:
                switch (op)
                {
                    case 0: case 1: case 4: return param == 0;           // §7.2: reserved, 0
                    case 2: return param <= 4;                           // Table 23
                    case 3: return param <= 3;                           // Table 25
                    case 5:                                              // Table 27 / Table 51
                        if (ch == Channel::B) return param <= 1;         // B: Prefetch only
                        return param <= 1 || (param >= 5 && param <= 7);
                    case 6: case 7:
                        return ch == Channel::A ? param <= 2             // Grow (Table 32)
                                                : param <= 2;            // Cap (Table 35)
                }
                return false;
            case Channel::C:
                switch (op)
                {
                    case 0: case 1: case 2: return param == 0;           // reserved (§9.5)
                    case 4: return param <= 5;                           // Prune or Report (Table 37)
                    case 5: case 6: case 7:                              // NtoN illegal (plan §1.3)
                        return param <= 4;
                }
                return false;
            case Channel::D:
                switch (op)
                {
                    case 0: case 1: case 2: case 6: return param == 0;   // reserved; ReleaseAck too
                    case 4: case 5: return param <= 1;                   // Cap toT/toB; never toN (decision 7)
                }
                return false;
            case Channel::E: return param == 0;                          // no param field
        }
        return false;
    }
}}
