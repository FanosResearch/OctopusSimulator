/*
 * File  :      TLPort.cpp
 * One Octopus endpoint on a TileLink link. Specification: docs/TileLink_Plan.md s5.2 (the TLPort
 * contract), s5.3 I-A (accounting); the contract is summarized in TLPort.h.
 */
#include "../../../header/Interconnect/TileLink/TLPort.h"
#include "../../../header/Interconnect/TileLink/TileLinkInterconnect.h"
#include "../../../header/Logger.h"
#include "tilelink/Kernel.h"
#include "tilelink/agents/Pipe.h"
#include <algorithm>
#include <sstream>

namespace octopus
{
    using tl::Channel;

    const char *TLPort::channelName(int oc)
    {
        static const char *n[] = {"-", "A", "B", "C", "D", "E"};
        return (oc >= 0 && oc <= 5) ? n[oc] : "?";
    }

    static std::string describe(const Message &m)
    {
        std::ostringstream o;
        o << "msg " << m.msg_id << " addr 0x" << std::hex << m.addr << std::dec << " kind " << (int)m.kind
          << " ch " << TLPort::channelName(m.tl.channel) << " opcode " << (int)m.tl.opcode;
        if (m.tl.channel >= 1 && m.tl.channel <= 5) o << " (" << tl::opcodeName(Channel(m.tl.channel - 1), m.tl.opcode) << ")";
        o << " param " << (int)m.tl.param << " size " << (int)m.tl.size << " source " << m.tl.source << " sink " << m.tl.sink
          << " from " << m.from << " to";
        for (int d : m.to) o << " " << d;
        return o.str();
    }

    // s5.3 I-A: A and B as the request bus, C, D and E as the response bus; a memory-side link as the
    // memory bus (I-B).
    static Logger::Role busRole(int c, bool memory_side)
    {
        if (memory_side) return Logger::Role::MEM_BUS;
        return (c == (int)Channel::A || c == (int)Channel::B) ? Logger::Role::REQ_BUS : Logger::Role::RESP_BUS;
    }

    // ------------------------------------------------------------------ identifier pools
    int TLPort::IdPool::alloc(uint32_t t)
    {
        for (size_t k = 0; k < tag.size(); k++)
            if (tag[k] < 0) { tag[k] = t; in_use++; return (int)k; }
        return -1;
    }
    bool TLPort::IdPool::release(uint64_t id, uint32_t *t)
    {
        if (id >= tag.size() || tag[id] < 0) return false;
        *t = (uint32_t)tag[id]; tag[id] = -1; in_use--;
        return true;
    }

    TLPort::TLPort(TileLinkInterconnect *owner, int id, const std::string &name, tl::Role role, const Depths &d, bool memory_side)
        : CommunicationInterface(id), tl::Agent(name), m_owner(owner), m_role(role), m_memory_side(memory_side), m_depth(d)
    {
        for (int c = 0; c < tl::NUM_CHANNELS; c++)
            if (m_depth.rx[c] < 1 || m_depth.tx[c] < 1)
                TileLinkInterconnect::fatal("port " + std::to_string(id) + ": rx and tx queue depths must be >= 1");
    }

    void TLPort::fatal(const std::string &what) const
    {
        TileLinkInterconnect::fatal("port " + std::to_string(m_interface_id) + " (" + m_name + "): " + what);
    }

    tl::Link *TLPort::link() const
    {
        if (!m_link)
        {
            if (m_links.size() != 1) fatal("a TLPort has exactly one link, this one has " + std::to_string(m_links.size()));
            m_link = m_links[0].link;
        }
        return m_link;
    }

    bool TLPort::sends(int oc) const
    {
        if (oc < 1 || oc > 5) return false;
        Channel ch = Channel(toTl(oc));
        return link()->has(ch) && (tl::masterSends(ch) == (m_role == tl::Role::Master));
    }
    bool TLPort::receives(int oc) const
    {
        if (oc < 1 || oc > 5) return false;
        Channel ch = Channel(toTl(oc));
        return link()->has(ch) && (tl::masterSends(ch) != (m_role == tl::Role::Master));
    }

    int TLPort::channelOf(MessageType type, bool tx) const
    {
        int base = type == MessageType::DATA_RESPONSE ? 4 : type == MessageType::SERVICE_REQUEST ? 2 : 1;
        bool ok = tx ? sends(base) : receives(base);
        if (ok) return base;
        int other = base == 1 ? 2 : base == 2 ? 1 : 3;   // A <-> B, D -> C
        return (tx ? sends(other) : receives(other)) ? other : base;
    }

    void TLPort::sizePools()
    {
        if (m_pools_sized) return;
        const tl::LinkParams &p = link()->params();
        size_t ns = (size_t)std::min<uint64_t>(p.numSources(), 4096), nk = (size_t)std::min<uint64_t>(p.numSinks(), 4096);
        m_src_a.tag.assign(ns, -1); m_src_c.tag.assign(ns, -1); m_sinks.tag.assign(nk, -1);
        m_pools_sized = true;
    }
    TLPort::IdPool *TLPort::poolFor(int oc)
    {
        sizePools();
        if (m_role == tl::Role::Master) return oc == 1 ? &m_src_a : oc == 3 ? &m_src_c : nullptr;
        return oc == 4 ? &m_sinks : nullptr;
    }
    const TLPort::IdPool *TLPort::poolFor(int oc) const { return const_cast<TLPort *>(this)->poolFor(oc); }
    int TLPort::idsInUse(int oc) const { const IdPool *p = poolFor(oc); return p ? p->in_use : 0; }
    int TLPort::poolSize(int oc) const { const IdPool *p = poolFor(oc); return p ? (int)p->tag.size() : 0; }

    // ------------------------------------------------------------------ admission (s5.2 points 1-3)
    int TLPort::txFreeSlotsCh(int oc)
    {
        if (!sends(oc)) return 0;
        int c = toTl(oc);
        return std::max(0, m_depth.tx[c] - (int)m_tx[c].size() - m_tx_reserved[c]);
    }
    void TLPort::txReserveCh(int oc, int n) { if (sends(oc)) m_tx_reserved[toTl(oc)] += n; }
    void TLPort::txReleaseCh(int oc, int n)
    {
        if (!sends(oc)) return;
        int &r = m_tx_reserved[toTl(oc)]; r = std::max(0, r - n);
    }
    int TLPort::txFreeIdsCh(int oc)
    {
        IdPool *p = poolFor(oc);
        return p ? std::max(0, p->free()) : (1 << 30);
    }
    void TLPort::txReserveIdsCh(int oc, int n) { if (IdPool *p = poolFor(oc)) p->reserved += n; }
    void TLPort::txReleaseIdsCh(int oc, int n) { if (IdPool *p = poolFor(oc)) p->reserved = std::max(0, p->reserved - n); }

    // ------------------------------------------------------------------ the endpoint's RX
    bool TLPort::peekMessage(Message *out_msg)
    {
        m_peeked = -1;
        for (int c = tl::NUM_CHANNELS - 1; c >= 0; c--)          // E, D, C, B, A (s5.2)
            if (!m_rx[c].empty()) { m_peeked = c; break; }
        if (m_peeked < 0) return false;
        out_msg->copy(m_rx[m_peeked].front());
        return true;
    }
    void TLPort::popFrontMessage()
    {
        if (m_peeked < 0 || m_rx[m_peeked].empty()) return;
        m_rx[m_peeked].pop_front();
        m_peeked = -1;
    }
    bool TLPort::pushMessage2RX(Message &msg, MessageType type)
    {
        int oc = msg.tl.channel ? msg.tl.channel : channelOf(type, false);
        if (!receives(oc)) fatal(std::string("pushMessage2RX on channel ") + channelName(oc) + ", which this port does not receive: " + describe(msg));
        int c = toTl(oc);
        if ((int)m_rx[c].size() + (m_rx_busy[c] ? 1 : 0) >= m_depth.rx[c]) return false;
        m_rx[c].push_back(msg);
        return true;
    }
    bool TLPort::canAcceptRX(MessageType type)
    {
        int oc = channelOf(type, false);
        if (!receives(oc)) return false;
        int c = toTl(oc);
        return (int)m_rx[c].size() + (m_rx_busy[c] ? 1 : 0) < m_depth.rx[c];
    }

    // ------------------------------------------------------------------ the endpoint's TX: Message -> tl::Msg
    bool TLPort::pushMessage(Message &msg, uint64_t, MessageType)
    {
        int oc = msg.tl.channel;
        if (oc < 1 || oc > 5)
            fatal("pushMessage of a message with no TileLink channel (Message::tl.channel " + std::to_string(oc) +
                  "): a TileLink interface carries only TileLink-tagged messages; " + describe(msg));
        if (!sends(oc))
            fatal(std::string("a ") + tl::roleName(m_role) + " port cannot send on channel " + channelName(oc) +
                  " of link " + link()->name() + " (" + link()->params().describe() + "): " + describe(msg));
        const int c = toTl(oc);
        const Channel ch = Channel(c);
        const unsigned op = msg.tl.opcode;
        if (!tl::opcodeLegal(ch, op)) fatal("opcode " + std::to_string(op) + " is not defined on channel " + channelName(oc) + ": " + describe(msg));
        msg.from = m_interface_id;          // every interface stamps the sender, as NetworkInterface does

        // B fans out to every destination in `to`; every other channel is one message routed by the wire
        std::vector<int> dsts;
        if (ch == Channel::B)
        {
            if (msg.to.empty()) fatal("a B message needs its destinations in Message::to: " + describe(msg));
            dsts.assign(msg.to.begin(), msg.to.end());
        }
        else dsts.push_back(-1);
        if ((int)m_tx[c].size() + (int)dsts.size() > m_depth.tx[c]) { m_refused++; return false; }

        // the identifier this message allocates (s5.2): every A a source, a Release[Data] on C a
        // source, a Grant[Data] on D a sink
        IdPool *pool = nullptr;
        if (ch == Channel::A) pool = poolFor(1);
        else if (ch == Channel::C && (op == (unsigned)tl::OpC::Release || op == (unsigned)tl::OpC::ReleaseData)) pool = poolFor(3);
        else if (ch == Channel::D && (op == (unsigned)tl::OpD::Grant || op == (unsigned)tl::OpD::GrantData)) pool = poolFor(4);
        if (pool && (int)pool->tag.size() - pool->in_use < (int)dsts.size()) { m_refused++; return false; }

        // size and data (s4, s4.5): a data message without an explicit size takes it from data_size
        const bool has_data = tl::hasData(ch, op);
        unsigned size = msg.tl.size;
        if (msg.data != NULL && !has_data)
            fatal(std::string("the message carries data but ") + tl::opcodeName(ch, op) + " has no data field: " + describe(msg));
        if (has_data && msg.data != NULL)
        {
            if (size == 0 && msg.data_size > 1)
            {
                while ((1u << size) < msg.data_size) size++;
            }
            if ((1u << size) != msg.data_size)
                fatal("data_size " + std::to_string(msg.data_size) + " is not 2^size (size " + std::to_string(size) + "): " + describe(msg));
        }
        const unsigned w = link()->params().w;
        const uint64_t wire_addr = size >= 64 ? 0 : (msg.addr & ~((uint64_t(1) << size) - 1));
        const tl::DataMode mode = (has_data && msg.data != NULL) ? tl::DataMode::Full : tl::DataMode::None;

        for (int dst : dsts)
        {
            tl::MsgPtr m = std::make_shared<tl::Msg>();
            m->channel = ch; m->opcode = (uint8_t)op; m->param = msg.tl.param; m->size = (uint8_t)size;
            m->address = wire_addr; m->denied = msg.tl.denied;
            m->source = msg.tl.source; m->sink = msg.tl.sink;
            m->layout(w, mode);
            if (mode == tl::DataMode::Full)
                for (uint64_t x = 0; x < (uint64_t(1) << size); x++)
                {
                    tl::LanePos p = tl::lanePos(m->address, w, wire_addr + x);
                    m->data[size_t(p.beat) * w + p.lane] = msg.data[x];
                }
            if (ch == Channel::A || ch == Channel::C)
            {
                if (pool) m->source = (uint64_t)pool->alloc(msg.tl.source);
                dst = m_owner->endpointOfAddress(wire_addr);
                if (dst < 0) fatal("address 0x" + [&] { std::ostringstream o; o << std::hex << wire_addr; return o.str(); }() + " is not in the crossbar's address map (routes): " + describe(msg));
            }
            else if (ch == Channel::B)
                m->source = m_owner->sourceBaseOf(dst);
            else if (ch == Channel::D)
            {
                if (pool) m->sink = (uint64_t)pool->alloc(msg.tl.sink);
                dst = m_owner->endpointOfSource(m->source);
                if (dst < 0) fatal("source " + std::to_string(m->source) + " of a D message belongs to no master: " + describe(msg));
            }
            else
            {
                dst = m_owner->endpointOfSink(m->sink);
                if (dst < 0) fatal("sink " + std::to_string(m->sink) + " of an E message belongs to no slave: " + describe(msg));
            }
            if (ch != Channel::B && !msg.to.empty() && std::find(msg.to.begin(), msg.to.end(), dst) == msg.to.end())
                fatal("the message is addressed to endpoint " + std::to_string(msg.to[0]) + " but the wire routes it to " + std::to_string(dst) +
                      (ch == Channel::A || ch == Channel::C ? " (the crossbar's address map does not express the home map; routes(vs))" : "") + ": " + describe(msg));
            m->id = kernel()->newMsgId();
            m->from_agent = id();
            m_owner->stash(m->id, msg, m_interface_id, dst);
            m_tx[c].push_back(TxEntry{m, 0, dst});
        }
        return true;
    }

    // ------------------------------------------------------------------ the TileLink side: three phases
    void TLPort::offer(uint64_t)
    {
        for (int c = 0; c < tl::NUM_CHANNELS; c++)
            if (!m_tx[c].empty() && sends(toOctopus(c)))
                link()->chan(Channel(c)).offer(m_tx[c].front().msg, m_tx[c].front().beat);
    }

    void TLPort::accept(uint64_t)
    {
        // ready = the receive queue is not full; a burst in progress already holds its slot
        for (int c = 0; c < tl::NUM_CHANNELS; c++)
            if (receives(toOctopus(c)))
                link()->chan(Channel(c)).setReady(m_rx_busy[c] || (int)m_rx[c].size() < m_depth.rx[c]);
    }

    void TLPort::commit(uint64_t)
    {
        for (int c = 0; c < tl::NUM_CHANNELS; c++)
        {
            int oc = toOctopus(c);
            if (sends(oc))
            {
                const tl::Beat &b = link()->chan(Channel(c)).accepted();
                if (b.msg && !m_tx[c].empty() && b.msg == m_tx[c].front().msg)
                {
                    TxEntry &e = m_tx[c].front();
                    if (e.beat == 0)
                        if (const Message *rec = m_owner->record(e.msg->id))
                            Logger::getLogger()->trace(*rec, busRole(c, m_memory_side), (uint32_t)oc, Logger::Phase::ENTER, (uint16_t)(e.dst & 0xff));
                    if (++e.beat >= e.msg->beats()) { m_tx[c].pop_front(); m_sent++; }
                }
            }
            else if (receives(oc))
            {
                const tl::Beat &b = link()->chan(Channel(c)).accepted();
                if (!b.msg) continue;
                if (!b.last()) { m_rx_busy[c] = true; continue; }
                m_rx_busy[c] = false;
                deliver(c, b.msg);
            }
        }
    }

    // The last beat is in: rebuild the Message (wire fields from the wire, Octopus fields from the
    // record), free the identifier its response completes, restore the handler's tag, log.
    void TLPort::deliver(int c, const tl::MsgPtr &m)
    {
        Message out; int src = -1, dst = -1;
        if (!m_owner->unstash(m->id, &out, &src, &dst))
            fatal("a message arrived without its Octopus record (tl id " + std::to_string(m->id) + ", " + m->describe() + ")");
        const int oc = toOctopus(c);
        const Channel ch = Channel(c);
        out.tl.channel = (uint8_t)oc; out.tl.opcode = m->opcode; out.tl.param = m->param; out.tl.size = m->size;
        out.tl.denied = m->denied; out.tl.source = (uint32_t)m->source; out.tl.sink = (uint32_t)m->sink;
        if (m->data_mode == tl::DataMode::Full && m->hasData())
        {
            std::vector<uint8_t> bytes(size_t(1) << m->size);
            for (uint64_t x = 0; x < bytes.size(); x++)
            {
                tl::LanePos p = tl::lanePos(m->address, m->w, m->address + x);
                bytes[x] = m->data[size_t(p.beat) * m->w + p.lane];
            }
            out.copy(bytes.data(), (uint16_t)bytes.size());
        }
        if (m_role == tl::Role::Master && ch == Channel::D)
        {
            // the D answers an A (AccessAck[Data], HintAck, Grant[Data]) or a Release (ReleaseAck)
            IdPool *pool = m->opcode == (uint8_t)tl::OpD::ReleaseAck ? poolFor(3) : poolFor(1);
            uint32_t tag = 0;
            if (!pool->release(m->source, &tag))
                fatal(std::string("a D ") + tl::opcodeName(ch, m->opcode) + " with source " + std::to_string(m->source) + ", which is not outstanding: " + describe(out));
            out.tl.source = tag;
        }
        else if (m_role == tl::Role::Slave && ch == Channel::E)
        {
            uint32_t tag = 0;
            if (!poolFor(4)->release(m->sink, &tag))
                fatal("a GrantAck with sink " + std::to_string(m->sink) + ", which is not outstanding: " + describe(out));
            out.tl.sink = tag;
        }
        m_rx[c].push_back(out);
        m_delivered++;
        Logger::Role role = busRole(c, m_memory_side);
        Logger::getLogger()->event(out.msg_id, role, ((uint32_t)m->opcode << 8) | (uint32_t)oc, Logger::Phase::EXIT);
        Logger::getLogger()->trace(out, role, (uint32_t)oc, Logger::Phase::EXIT, (uint16_t)(m_interface_id & 0xff));
        (void)src; (void)dst;
    }

    bool TLPort::idle() const
    {
        for (int c = 0; c < tl::NUM_CHANNELS; c++) if (!m_tx[c].empty() || m_rx_busy[c]) return false;
        return true;
    }

    tl::ProgressHints TLPort::hints() const
    {
        tl::ProgressHints h;
        h.latency = 1;
        h.queue_depth = (uint64_t)*std::max_element(m_depth.rx, m_depth.rx + tl::NUM_CHANNELS);
        h.is_master = m_role == tl::Role::Master;
        return h;
    }

    std::string TLPort::dump() const
    {
        std::ostringstream o;
        o << "[TL] port " << m_interface_id << " " << m_name << " " << tl::roleName(m_role) << (m_memory_side ? " memory-side" : "")
          << " delivered " << m_delivered << " sent " << m_sent << " refused " << m_refused << "\n";
        for (int c = 0; c < tl::NUM_CHANNELS; c++)
        {
            int oc = toOctopus(c);
            if (receives(oc)) o << "[TL]   rx " << channelName(oc) << " " << m_rx[c].size() << "/" << m_depth.rx[c] << (m_rx_busy[c] ? " +burst" : "") << "\n";
            if (sends(oc))
            {
                o << "[TL]   tx " << channelName(oc) << " " << m_tx[c].size() << "/" << m_depth.tx[c] << " reserved " << m_tx_reserved[c];
                if (!m_tx[c].empty()) o << " front " << m_tx[c].front().msg->describe() << " beat " << m_tx[c].front().beat;
                o << "\n";
            }
        }
        for (int oc : {1, 3, 4})
            if (const IdPool *p = poolFor(oc))
                o << "[TL]   ids (" << channelName(oc) << ") " << p->in_use << "/" << p->tag.size() << " in use, reserved " << p->reserved << "\n";
        return o.str();
    }
}
