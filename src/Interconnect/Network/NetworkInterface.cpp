/*
 * File  :      NetworkInterface.cpp
 * Specification: docs/NoC_Architecture_Spec.md §3.1, §7.
 */
#include "../../../header/Interconnect/Network/NetworkInterface.h"
#include "../../../header/MemHazard.h"
#include <algorithm>
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

    // The MessageType map serves Octopus's own protocols (2 or 3 classes). A TileLink network (5 or 7
    // classes) is keyed on the channel (spec s6.3): a MessageType call there has no class, fatal.
    int NetworkInterface::classOfType(MessageType t) const
    {
        if (m_net->classifier().tileLink())
            fatal("endpoint " + std::to_string(m_interface_id) + ": a MessageType (" + std::to_string((int)t) + ") admission or RX call on a " + std::to_string(m_num_classes) +
                  "-class (TileLink) network; TileLink endpoints use the per-channel calls (txFreeSlotsCh, spec s6.3)");
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
        m_last_served = m_peeked;                 // the selector moves on every peek (a peek without a pop skips the class next time), as MeshInterface does
        out_msg->copy(m_rx[m_peeked].front());
        return true;
    }
    void NetworkInterface::popFrontMessage()
    {
        if (m_peeked < 0 || m_rx[m_peeked].empty()) return;
        m_rx[m_peeked].pop_front();
        m_peeked = -1; m_popped++;
    }

    // ---- injection (spec §3.1): atomic for every destination of a multicast
    bool NetworkInterface::pushMessage(Message &msg, uint64_t, MessageType)
    {
        if (msg.to.empty()) fatal("endpoint " + std::to_string(m_interface_id) + " pushed a message with no destination: " + Classifier::describe(msg, -1));
        msg.from = m_interface_id;          // every interface stamps the sender (the directory L1 tells home data from owner data by it)
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
            uint64_t seq = m_net->newPacket(msg, id(), msg.to[i], cls[i]);
            m_inject[cls[i]].push_back(seq);
        }
        if (msg.kind == Message::K_MEM_READ) MemHazard::readSent(msg.msg_id, msg.addr, msg.cycle);
        else if (msg.kind == Message::K_MEM_WRITE) MemHazard::writeSent(msg.addr, msg.msg_id, msg.cycle);
        return true;
    }
    // the class of an RX push: the MessageType's for Octopus's own protocols, the channel's for TileLink
    int NetworkInterface::rxClassOf(const Message &msg, MessageType type) const
    {
        if (msg.tl.channel == 0 && !m_net->classifier().tileLink()) return classOfType(type);
        if (msg.tl.channel == 0 || !m_net->classifier().tileLink())
            fatal("endpoint " + std::to_string(m_interface_id) + ": RX push of " + m_net->classifier().why(msg, m_interface_id) + " " + Classifier::describe(msg, m_interface_id));
        return txClassCh(msg.tl.channel);
    }
    bool NetworkInterface::pushMessage2RX(Message &msg, MessageType type)
    {
        // only the network delivers; an external caller gets the plain RX push
        int c = rxClassOf(msg, type);
        if ((int)m_rx[c].size() + m_reserved[c] >= m_eject_depth) return false;
        m_rx[c].push_back(msg); return true;
    }
    bool NetworkInterface::canAcceptRX(MessageType type) { return rxFree(classOfType(type)) > 0; }
    int  NetworkInterface::txResponseFreeSlots() { return injectFree(classOfType(MessageType::DATA_RESPONSE)); }
    int  NetworkInterface::txClass(MessageType type) const
    {
        classOfType(type);                       // fatal on a TileLink network
        return (type == MessageType::DATA_RESPONSE) ? m_num_classes - 1
             : (type == MessageType::SERVICE_REQUEST && m_num_classes == 3) ? CLASS_FORWARD : CLASS_REQUEST;
    }
    int  NetworkInterface::txFreeSlots(MessageType type)
    {
        if (m_tx_reserved.empty()) m_tx_reserved.assign(m_num_classes, 0);
        int cls = txClass(type);
        return std::max(0, injectFree(cls) - m_tx_reserved[cls]);
    }
    void NetworkInterface::reserveTx(int cls, int n)
    {
        if (m_tx_reserved.empty()) m_tx_reserved.assign(m_num_classes, 0);
        if (m_tx_taken.empty()) { m_tx_taken.assign(m_num_classes, 0); m_tx_given.assign(m_num_classes, 0); }
        m_tx_reserved[cls] += n; m_tx_taken[cls] += n;
    }
    void NetworkInterface::releaseTx(int cls, int n)
    {
        if (m_tx_reserved.empty()) m_tx_reserved.assign(m_num_classes, 0);
        if (m_tx_taken.empty()) { m_tx_taken.assign(m_num_classes, 0); m_tx_given.assign(m_num_classes, 0); }
        int &r = m_tx_reserved[cls]; r = std::max(0, r - n); m_tx_given[cls] += n;
    }
    void NetworkInterface::txReserve(MessageType type, int n) { reserveTx(txClass(type), n); }
    void NetworkInterface::txRelease(MessageType type, int n) { releaseTx(txClass(type), n); }

    // ---- TileLink admission per channel (spec s6.3): the class of the channel on this endpoint's
    // side; the endpoint's messages all travel on its side (the classifier refuses a message between
    // a memory-side endpoint and an inner one), so the class is exact
    int  NetworkInterface::txClassCh(int tl_channel) const
    {
        std::string why;
        int c = m_net->classifier().tlClass(tl_channel, m_net->isMemoryEndpoint(m_interface_id), &why);
        if (c < 0) fatal("endpoint " + std::to_string(m_interface_id) + ", channel " + Classifier::channelName(tl_channel) + ": " + why);
        return c;
    }
    int  NetworkInterface::txFreeSlotsCh(int tl_channel)
    {
        if (m_tx_reserved.empty()) m_tx_reserved.assign(m_num_classes, 0);
        int cls = txClassCh(tl_channel);
        return std::max(0, injectFree(cls) - m_tx_reserved[cls]);
    }
    void NetworkInterface::txReserveCh(int tl_channel, int n) { reserveTx(txClassCh(tl_channel), n); }
    void NetworkInterface::txReleaseCh(int tl_channel, int n) { releaseTx(txClassCh(tl_channel), n); }
    bool NetworkInterface::rollback(uint64_t, uint64_t, Message *) { return false; }   // as MeshInterface (its search is commented out)

    void NetworkInterface::deliver(int cls, const Message &msg)
    {
        if (m_reserved[cls] <= 0) fatal("endpoint " + std::to_string(m_interface_id) + ": delivery of class " + std::to_string(cls) + " without a reservation");
        m_reserved[cls]--;
        if ((int)m_rx[cls].size() + m_reserved[cls] >= m_eject_depth)
            fatal("endpoint " + std::to_string(m_interface_id) + ": RX of class " + std::to_string(cls) + " full at delivery despite the reservation");
        if (msg.kind == Message::K_MEM_WRITE) MemHazard::writeDelivered(msg.addr);
        else if (msg.kind == Message::K_MEM_READ) MemHazard::readDelivered(msg.msg_id, msg.addr);
        m_rx[cls].push_back(msg); m_delivered++;
    }
}}
