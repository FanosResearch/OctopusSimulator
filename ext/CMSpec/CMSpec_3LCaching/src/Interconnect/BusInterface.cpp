/*
 * File  :      BusInterface.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 5, 2022
 */

#include "../../header/Interconnect/BusInterface.h"

namespace ns3
{
    BusInterface::BusInterface(int id, int buffer_max_size) : CommunicationInterface(id)
    {
        m_buffer_selector = -1;
        m_buffer_max_size = buffer_max_size;
    }

    bool BusInterface::peekMessage(Message *out_msg)
    {
        for (int i = 0; i < 2; i++) // iterate twice to check both buffers
        {
            if (!m_rx_request_buffer.empty() && m_buffer_selector != 0)
            {
                out_msg->copy(m_rx_request_buffer[0]);
                m_buffer_selector = 0;
                return true;
            }

            if (!m_rx_response_buffer.empty() && m_buffer_selector != 1)
            {
                out_msg->copy(m_rx_response_buffer[0]);
                m_buffer_selector = 1;
                return true;
            }
            m_buffer_selector = -1;
        }

        return false;
    }

    void BusInterface::popFrontMessage()
    {
        if (m_buffer_selector == 0)
            m_rx_request_buffer.erase(m_rx_request_buffer.begin());
        else if (m_buffer_selector == 1)
            m_rx_response_buffer.erase(m_rx_response_buffer.begin());
    }

    bool BusInterface::pushMessage(Message &msg, uint64_t cycle = 0, MessageType type)
    {
        if (cycle != 0)
            msg.cycle = cycle;

        if (type == MessageType::REQUEST && (int)m_tx_request_buffer.size() < m_buffer_max_size)
        {
            m_tx_request_buffer.push_back(msg);
            return true;
        }
        else if (type == MessageType::DATA_RESPONSE && (int)m_tx_response_buffer.size() < m_buffer_max_size)
        {
            m_tx_response_buffer.push_back(msg);
            return true;
        }

        return false;
    }

    bool BusInterface::pushMessage2RX(Message &msg, MessageType type)
    {
        if (type == MessageType::REQUEST && (int)m_rx_request_buffer.size() < m_buffer_max_size)
        {
            m_rx_request_buffer.push_back(msg);
            return true;
        }
        else if (type == MessageType::DATA_RESPONSE && (int)m_rx_response_buffer.size() < m_buffer_max_size)
        {
            m_rx_response_buffer.push_back(msg);
            return true;
        }

        return false;
    }

    void BusInterface::getCongregatedBuffers(vector<CommunicationInterface *>& interfaces, bool request_buffer, vector<vector<Message>*>* buffers)
    {
        for(int i = 0; i < (int)interfaces.size(); i++)
        {
            if(request_buffer)
                buffers->push_back(&((BusInterface*)interfaces[i])->m_tx_request_buffer);
            else
                buffers->push_back(&((BusInterface*)interfaces[i])->m_tx_response_buffer);
        }
    }

    bool BusInterface::rollback(uint64_t address, uint64_t mask, Message *out_msg)
    {
        for(int i = 0; i < (int)m_tx_request_buffer.size(); i++)
        {
            uint64_t comp1 = address & ~(mask - 1);
            uint64_t comp2 = m_tx_request_buffer[i].addr & ~(mask - 1);
            if(comp1 == comp2)
            {
                *out_msg = m_tx_request_buffer[i];
                m_tx_request_buffer.erase(m_tx_request_buffer.begin() + i);
                return true;
            }
        }

        for(int i = 0; i < (int)m_tx_response_buffer.size(); i++)
        {
            uint64_t comp1 = address & ~(mask - 1);
            uint64_t comp2 = m_tx_response_buffer[i].addr & ~(mask - 1);
            if(comp1 == comp2)
            {
                *out_msg = m_tx_response_buffer[i];
                m_tx_response_buffer.erase(m_tx_response_buffer.begin() + i);
                return true;
            }
        }
        
        return false;
    }
}