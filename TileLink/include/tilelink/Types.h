/*
 * File  :      Types.h
 * TileLink core types: channels, conformance levels, opcodes, params, the encoding tables
 * and the single fatal path. Specification: TileLink 1.9.3 §2.2, §3, §6.2, §7.2, §8.2, §9.3;
 * docs/TileLink_Plan.md §1.2, §1.3.
 *
 * Everything here is a pure function of the encoding tables; no simulator state.
 */
#ifndef _TL_TYPES_H
#define _TL_TYPES_H
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace octopus { namespace tl
{
    // The single fatal path: prints "TileLink: <msg>" to stderr and exits with status 2, as
    // net::fatal does for the network. Never status 0.
    [[noreturn]] void fatal(const std::string &msg);

    // Raised for a configuration the library refuses (bad parameters, unknown agent type, a
    // capability below the link's level). Kernel::elaborate turns it into fatal(); the unit tests
    // catch it to check the message.
    struct ConfigError : std::runtime_error { using std::runtime_error::runtime_error; };

    // §2.2, Table 2: the five channels in priority order A < B < C < D < E.
    enum class Channel : uint8_t { A = 0, B = 1, C = 2, D = 3, E = 4 };
    constexpr int NUM_CHANNELS = 5;

    // §1 (conformance levels): TL-UL < TL-UH < TL-C.
    enum class Level : uint8_t { UL = 0, UH = 1, C = 2 };

    // Table 13: opcodes per channel. Opcode numbering is per channel; A and B share theirs,
    // C and D share 0..2 (plan §1.2). The wire carries a plain integer (Msg::opcode), these
    // enums name it.
    enum class OpA : uint8_t { PutFullData = 0, PutPartialData = 1, ArithmeticData = 2, LogicalData = 3,
                               Get = 4, Intent = 5, AcquireBlock = 6, AcquirePerm = 7 };
    enum class OpB : uint8_t { PutFullData = 0, PutPartialData = 1, ArithmeticData = 2, LogicalData = 3,
                               Get = 4, Intent = 5, ProbeBlock = 6, ProbePerm = 7 };
    enum class OpC : uint8_t { AccessAck = 0, AccessAckData = 1, HintAck = 2,
                               ProbeAck = 4, ProbeAckData = 5, Release = 6, ReleaseData = 7 };
    enum class OpD : uint8_t { AccessAck = 0, AccessAckData = 1, HintAck = 2,
                               Grant = 4, GrantData = 5, ReleaseAck = 6 };
    enum class OpE : uint8_t { GrantAck = 0 };   // Table 10: E has no opcode field; 0 names GrantAck

    // Plan §1.3: param encodings.
    enum class Arith  : uint8_t { MIN = 0, MAX = 1, MINU = 2, MAXU = 3, ADD = 4 };          // Table 23
    enum class Logic  : uint8_t { XOR = 0, OR = 1, AND = 2, SWAP = 3 };                     // Table 25
    enum class Intent : uint8_t { PrefetchRead = 0, PrefetchWrite = 1,                      // Table 27
                                  CBOInval = 5, CBOClean = 6, CBOFlush = 7 };
    enum class Cap    : uint8_t { toT = 0, toB = 1, toN = 2 };                               // Grant, Probe
    enum class Grow   : uint8_t { NtoB = 0, NtoT = 1, BtoT = 2 };                            // Acquire
    enum class Prune  : uint8_t { TtoB = 0, TtoN = 1, BtoN = 2 };                            // ProbeAck, Release
    enum class Report : uint8_t { TtoT = 3, BtoB = 4, NtoN = 5 };                            // ProbeAck, Release

    // ------------------------------------------------------------------ names
    const char *channelName(Channel ch);                 // "A".."E"
    const char *levelName(Level l);                      // "UL", "UH", "C"
    bool parseLevel(const std::string &s, Level &out);   // accepts "UL"/"UH"/"C" and "TL-UL"/...
    bool parseChannel(const std::string &s, Channel &out);
    // The message name of (channel, opcode), or "?" when the opcode is not defined there.
    const char *opcodeName(Channel ch, unsigned op);
    bool parseOpcode(Channel ch, const std::string &name, unsigned &op);

    // ------------------------------------------------------------------ encoding rules
    // Table 13: is the opcode defined on the channel.
    bool opcodeLegal(Channel ch, unsigned op);
    // Table 12: does the message carry a data payload (its name ends with Data).
    bool hasData(Channel ch, unsigned op);
    // §5.1: a request message requires a response. A and B are requests; C Release and
    // ReleaseData; D Grant and GrantData (Grants are both request and response).
    bool isRequest(Channel ch, unsigned op);
    // Table 11/12: the lowest conformance level that defines the message.
    Level levelOf(Channel ch, unsigned op);
    // Plan §1.1: which channels a link of this level has (A+D, or A..E).
    bool channelExists(Level level, Channel ch);
    // The message is defined on that channel and at that level (opcodeLegal && levelOf <= level
    // && channelExists).
    bool legalOn(Level level, Channel ch, unsigned op);

    // Table 12, rightmost column: the only allowed responses to a request message.
    struct Response { Channel ch; uint8_t opcode; };
    std::vector<Response> responseOf(Channel ch, unsigned op);

    // Plan §1.3 and plan §8 decision 7: the param ranges per message. Reserved params must be
    // 0; Intent on B is Prefetch only (Table 51); NtoN is illegal on ProbeAckData, Release and
    // ReleaseData; Grant and GrantData never carry toN.
    bool paramLegal(Channel ch, unsigned op, unsigned param);

    // §2.2: priority order A < B < C < D < E (0 lowest). Sender side per §4: A, C, E master
    // to slave; B, D slave to master.
    inline int priority(Channel ch) { return static_cast<int>(ch); }
    inline bool masterSends(Channel ch) { return ch == Channel::A || ch == Channel::C || ch == Channel::E; }
    // Tables 6-10: only A and B carry a mask field (§4.5); C and D carry data and corrupt
    // without a mask, E carries only a sink.
    inline bool hasMask(Channel ch) { return ch == Channel::A || ch == Channel::B; }

    // ------------------------------------------------------------------ beat math (§4, §4.5)
    // §4: a message with data has max(1, 2^size / w) beats; every other message is one beat.
    inline unsigned beatCount(bool has_data, unsigned size, unsigned w)
    {
        if (!has_data) return 1;
        uint64_t bytes = uint64_t(1) << size;
        return bytes <= w ? 1u : unsigned(bytes / w);
    }
    // §4.5: (address & (2^size - 1)) == 0 always holds.
    inline bool aligned(uint64_t address, unsigned size) { return (address & ((uint64_t(1) << size) - 1)) == 0; }
}}
#endif /* _TL_TYPES_H */
