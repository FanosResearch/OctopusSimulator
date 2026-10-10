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

    bool TripleBusInterface::peekMessage(Message *out_msg)
    {
        // A back-invalidation (service channel) is handed out only if the bus
        // delivered it BEFORE the request at the head of the request RX. Giving the
        // service channel unconditional priority let a controller that was one
        // message behind process the LLC's Own_Invalidation ahead of a GetM that
        // every other snooper had already seen -- a bus-order violation. (Seen on
        // cacheb01 under a perfect LLC: the L1 went IM_d -> IM_dI on the INV, the
        // LLC re-fetched for the GetM, made that L1 the owner, and the L1's returned
        // data parked the LLC in IorS_a forever.) Responses are not ordered against
        // requests here: they are exempt from ordering by design so that a full
        // queue of stalled requests is always drainable.
        if (!m_rx_service_buffer.empty() &&
            (m_rx_request_buffer.empty() || m_rx_service_seq[0] < m_rx_request_seq[0]))
        {
            out_msg->copy(m_rx_service_buffer[0]);
            m_service_selected = true;
            return true;
        }
        m_service_selected = false;
        return BusInterface::peekMessage(out_msg);
    }

    void TripleBusInterface::popFrontMessage()
    {
        if (m_service_selected)
        {
            m_rx_service_buffer.erase(m_rx_service_buffer.begin());
            m_rx_service_seq.erase(m_rx_service_seq.begin());
            m_service_selected = false;
        }
        else
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

        // Back-invalidations are service traffic -- ALWAYS accepted, never held.
        m_rx_service_buffer.push_back(msg);
        m_rx_service_seq.push_back(++s_rx_delivery_seq);
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