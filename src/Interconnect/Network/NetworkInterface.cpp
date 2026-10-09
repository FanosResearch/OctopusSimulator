/*
 * File  :      NetworkInterface.cpp
 * Specification: docs/NoC_Architecture_Spec.md §3.1, §7.
 */
#include "../../../header/Interconnect/Network/NetworkInterface.h"
#include "../../../header/Interconnect/Network/Network.h"

namespace octopus { namespace net
{
    NetworkInterface::NetworkInterface(Network *net, int id, int num_classes, int inject_depth, int eject_depth, const std::string &rx_serving)
        : CommunicationInterface(id), m_net(net), m_num_classes(num_classes), m_inject_depth(inject_depth), m_eject_depth(eject_depth),
          m_priority(rx_serving == "priority"), m_inject(num_classes), m_rx(num_classes), m_reserved(num_classes, 0)
    {
        if (rx_serving != "priority" && rx_serving != "alternate")
            throw ConfigError("rx_serving must be priority or alternate, got '" + rx_serving + "'");
    }

    int NetworkInterface::classOfType(MessageType t) const
    {
        if (m_num_classes == 2) return t == MessageType::DATA_RESPONSE ? 1 : 0;
        switch (t)
        {
            case MessageType::DATA_RESPONSE: return CLASS_RESPONSE;
            case MessageType::SERVICE_REQUEST: return CLASS_FORWARD;
            default: return CLASS_REQUEST;
        }
    }

    // ---- RX serving (spec §7.4)
    bool NetworkInterface::peekMessage(Message *out_msg)
    {
        m_peeked = -1;
        if (m_priority)
        {
            for (int c : m_net->cfg().classPriority())
                if (!m_rx[c].empty()) { m_peeked = c; break; }
        }
        else
        {
            // alternate: the next non-empty class after the one served last, cyclically; with two
            // classes this is exactly MeshInterface's request/data alternation
            for (int k = 1; k <= m_num_classes; k++)
            {
                int c = (m_last_served + k) % m_num_classes;
                if (!m_rx[c].empty()) { m_peeked = c; break; }
            }
        }
        if (m_peeked < 0) return false;
        out_msg->copy(m_rx[m_peeked].front());
        return true;
    }
    void NetworkInterface::popFrontMessage()
    {
        if (m_peeked < 0 || m_rx[m_peeked].empty()) return;
        m_rx[m_peeked].pop_front();
        m_last_served = m_peeked; m_peeked = -1; m_popped++;
    }

    // ---- injection (spec §3.1): atomic for every destination of a multicast
    bool NetworkInterface::pushMessage(Message &msg, uint64_t, MessageType)
    {
        if (msg.to.empty()) fatal("endpoint " + std::to_string(m_interface_id) + " pushed a message with no destination: " + Classifier::describe(msg, -1));
        // class per destination (the fallback rules look at the destination)
        std::vector<int> cls; std::vector<int> need(m_num_classes, 0);
        for (int dst : msg.to)
        {
            if (!m_net->topology().isEndpoint(dst))
                fatal("endpoint " + std::to_string(m_interface_id) + " addressed " + std::to_string(dst) + ", which is not an endpoint: " + Classifier::describe(msg, dst));
            int c = m_net->classOf(msg, dst); cls.push_back(c); need[c]++;
        }
        for (int c = 0; c < m_num_classes; c++)
            if (need[c] > injectFree(c)) { m_refused++; return false; }
        for (size_t i = 0; i < msg.to.size(); i++)
        {
            uint64_t seq = m_net->newPacket(msg, msg.to[i], cls[i]);
            m_inject[cls[i]].push_back(seq);
        }
        return true;
    }
    bool NetworkInterface::pushMessage2RX(Message &msg, MessageType type)
    {
        // only the network delivers; an external caller gets the plain RX push
        int c = classOfType(type);
        if ((int)m_rx[c].size() + m_reserved[c] >= m_eject_depth) return false;
        m_rx[c].push_back(msg); return true;
    }
    bool NetworkInterface::canAcceptRX(MessageType type) { return rxFree(classOfType(type)) > 0; }
    int  NetworkInterface::txResponseFreeSlots() { return injectFree(m_num_classes - 1); }
    bool NetworkInterface::rollback(uint64_t, uint64_t, Message *) { return false; }   // as MeshInterface (its search is commented out)

    void NetworkInterface::deliver(int cls, const Message &msg)
    {
        if (m_reserved[cls] <= 0) fatal("endpoint " + std::to_string(m_interface_id) + ": delivery of class " + std::to_string(cls) + " without a reservation");
        m_reserved[cls]--;
        if ((int)m_rx[cls].size() + m_reserved[cls] >= m_eject_depth)
            fatal("endpoint " + std::to_string(m_interface_id) + ": RX of class " + std::to_string(cls) + " full at delivery despite the reservation");
        m_rx[cls].push_back(msg); m_delivered++;
    }
}}
