/*
 * File  :      TLPort.h
 * One Octopus endpoint on a TileLink link (docs/TileLink_Plan.md s5.2): a CommunicationInterface on
 * the Octopus side (an L1's lower-level interface, an LLC's inner interface, later an LLC's memory
 * side and a memory controller) and a tl::Agent on the TileLink side, master or slave of exactly one
 * link of the TileLink kernel that TileLinkInterconnect owns.
 *
 * Receive side (the endpoint's RX):
 *   - one queue per channel the port receives on (master: B, D; slave: A, C, E), served highest
 *     priority first, E, D, C, B, A, by peekMessage/popFrontMessage, so a held B (a Probe stalled
 *     until a ReleaseAck) never blocks the D that unblocks it;
 *   - ready on a channel = that queue is not full. Its depth counts delivered messages plus the one
 *     whose burst is in progress: beat 0 accepted takes the slot, the following beats are always
 *     accepted (no stall in mid-burst);
 *   - a message is accepted, in the spec's sense, when its beats enter the queue; it is DELIVERED
 *     (peekable) once its LAST beat is accepted, because Octopus controllers consume whole
 *     messages. The controller's later pop is processing, not acceptance.
 * Transmit side:
 *   - pushMessage reads the channel from Message::tl.channel (fatal if 0: a TileLink interface
 *     carries only TileLink-tagged messages) and converts the Message into a tl::Msg: opcode,
 *     param, size, source, sink and denied from the header, the wire address (Message::addr rounded
 *     down to 2^size, as s4.5 requires), beats from the size and the link's w, the data bytes when
 *     Message::data != NULL (a data opcode without bytes travels in data mode None and arrives with
 *     data NULL, as it left);
 *   - one TX queue per channel the port sends on (master: A, C, E; slave: B, D), bounded by its
 *     depth; txFreeSlotsCh/txReserveCh/txReleaseCh count and reserve slots per channel for the
 *     admission contract (s5.2 points 1-3). The MessageType versions map onto channels
 *     (channelOf): REQUEST -> A, SERVICE_REQUEST -> B, DATA_RESPONSE -> D, then a channel the port
 *     does not use in that direction becomes its counterpart (A <-> B, C <-> D), so a caller that
 *     is not TileLink-aware still sees sensible numbers;
 *   - the Octopus fields the wire has no room for (msg_id, kind, owner, from, to, cycle, the
 *     complementary values, source, the unaligned address) travel in the interconnect's record of
 *     the message, keyed by tl::Msg::id, which every library agent keeps on the clones it forwards
 *     (Pipe.h cloneMsg); the receiving port restores them, so the Logger and the event trace
 *     correlate both ends. The header fields and the data come from the wire.
 * Identifier pools (s5.2, TileLink s5.4, s6.4):
 *   - a master allocates the source of every A message from a pool of 2^o (capped at 4096) and of
 *     every Release/ReleaseData on C from a second pool of the same size; a ProbeAck[Data] on C
 *     carries the source the handler gives it (the Probe's). A slave allocates the sink of every
 *     Grant/GrantData from a pool of 2^i. Allocation is lowest-free-first (deterministic);
 *   - the id is released when its response is accepted at this port: the D answering an A
 *     (AccessAck[Data], HintAck, Grant[Data]) or a Release (ReleaseAck), the E (GrantAck) answering a
 *     Grant; the handler's own value of the field (its "tag") is restored on that response, so a
 *     handler never sees the pool's numbering;
 *   - txFreeIdsCh/txReserveIdsCh/txReleaseIdsCh expose the pool a message on that channel draws
 *     from (A and C: sources; D: sinks), less reservations; a push that finds its pool empty is
 *     refused (the admission contract makes that an accounting bug).
 * Routing: A and C go by address (the Xbar's map), D by source, E by sink, all as the wire says; B
 *   (sent by a slave) goes to each id in Message::to, its source set to the base of that master's
 *   range in the crossbar's namespace (one tl::Msg per destination). For A, C, D and E a non-empty
 *   Message::to must name the endpoint the wire routes to, else fatal: an Octopus home map that the
 *   crossbar's address map does not express is refused at the first message.
 * On the Network (s5.3 I-C) TLNetPort derives from this class for the pools, the link view and the
 *   C-PERM registry (forEndpoint); its queues are the network interface's (TLNetPort.h).
 * Accounting (s5.3 I-A): every message whose last beat is accepted logs a Logger event and an
 *   OCTOPUS_TRACE EXIT record at the receiving port, and beat 0 accepted at the sending port logs the
 *   ENTER record: A and B as REQ_BUS, C, D and E as RESP_BUS, a memory-side port as MEM_BUS. The
 *   event's comp_id is (opcode << 8) | channel (1..5), which the Logger's TileLink columns read;
 *   the trace record's comp is the channel and its flags the destination endpoint, so octoviz pairs
 *   ENTER and EXIT per copy.
 */
#ifndef _TLPORT_H
#define _TLPORT_H
#include "CommunicationInterface.h"
#include "tilelink/Agent.h"
#include "tilelink/Msg.h"
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace octopus
{
    class TileLinkInterconnect;

    class TLPort : public CommunicationInterface, public tl::Agent
    {
    public:
        // Message::tl.channel is 1..5 = A..E; the library's tl::Channel is 0..4.
        static int  toTl(int octopus_channel) { return octopus_channel - 1; }
        static int  toOctopus(int tl_channel) { return tl_channel + 1; }
        static const char *channelName(int octopus_channel);   // "A".."E", "-" for 0

        struct Depths { int rx[tl::NUM_CHANNELS] = {4, 4, 4, 4, 4}; int tx[tl::NUM_CHANNELS] = {4, 4, 4, 4, 4}; };

        TLPort(TileLinkInterconnect *owner, int id, const std::string &name, tl::Role role, const Depths &d, bool memory_side);
        ~TLPort();

        // The port of an Octopus endpoint id (nullptr if that endpoint is not on a TileLink kernel): the
        // TL-C handlers report performed accesses through it.
        static TLPort *forEndpoint(int id);
        // C-PERM (TileLink CoherenceCheck, Table 29): the cache behind this master port served a load or a
        // store from its copy; checked against the permission observed on this port's link. Virtual: the
        // Network's TLNetPort reports the network's tick (its kernel is never stepped).
        virtual void accessPerformed(uint64_t addr, bool store);

        // ---- CommunicationInterface: the Octopus endpoint's side
        bool peekMessage(Message *out_msg) override;
        void popFrontMessage() override;
        bool pushMessage(Message &msg, uint64_t cycle = 0, MessageType type = MessageType::REQUEST) override;
        // Straight into an RX queue (no wire): Message::tl.channel if set, else channelOf(type).
        bool pushMessage2RX(Message &msg, MessageType type = MessageType::REQUEST) override;
        bool canAcceptRX(MessageType type = MessageType::REQUEST) override;
        int  txResponseFreeSlots() override { return txFreeSlots(MessageType::DATA_RESPONSE); }
        int  txFreeSlots(MessageType type) override { return txFreeSlotsCh(channelOf(type, true)); }
        void txReserve(MessageType type, int n) override { txReserveCh(channelOf(type, true), n); }
        void txRelease(MessageType type, int n) override { txReleaseCh(channelOf(type, true), n); }
        int  txFreeSlotsCh(int tl_channel) override;
        void txReserveCh(int tl_channel, int n) override;
        void txReleaseCh(int tl_channel, int n) override;
        int  txFreeIdsCh(int tl_channel) override;
        void txReserveIdsCh(int tl_channel, int n) override;
        void txReleaseIdsCh(int tl_channel, int n) override;
        bool rollback(uint64_t, uint64_t, Message *) override { return false; }

        // ---- tl::Agent: the TileLink side
        tl::Level capability(const tl::Link &, tl::Role) const override { return tl::Level::C; }
        void offer(uint64_t cycle) override;
        void accept(uint64_t cycle) override;
        void commit(uint64_t cycle) override;
        bool idle() const override;
        tl::ProgressHints hints() const override;

        // ---- inspection (tests, the hang dump)
        int  endpoint() const { return m_interface_id; }
        tl::Role role() const { return m_role; }
        bool memorySide() const { return m_memory_side; }
        tl::Link *link() const;                                  // the one link (fatal if not exactly one)
        bool sends(int tl_channel) const;                        // on this port's link, in its role
        bool receives(int tl_channel) const;
        int  channelOf(MessageType type, bool tx) const;         // 1..5
        int  rxSize(int tl_channel) const { return (int)m_rx[toTl(tl_channel)].size(); }
        int  txSize(int tl_channel) const { return (int)m_tx[toTl(tl_channel)].size(); }
        int  idsInUse(int tl_channel) const;                     // the pool a send on that channel draws from
        int  poolSize(int tl_channel) const;
        uint64_t delivered() const { return m_delivered; }
        uint64_t sent() const { return m_sent; }
        uint64_t refused() const { return m_refused; }
        std::string dump() const;

    protected:
        // shared with TLNetPort (the same port on the Network, docs/TileLink_Plan.md s5.3 I-C): the
        // identifier pools, the link and the role; the queues are the crossbar's only
        struct TxEntry { tl::MsgPtr msg; unsigned beat = 0; int dst = -1; };
        // One identifier pool: tag[k] = the handler's own value of the field for id k, -1 = free.
        struct IdPool
        {
            std::vector<int64_t> tag;
            int in_use = 0, reserved = 0;
            int alloc(uint32_t t);                               // -1 when empty
            bool release(uint64_t id, uint32_t *t);              // false when id was not allocated
            int free() const { return (int)tag.size() - in_use - reserved; }
        };
        IdPool *poolFor(int tl_channel);                         // A, C: sources (master); D: sinks (slave)
        const IdPool *poolFor(int tl_channel) const;
        void sizePools();
        void deliver(int c, const tl::MsgPtr &m);                // c: tl channel index
        [[noreturn]] void fatal(const std::string &what) const;

        TileLinkInterconnect *m_owner;
        tl::Role m_role;
        bool m_memory_side;
        Depths m_depth;
        mutable tl::Link *m_link = nullptr;
        std::deque<Message> m_rx[tl::NUM_CHANNELS];
        bool m_rx_busy[tl::NUM_CHANNELS] = {false, false, false, false, false};   // a burst in progress holds a slot
        std::deque<TxEntry> m_tx[tl::NUM_CHANNELS];
        int m_tx_reserved[tl::NUM_CHANNELS] = {0, 0, 0, 0, 0};
        IdPool m_src_a, m_src_c, m_sinks;
        bool m_pools_sized = false;
        int m_peeked = -1;                                       // tl channel index peekMessage returned
        uint64_t m_delivered = 0, m_sent = 0, m_refused = 0;
    };
}

#endif /* _TLPORT_H */
