/*
 * File  :      TripleBusInterface.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 5, 2022
 */

#include "../../header/Interconnect/TripleBusInterface.h"

namespace ns3
{
    TripleBusInterface::TripleBusInterface(int id, int buffer_max_size) : BusInterface(id, buffer_max_size)
    {
    }

    bool TripleBusInterface::peekMessage(Message *out_msg)
    {
        if (!m_rx_service_buffer.empty())
        {
            out_msg->copy(m_rx_service_buffer[0]);
            return true;
        }
        return BusInterface::peekMessage(out_msg);
    }

    void TripleBusInterface::popFrontMessage()
    {
        if (!m_rx_service_buffer.empty())
            m_rx_service_buffer.erase(m_rx_service_buffer.begin());
        else
            BusInterface::popFrontMessage();
    }

    bool TripleBusInterface::pushMessage(Message &msg, uint64_t cycle = 0, MessageType type)
    {
        if(type != MessageType::SERVICE_REQUEST)
            return BusInterface::pushMessage(msg, cycle, type);

        if (cycle != 0)
            msg.cycle = cycle;

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

        if ((int)m_rx_service_buffer.size() < m_buffer_max_size)
        {
            m_rx_service_buffer.push_back(msg);
            return true;
        }

        return false;
    }

    void TripleBusInterface::getCongregatedServiceBuffers(vector<CommunicationInterface *>& interfaces, vector<vector<Message>*>* buffers)
    {
        for(int i = 0; i < (int)interfaces.size(); i++)
            buffers->push_back(&((TripleBusInterface*)interfaces[i])->m_tx_service_buffer);
    }
}