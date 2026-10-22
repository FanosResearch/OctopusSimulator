/*
 * File  :      TLMemSlave.cpp
 * Memory over TileLink: the memory controller's TL-UH slave interface and the LLC bank's TL-UH
 * master interface. Specification: docs/TileLink_Plan.md s5.1 (the memory rows), s5.3 I-B;
 * docs/NoC_Memory_Plan.md M1 (H1, H2, H3, K_MEM_WACK) and docs/NoC_Architecture_Spec.md s25.3
 * (admission). The contract is summarized in TLMemSlave.h.
 */
#include "../../../header/Interconnect/TileLink/TLMemSlave.h"
#include "../../../header/Interconnect/TileLink/TileLinkInterconnect.h"
#include "../../../header/HangDump.h"
#include "../../../header/MemHazard.h"
#include "tilelink/Types.h"
#include <iostream>
#include <sstream>

namespace octopus
{
    static unsigned log2Of(unsigned n) { unsigned b = 0; while ((1u << b) < n) b++; return b; }

    static std::string describe(const Message &m)
    {
        std::ostringstream o;
        o << "msg " << m.msg_id << " addr 0x" << std::hex << m.addr << std::dec << " kind " << (int)m.kind
          << " ch " << TLPort::channelName(m.tl.channel) << " opcode " << (int)m.tl.opcode << " size " << (int)m.tl.size
          << " source " << m.tl.source << " data " << (m.data ? std::to_string(m.data_size) + " bytes" : std::string("none"))
          << " from " << m.from;
        return o.str();
    }

    static TLPort *present(TLPort *port, const char *what)
    {
        if (!port) TileLinkInterconnect::fatal(std::string(what) + ": no such endpoint on the memory crossbar");
        return port;
    }

    // ------------------------------------------------------------------ TLMemSlave
    TLMemSlave::TLMemSlave(TLPort *port, unsigned block_bytes)
        : CommunicationInterface(present(port, "TLMemSlave")->endpoint()), m_port(port), m_block(block_bytes)
    {
        if (port->role() != tl::Role::Slave)
            TileLinkInterconnect::fatal("TLMemSlave " + std::to_string(port->endpoint()) + ": the memory controller's port must be a TileLink slave");
        if ((1u << log2Of(block_bytes)) != block_bytes)
            TileLinkInterconnect::fatal("TLMemSlave " + std::to_string(port->endpoint()) + ": the block size must be a power of two");
        HangDump::add([this]() { std::cerr << dump(); });
    }

    void TLMemSlave::fatal(const std::string &what) const
    {
        TileLinkInterconnect::fatal("memory slave " + std::to_string(m_interface_id) + " (docs/TileLink_Plan.md s5.3 I-B): " + what);
    }

    bool TLMemSlave::peekMessage(Message *out_msg)
    {
        m_have_peek = m_port->peekMessage(&m_peeked);
        if (m_have_peek) out_msg->copy(m_peeked);
        return m_have_peek;
    }

    // The controller has taken the request: check it against the contract, remember what its
    // response must carry, run the read-after-write detector's delivery hook.
    void TLMemSlave::popFrontMessage()
    {
        if (!m_have_peek && !m_port->peekMessage(&m_peeked)) return;
        m_have_peek = false;
        const Message &m = m_peeked;
        if (m.tl.channel != 1)
            fatal(std::string("a memory slave receives only channel A, got ") + TLPort::channelName(m.tl.channel) + ": " + describe(m));
        const unsigned size = log2Of(m_block);
        if (m.tl.opcode == (uint8_t)tl::OpA::Get)
        {
            if (m.kind != Message::K_MEM_READ || m.data != NULL)
                fatal("a Get must be a K_MEM_READ without data (the controller tells a read by its missing data): " + describe(m));
            m_gets++;
        }
        else if (m.tl.opcode == (uint8_t)tl::OpA::PutFullData)
        {
            if (m.kind != Message::K_MEM_WRITE || m.data == NULL)
                fatal("a PutFullData must be a K_MEM_WRITE with the block (the controller acknowledges K_MEM_WRITE only): " + describe(m));
            m_puts++;
        }
        else
            fatal(std::string("the memory slave serves Get and PutFullData only, got ") + tl::opcodeName(tl::Channel::A, m.tl.opcode) + ": " + describe(m));
        if (m.tl.size != size || m.tl.param != 0)
            fatal("a memory request moves one whole block (size " + std::to_string(size) + ", param 0): " + describe(m));
        m_pending.push_back(Pending{m.msg_id, lineOf(m.addr), m.tl.opcode, m.tl.size, m.tl.source});
        if (m.kind == Message::K_MEM_WRITE) MemHazard::writeDelivered(m.addr);
        else                                MemHazard::readDelivered(m.msg_id, m.addr);
        m_port->popFrontMessage();
    }

    // An untagged K_FILL / K_MEM_WACK from the controller: the D header comes from the request it
    // answers (oldest first), the push is of a tagged copy, so a refused push leaves the
    // controller's message as it was.
    bool TLMemSlave::pushMessage(Message &msg, uint64_t cycle, MessageType type)
    {
        (void)type;
        auto match = m_pending.end();
        if (msg.tl.channel != 0)
        {
            // already tagged (a TileLink-aware controller): it names its request by source
            if (msg.tl.channel != 4) fatal(std::string("a memory slave sends only channel D, got ") + TLPort::channelName(msg.tl.channel) + ": " + describe(msg));
            for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
                if (it->source == msg.tl.source) { match = it; break; }
        }
        else if (msg.kind == Message::K_FILL)
        {
            for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
                if (it->opcode == (uint8_t)tl::OpA::Get && it->msg_id == msg.msg_id && it->line == lineOf(msg.addr)) { match = it; break; }
        }
        else if (msg.kind == Message::K_MEM_WACK)
        {
            for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
                if (it->opcode == (uint8_t)tl::OpA::PutFullData && it->line == lineOf(msg.addr) && (msg.msg_id == 0 || it->msg_id == msg.msg_id)) { match = it; break; }
        }
        else
            fatal("the controller sent a response of kind " + std::to_string((int)msg.kind) + "; a memory slave answers with K_FILL or K_MEM_WACK: " + describe(msg));
        if (match == m_pending.end())
            fatal("a response that answers no outstanding request (TileLink s5.4: every response answers exactly one request): " + describe(msg));

        Message d;
        d.copy(msg);
        if (cycle != 0) d.cycle = cycle;
        const bool data = match->opcode == (uint8_t)tl::OpA::Get;
        d.tl.channel = 4;
        d.tl.opcode = (uint8_t)(data ? tl::OpD::AccessAckData : tl::OpD::AccessAck);
        d.tl.param = 0; d.tl.size = match->size; d.tl.source = match->source; d.tl.sink = 0; d.tl.denied = false;
        if (data && (d.data == NULL || d.data_size != (1u << match->size)))
            fatal("an AccessAckData carries its 2^size bytes on every beat (TileLink s4.1): " + describe(d));
        if (!data && d.data != NULL)
            fatal("an AccessAck carries no data: " + describe(d));
        if (!m_port->pushMessage(d, cycle, MessageType::DATA_RESPONSE)) return false;
        m_pending.erase(match);
        m_answered++;
        return true;
    }

    std::string TLMemSlave::dump() const
    {
        std::ostringstream o;
        o << "[TL] memory slave " << m_interface_id << ": " << m_gets << " Gets, " << m_puts << " PutFullData, " << m_answered
          << " answered, " << m_pending.size() << " outstanding";
        if (!m_pending.empty())
            o << " (oldest msg " << m_pending.front().msg_id << " line 0x" << std::hex << m_pending.front().line << std::dec
              << " opcode " << (int)m_pending.front().opcode << " source " << m_pending.front().source << ")";
        o << "\n";
        return o.str();
    }

    // ------------------------------------------------------------------ TLMemMaster
    TLMemMaster::TLMemMaster(TLPort *port, unsigned block_bytes)
        : CommunicationInterface(present(port, "TLMemMaster")->endpoint()), m_port(port), m_block(block_bytes)
    {
        if (port->role() != tl::Role::Master)
            TileLinkInterconnect::fatal("TLMemMaster " + std::to_string(port->endpoint()) + ": an LLC bank's memory port must be a TileLink master");
    }

    bool TLMemMaster::pushMessage(Message &msg, uint64_t cycle, MessageType type)
    {
        const unsigned size = log2Of(m_block);
        auto refuse = [&](const char *what) {
            TileLinkInterconnect::fatal("memory port " + std::to_string(m_interface_id) + " (docs/TileLink_Plan.md s5.3 I-B): " + what + ": " + describe(msg));
        };
        if (msg.kind == Message::K_MEM_READ &&
            (msg.tl.channel != 1 || msg.tl.opcode != (uint8_t)tl::OpA::Get || msg.tl.size != size || msg.tl.param != 0 || msg.data != NULL))
            refuse("a K_MEM_READ is a Get on A, param 0, size of the block, without data");
        if (msg.kind == Message::K_MEM_WRITE &&
            (msg.tl.channel != 1 || msg.tl.opcode != (uint8_t)tl::OpA::PutFullData || msg.tl.size != size || msg.tl.param != 0 || msg.data == NULL))
            refuse("a K_MEM_WRITE is a PutFullData on A, param 0, size of the block, with the block");
        if (!m_port->pushMessage(msg, cycle, type)) return false;
        uint64_t at = cycle ? cycle : msg.cycle;
        if (msg.kind == Message::K_MEM_READ) MemHazard::readSent(msg.msg_id, msg.addr, at);
        else if (msg.kind == Message::K_MEM_WRITE) MemHazard::writeSent(msg.addr, msg.msg_id, at);
        return true;
    }
}
