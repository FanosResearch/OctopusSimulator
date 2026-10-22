/*
 * File  :      TLMemSlave.h
 * Memory over TileLink (docs/TileLink_Plan.md s5.3 I-B, step T8): the two ends of the TL-UH link
 * between an LLC bank's memory side and a memory controller. Both are thin CommunicationInterfaces
 * built by composition on the TLPort that the memory crossbar (a second TileLinkInterconnect) owns
 * at that endpoint; TLPort's contract is unchanged.
 *
 * The LLC <-> memory contract (shared with the TLC_LLC handler):
 *   read        K_MEM_READ,  A, Get (4),         param 0, size 6, no data
 *               -> K_FILL,     D, AccessAckData (1), size 6, the block, tl.source of the Get
 *   write-back  K_MEM_WRITE, A, PutFullData (0), param 0, size 6, the block
 *               -> K_MEM_WACK, D, AccessAck (0),     size 6, no data, tl.source of the Put
 *
 * TLMemSlave (the memory controller's interface; MainMemoryController and MCsimInterface do not
 * know TileLink and are unchanged):
 *   - receive: the port's A queue, in arrival order. A request is checked against the contract
 *     when the controller pops it (Get <-> K_MEM_READ without data, PutFullData <-> K_MEM_WRITE
 *     with the block; anything else is fatal) and enters the table of outstanding requests;
 *   - transmit: the controller pushes untagged K_FILL / K_MEM_WACK (MessageType DATA_RESPONSE).
 *     The adapter finds the request it answers, oldest first: a K_FILL the Get with its msg_id and
 *     line, a K_MEM_WACK the PutFullData of its line (the controllers stamp the acknowledgement
 *     with msg_id 0, docs/NoC_Memory_Plan.md M1; a non-zero msg_id must match too). It writes the
 *     D header from that request (opcode, size, source copied, param 0, not denied) and pushes a
 *     tagged copy; the entry leaves the table when the push succeeds. A response that matches no
 *     outstanding request is fatal (TileLink s5.4: a response answers exactly one request), as is
 *     an AccessAckData without its 2^size bytes;
 *   - admission is the controllers' own, as on the bus and the Network (spec s25.3): a read is
 *     accepted only with room in the controller's queue (the A queue then fills and the wire
 *     stalls), a write always; a response is pushed only when txFreeSlots(DATA_RESPONSE), the
 *     port's D queue, has a slot, else it waits in the controller's ordered output queue.
 * TLMemMaster (the LLC bank's memory side, its upper interface): everything is the port's, except
 *   that a push of a K_MEM_READ / K_MEM_WRITE is checked against the contract (fatal otherwise:
 *   the slave would answer it wrongly or never).
 *
 * Read-after-write detector (OCTOPUS_MEM_HAZARD, header/MemHazard.h; plan s5.3 I-B): the sender's
 * hooks (readSent / writeSent) are in TLMemMaster::pushMessage, when the port has taken the
 * message, as in BusInterface and NetworkInterface; the delivery hooks (readDelivered /
 * writeDelivered) in TLMemSlave::popFrontMessage. A TL-UH slave has one receive queue, A, served in
 * arrival order, so delivery order and pop order are the same; the controllers' own hooks
 * (arrival, performed, completed, forwarded) are unchanged. NET then counts reads that overtook an
 * older write of their line in the crossbar, MC reads completed inside the controller ahead of one,
 * with the meaning they have on the bus and the Network.
 */
#ifndef _TLMEMSLAVE_H
#define _TLMEMSLAVE_H
#include "CommunicationInterface.h"
#include "TLPort.h"
#include <cstdint>
#include <deque>
#include <string>

namespace octopus
{
    class TLMemSlave : public CommunicationInterface
    {
    public:
        // port: the memory controller's endpoint on the memory crossbar (a TL-UH slave link)
        explicit TLMemSlave(TLPort *port, unsigned block_bytes = 64);

        bool peekMessage(Message *out_msg) override;
        void popFrontMessage() override;
        bool pushMessage(Message &msg, uint64_t cycle = 0, MessageType type = MessageType::DATA_RESPONSE) override;
        bool pushMessage2RX(Message &msg, MessageType type = MessageType::REQUEST) override { return m_port->pushMessage2RX(msg, type); }
        bool canAcceptRX(MessageType type = MessageType::REQUEST) override { return m_port->canAcceptRX(type); }
        int  txResponseFreeSlots() override { return m_port->txFreeSlotsCh(4); }
        int  txFreeSlots(MessageType type) override { return m_port->txFreeSlots(type); }
        void txReserve(MessageType type, int n) override { m_port->txReserve(type, n); }
        void txRelease(MessageType type, int n) override { m_port->txRelease(type, n); }
        int  txFreeSlotsCh(int c) override { return m_port->txFreeSlotsCh(c); }
        void txReserveCh(int c, int n) override { m_port->txReserveCh(c, n); }
        void txReleaseCh(int c, int n) override { m_port->txReleaseCh(c, n); }
        int  txFreeIdsCh(int c) override { return m_port->txFreeIdsCh(c); }
        void txReserveIdsCh(int c, int n) override { m_port->txReserveIdsCh(c, n); }
        void txReleaseIdsCh(int c, int n) override { m_port->txReleaseIdsCh(c, n); }
        bool rollback(uint64_t, uint64_t, Message *) override { return false; }

        // ---- inspection (tests, the hang dump)
        TLPort *port() const { return m_port; }
        size_t outstanding() const { return m_pending.size(); }
        uint64_t gets() const { return m_gets; }           // Get requests handed to the controller
        uint64_t puts() const { return m_puts; }           // PutFullData requests handed to the controller
        uint64_t answered() const { return m_answered; }   // D responses taken by the port
        std::string dump() const;

    private:
        struct Pending { uint64_t msg_id, line; uint8_t opcode, size; uint32_t source; };
        [[noreturn]] void fatal(const std::string &what) const;
        uint64_t lineOf(uint64_t addr) const { return addr & ~uint64_t(m_block - 1); }

        TLPort *m_port;
        unsigned m_block;
        std::deque<Pending> m_pending;      // outstanding requests, in acceptance order
        Message m_peeked;
        bool m_have_peek = false;
        uint64_t m_gets = 0, m_puts = 0, m_answered = 0;
    };

    class TLMemMaster : public CommunicationInterface
    {
    public:
        // port: an LLC bank's memory port on the memory crossbar (a TL-UH master link)
        explicit TLMemMaster(TLPort *port, unsigned block_bytes = 64);

        bool peekMessage(Message *out_msg) override { return m_port->peekMessage(out_msg); }
        void popFrontMessage() override { m_port->popFrontMessage(); }
        bool pushMessage(Message &msg, uint64_t cycle = 0, MessageType type = MessageType::REQUEST) override;
        bool pushMessage2RX(Message &msg, MessageType type = MessageType::REQUEST) override { return m_port->pushMessage2RX(msg, type); }
        bool canAcceptRX(MessageType type = MessageType::REQUEST) override { return m_port->canAcceptRX(type); }
        int  txResponseFreeSlots() override { return m_port->txResponseFreeSlots(); }
        int  txFreeSlots(MessageType type) override { return m_port->txFreeSlots(type); }
        void txReserve(MessageType type, int n) override { m_port->txReserve(type, n); }
        void txRelease(MessageType type, int n) override { m_port->txRelease(type, n); }
        int  txFreeSlotsCh(int c) override { return m_port->txFreeSlotsCh(c); }
        void txReserveCh(int c, int n) override { m_port->txReserveCh(c, n); }
        void txReleaseCh(int c, int n) override { m_port->txReleaseCh(c, n); }
        int  txFreeIdsCh(int c) override { return m_port->txFreeIdsCh(c); }
        void txReserveIdsCh(int c, int n) override { m_port->txReserveIdsCh(c, n); }
        void txReleaseIdsCh(int c, int n) override { m_port->txReleaseIdsCh(c, n); }
        bool rollback(uint64_t, uint64_t, Message *) override { return false; }

        TLPort *port() const { return m_port; }

    private:
        TLPort *m_port;
        unsigned m_block;
    };
}

#endif /* _TLMEMSLAVE_H */
