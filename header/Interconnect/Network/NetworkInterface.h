/*
 * File  :      NetworkInterface.h
 * The endpoint side of the network: implements CommunicationInterface for one endpoint.
 * Specification: docs/NoC_Architecture_Spec.md §3.1, §7.
 */
#ifndef _NET_NETWORK_INTERFACE_H
#define _NET_NETWORK_INTERFACE_H
#include "CommunicationInterface.h"
#include "NetTypes.h"
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace octopus { namespace net
{
    class Network;

    class NetworkInterface : public CommunicationInterface
    {
    public:
        NetworkInterface(Network *net, int id, int num_classes, int inject_depth, int eject_depth, const std::string &rx_serving);

        // ---- the endpoint's contract (spec §3.1)
        bool peekMessage(Message *out_msg) override;
        void popFrontMessage() override;
        bool pushMessage(Message &msg, uint64_t cycle = 0, MessageType type = MessageType::REQUEST) override;
        bool pushMessage2RX(Message &msg, MessageType type = MessageType::REQUEST) override;
        bool canAcceptRX(MessageType type = MessageType::REQUEST) override;
        int  txResponseFreeSlots() override;
        int  txFreeSlots(MessageType type) override;
        void txReserve(MessageType type, int n) override;
        void txRelease(MessageType type, int n) override;
        int  txClass(MessageType type) const;
        // TileLink (num_classes 5 or 7, spec s6.3): admission per channel, on the class of (channel,
        // this endpoint's side: a memory-side endpoint sends on the outer link). The MessageType
        // calls above are fatal on such a network, and these on a 2- or 3-class one.
        int  txFreeSlotsCh(int tl_channel) override;
        void txReserveCh(int tl_channel, int n) override;
        void txReleaseCh(int tl_channel, int n) override;
        int  txClassCh(int tl_channel) const;
        bool rollback(uint64_t address, uint64_t mask, Message *out_msg) override;

        // ---- the network's side
        std::deque<uint64_t> &injectQueue(int cls) { return m_inject[cls]; }
        const std::deque<uint64_t> &injectQueue(int cls) const { return m_inject[cls]; }
        int  injectFree(int cls) const { return m_inject_depth - (int)m_inject[cls].size(); }
        // RX reservation (spec §8.4): one slot per packet, taken at VC allocation on the local port
        bool canReserve(int cls) const { return rxFree(cls) > 0; }
        void reserve(int cls) { m_reserved[cls]++; }
        // the tail flit arrived: fill the reserved slot
        void deliver(int cls, const Message &msg);
        // TileLink over the network (docs/TileLink_Plan.md s5.3 I-C): the boundary at this endpoint
        // (TLNetPort) sees every delivery, with the message before it enters the RX queue: the
        // message is accepted there in the TileLink sense, and the hook restores the identifiers the
        // endpoint's pools gave out. Unset on every Octopus network (nothing changes there).
        void setDeliverHook(std::function<void(int cls, Message &msg)> h) { m_on_deliver = std::move(h); }
        int  rxFree(int cls) const { return m_eject_depth - (int)m_rx[cls].size() - m_reserved[cls]; }
        int  rxSize(int cls) const { return (int)m_rx[cls].size(); }
        int  reserved(int cls) const { return m_reserved[cls]; }
        int  id() const { return m_interface_id; }
        uint64_t rxDelivered() const { return m_delivered; }
        uint64_t rxPopped() const { return m_popped; }
        uint64_t pushRefused() const { return m_refused; }
        // TX reservations (spec s14.2): held now, and taken / released in total, per class
        int  txReserved(int cls) const { return m_tx_reserved.empty() ? 0 : m_tx_reserved[cls]; }
        uint64_t txReservedTotal(int cls) const { return m_tx_taken.empty() ? 0 : m_tx_taken[cls]; }
        uint64_t txReleasedTotal(int cls) const { return m_tx_given.empty() ? 0 : m_tx_given[cls]; }

    private:
        int classOfType(MessageType t) const;
        int rxClassOf(const Message &msg, MessageType t) const;   // pushMessage2RX's class
        void reserveTx(int cls, int n);
        void releaseTx(int cls, int n);
        Network *m_net;
        int m_num_classes, m_inject_depth, m_eject_depth;
        bool m_priority;                        // rx_serving: priority (true) or alternate
        std::vector<std::deque<uint64_t>> m_inject;   // packet seqs per class
        std::vector<int> m_tx_reserved;               // slots reserved by admitted transitions, per class
        std::vector<uint64_t> m_tx_taken, m_tx_given; // slots reserved / released in total, per class (NetworkStats)
        std::vector<std::deque<Message>> m_rx;        // the endpoint's RX per class
        std::vector<int> m_reserved;
        int m_peeked = -1;                      // class peekMessage last returned
        int m_last_served = -1;                 // for alternate
        uint64_t m_delivered = 0, m_popped = 0, m_refused = 0;
        std::function<void(int, Message &)> m_on_deliver;
    };
}}
#endif /* _NET_NETWORK_INTERFACE_H */
