/*
 * File  :      TLC_Common.h
 * What the TL-C protocol handlers TLC_L1 and TLC_LLC share (docs/TileLink_Plan.md s5.3 I-A, step T7):
 * where their tables live, the conversion between Octopus's Message::TLHeader and the library's
 * tl::MsgHeader, and the complementary_value codes of their messages.
 *
 * The tables are the library's (TileLink/tables, docs/TileLink_TLC_Tables.md): fsm_filename names one
 * of them (TLC_MESI_L1, TLC_MESI_LLC, TLC_MSI_LLC). Octopus's FSM_PATH (Protocols_FSM/) is looked at
 * first, so a copy placed there wins; otherwise tl::findTable searches $TILELINK_TABLES and the
 * library's own tables directory. Both readers parse the same file: the library's coh::Table drives
 * the engine (decode, cells, builders), Octopus's FSMReader (CoherenceProtocolHandler::m_fsm) names
 * the states and events for DebugPrint::transition and the protocol-and-network check.
 *
 * complementary_value (docs/MessageEncoding.md): the TL-C decoders never read it (they decode the
 * TileLink header), but the controller does. Message::isDemandRequest is "no data, from the lower
 * interface, value 0 or 1" and drives the processing queue's demand cap, its per-line FCFS gate and
 * the MSHR admission; Message::isInvalidation is "value 10, no data" and joins that gate. So an
 * AcquireBlock carries 0 (NtoB, a read) or 1 (NtoT / BtoT, a write) and is the LLC's demand request,
 * as GetS / GetM are; every other TileLink message carries REQUEST_TYPE_TL (never 0, 1 or 10): a
 * ProbeAck, Release or GrantAck at the LLC, a Probe at an L1 (which must never wait behind a CPU
 * access to its line: TLC_Tables s5.3, a Probe is answered while the Acquire is outstanding).
 */

#ifndef _TLC_COMMON_H
#define _TLC_COMMON_H

#include "CommunicationInterface.h"
#include "tilelink/Msg.h"
#include "tilelink/agents/CacheMaster.h"   // tl::findTable
#include <fstream>
#include <string>

namespace octopus
{
    namespace tlc
    {
        static const uint64_t REQUEST_TYPE_ACQUIRE_READ  = 0;    // AcquireBlock NtoB (plan s5.1: GetS)
        static const uint64_t REQUEST_TYPE_ACQUIRE_WRITE = 1;    // AcquireBlock NtoT, BtoT (GetM)
        static const uint64_t REQUEST_TYPE_TL            = 20;   // every other TileLink message

        // fsm_path is FSM_PATH + fsm_filename + ".csv": that file if it exists, else the library's table
        // of the same name (TLC_Tables s1.1). Fatal (status 2) when neither exists.
        inline std::string tablePath(const std::string &fsm_path)
        {
            if (std::ifstream(fsm_path).good()) return fsm_path;
            size_t s = fsm_path.find_last_of('/');
            std::string name = s == std::string::npos ? fsm_path : fsm_path.substr(s + 1);
            try { return tl::findTable(name); }
            catch (tl::ConfigError &e)
            {
                std::cerr << "TL-C protocol: " << e.what() << " (fsm_filename names a table of TileLink/tables: TLC_MESI_L1, TLC_MESI_LLC, TLC_MSI_LLC)" << std::endl;
                exit(2);
            }
        }

        // Message::TLHeader (channel 1..5 = A..E) <-> tl::MsgHeader (Channel 0..4). The wire address is
        // the Message's address rounded down to 2^size (TileLink s4.5), as TLPort sends it.
        inline tl::MsgHeader toHeader(const Message &m)
        {
            tl::MsgHeader h;
            h.channel = tl::Channel(m.tl.channel ? m.tl.channel - 1 : 0);
            h.opcode = m.tl.opcode; h.param = m.tl.param; h.size = m.tl.size; h.denied = m.tl.denied;
            h.source = m.tl.source; h.sink = m.tl.sink;
            h.address = m.tl.size >= 64 ? 0 : (m.addr & ~((uint64_t(1) << m.tl.size) - 1));
            return h;
        }
        inline void setHeader(Message &m, const tl::MsgHeader &h)
        {
            m.tl.channel = uint8_t(int(h.channel) + 1);
            m.tl.opcode = h.opcode; m.tl.param = h.param; m.tl.size = h.size; m.tl.denied = h.denied;
            m.tl.source = uint32_t(h.source); m.tl.sink = uint32_t(h.sink);
        }
        inline unsigned lgOf(uint32_t bytes) { unsigned l = 0; while ((1u << l) < bytes) l++; return l; }
    }
}

#endif /* _TLC_COMMON_H */
