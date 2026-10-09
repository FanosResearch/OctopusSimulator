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
        int  rxFree(int cls) const { return m_eject_depth - (int)m_rx[cls].size() - m_reserved[cls]; }
        int  rxSize(int cls) const { return (int)m_rx[cls].size(); }
        int  reserved(int cls) const { return m_reserved[cls]; }
        int  id() const { return m_interface_id; }
        uint64_t rxDelivered() const { return m_delivered; }
        uint64_t rxPopped() const { return m_popped; }
        uint64_t pushRefused() const { return m_refused; }

    private:
        int classOfType(MessageType t) const;
        Network *m_net;
        int m_num_classes, m_inject_depth, m_eject_depth;
        bool m_priority;                        // rx_serving: priority (true) or alternate
        std::vector<std::deque<uint64_t>> m_inject;   // packet seqs per class
        std::vector<std::deque<Message>> m_rx;        // the endpoint's RX per class
        std::vector<int> m_reserved;
        int m_peeked = -1;                      // class peekMessage last returned
        int m_last_served = -1;                 // for alternate
        uint64_t m_delivered = 0, m_popped = 0, m_refused = 0;
    };
}}
#endif /* _NET_NETWORK_INTERFACE_H */
