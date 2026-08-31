/*
 * File  :      TripleBusInterface.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 5, 2022
 */

#include "../../header/Interconnect/TripleBusInterface.h"

namespace octopus
{
    TripleBusInterface::TripleBusInterface(int id, int buffer_max_size) : BusInterface(id, buffer_max_size)
    {
    }

    // Invalidations (service bus) and coherence requests (request bus) must be
    // consumed in a single global order: a back-invalidation must NOT overtake a
    // coherence request already waiting in the RX. So service RX traffic is merged
    // into the request RX buffer at arrival (see pushMessage2RX) and peeked/popped
    // through the base request/response path -- no service-first priority.
    bool TripleBusInterface::peekMessage(Message *out_msg)
    {
        return BusInterface::peekMessage(out_msg);
    }

    void TripleBusInterface::popFrontMessage()
    {
        BusInterface::popFrontMessage();
    }

    bool TripleBusInterface::pushMessage(Message &msg, uint64_t cycle = 0, MessageType type)
    {
        if(type != MessageType::SERVICE_REQUEST)
            return BusInterface::pushMessage(msg, cycle, type);

        if (cycle != 0)
            msg.cycle = cycle;
        msg.from = m_interface_id;

        if ((int)m_tx_service_buffer.size() < m_buffer_max_size)
        {
            m_tx_service_buffer.push_back(msg);
            return true;
        }

        return false;
    }

    bool TripleBusInterface::pushMessage2RX(Message &msg, MessageType type)
    {
        if(type != MessageType::SERVICE_REQUEST)
            return BusInterface::pushMessage2RX(msg, type);

        // Back-invalidations are ALWAYS accepted (never held), but to preserve a
        // single global order with coherence requests they land in the SAME RX
        // buffer as requests, in arrival order -- not a separate priority buffer.
        // Bypasses the demand cap (service is unbounded, bounded upstream).
        m_rx_request_buffer.push_back(msg);
        return true;
    }

    bool TripleBusInterface::canAcceptRX(MessageType type)
    {
        if(type != MessageType::SERVICE_REQUEST)
            return BusInterface::canAcceptRX(type);

        return true; // back-invalidations always accepted
    }

    void TripleBusInterface::getCongregatedServiceBuffers(vector<CommunicationInterface *>& interfaces, vector<vector<Message>*>* buffers)
    {
        for(int i = 0; i < (int)interfaces.size(); i++)
            buffers->push_back(&((TripleBusInterface*)interfaces[i])->m_tx_service_buffer);
    }
}