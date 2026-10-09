/*
 * File  :      NetTypes.h
 * Network-on-chip core types: message classes, packets, flits, the fatal path.
 * Specification: docs/NoC_Architecture_Spec.md §6, §16.
 */
#ifndef _NET_TYPES_H
#define _NET_TYPES_H
#include "CommunicationInterface.h"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace octopus { namespace net
{
    // Message classes = virtual networks (spec §6.3). With num_classes == 2 the legacy
    // split applies: 0 = no data, 1 = data.
    // (plain constants: MessageType::REQUEST already exists at global scope)
    constexpr int CLASS_REQUEST = 0, CLASS_FORWARD = 1, CLASS_RESPONSE = 2;

    // Raised at construction for a configuration the network refuses (spec §4.5). The
    // Network object turns it into fatal(); unit tests catch it to check the message.
    struct ConfigError : std::runtime_error { using std::runtime_error::runtime_error; };

    // The single fatal path (spec §16): prints "Network: <msg>" and exits with status 2.
    [[noreturn]] void fatal(const std::string &msg);

    // Flit count of a message (spec §6.2).
    inline int flitCount(const Message &m, int flit_bytes, int header_bytes)
    {
        if (flit_bytes <= 0) return 1;
        int payload = (m.data != NULL) ? m.data_size : 0;
        int bytes = header_bytes + payload;
        return bytes <= 0 ? 1 : (bytes + flit_bytes - 1) / flit_bytes;
    }

    struct Packet
    {
        Message  msg;              // the message, delivered bit-identical (spec §3.2)
        uint64_t seq = 0;          // network-global injection sequence; the tie-breaker everywhere
        int      cls = 0;
        int      dst_endpoint = -1;
        int      src_router = -1, dst_router = -1;
        int      flits = 1;
        int      next_flit = 0;    // flits already injected
        uint64_t t_inject = 0;
        // per-hop statistics (spec §15.3)
        int      hops = 0;
        uint64_t wait_ticks = 0, transfer_ticks = 0, ahead = 0;
    };

    struct Flit
    {
        uint64_t seq = 0;          // packet it belongs to
        int      index = 0;        // 0 = head
        bool     head = false, tail = false;
        uint64_t arrived = 0;      // tick it arrived in (or will arrive in) its input VC
    };
}}
#endif /* _NET_TYPES_H */
