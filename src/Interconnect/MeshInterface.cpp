/*
 * File  :      MeshInterface.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 27, 2023
 */

#include "../../header/Interconnect/MeshInterface.h"

namespace octopus
{
    MeshInterface::MeshInterface(int id, int buffer_max_size, vector<int>& link_ids) : CommunicationInterface(id)
    {
        m_buffer_selector = -1;
        m_buffer_max_size = buffer_max_size;
        m_link_ids = link_ids;

        m_tx_request_buffer.reserve(m_link_ids.size());
        m_tx_data_buffer.reserve(m_link_ids.size());
        for(int i = 0; i < m_link_ids.size(); i++)
        {
            m_tx_request_buffer.push_back(vector<Message>());
            m_tx_data_buffer.push_back(vector<Message>());
        }
    }

    bool MeshInterface::peekMessage(Message *out_msg)
    {
        for (int i = 0; i < 2; i++) // iterate twice to check both buffers
        {
            if (!m_rx_request_buffer.empty() && m_buffer_selector != 0)
            {
                out_msg->copy(m_rx_request_buffer[0]);
                m_buffer_selector = 0;
                return true;
            }

            if (!m_rx_data_buffer.empty() && m_buffer_selector != 1)
            {
                out_msg->copy(m_rx_data_buffer[0]);
                m_buffer_selector = 1;
                return true;
            }
            m_buffer_selector = -1;
        }

        return false;
    }

    void MeshInterface::popFrontMessage()
    {
        if (m_buffer_selector == 0)
            m_rx_request_buffer.erase(m_rx_request_buffer.begin());
        else if (m_buffer_selector == 1)
            m_rx_data_buffer.erase(m_rx_data_buffer.begin());
    }

    bool MeshInterface::pushMessage(Message &msg, uint64_t cycle = 0, MessageType type) //Ignore type
    {
        if (cycle != 0)
            msg.cycle = cycle;
        msg.from = m_interface_id;
        
        if(msg.to.empty())
        {
            cout << "Error missing \"to\" vector\n";
            exit(0);
        }

        if (msg.data == NULL && (int)m_tx_request_buffer.size() < m_buffer_max_size)
        {
            for(int to_id : msg.to)
            {
                for(int i = 0; i < m_link_ids.size(); i++)
                {
                    if(m_link_ids[i] == to_id)
                    {
                        Message msg_copy = msg;
                        msg_copy.to.clear();
                        msg_copy.to.push_back(to_id);
                        m_tx_request_buffer[i].push_back(msg_copy);
                        break;
                    }
                }
            }
            return true;
        }
        else if (msg.data != NULL && (int)m_tx_data_buffer.size() < m_buffer_max_size)
        {
            for(int to_id : msg.to)
            {
                for(int i = 0; i < m_link_ids.size(); i++)
                {
                    if(m_link_ids[i] == to_id)
                    {
                        Message msg_copy = msg;
                        msg_copy.to.clear();
                        msg_copy.to.push_back(to_id);
                        m_tx_data_buffer[i].push_back(msg_copy);
                        break;
                    }
                }
            }
            return true;
        }

        return false;
    }

    bool MeshInterface::pushMessage2RX(Message &msg, MessageType type) //Ignore type
    {
        if (msg.data == NULL && (int)m_rx_request_buffer.size() < m_buffer_max_size)
        {
            m_rx_request_buffer.push_back(msg);
            return true;
        }
        else if (msg.data != NULL && (int)m_rx_data_buffer.size() < m_buffer_max_size)
        {
            m_rx_data_buffer.push_back(msg);
            return true;
        }

        return false;
    }

    void MeshInterface::getCongregatedBuffers(vector<CommunicationInterface *>& interfaces, bool request_buffer, vector<vector<Message>*>* buffers)
    {
        if(interfaces.size() != 2)
        {
            cout << "interfaces are more than 2\n";
            exit(0);
        }
        for(int j = 0; j < 2; j++)
        {
            MeshInterface* interface = (MeshInterface*)interfaces[j];
            for(int i = 0; i < interface->m_link_ids.size(); i++)
            {
                if(*interfaces[(j+1)%2] == interface->m_link_ids[i])
                {
                    if(request_buffer)
                        buffers->push_back(&interface->m_tx_request_buffer[i]);
                    else
                        buffers->push_back(&interface->m_tx_data_buffer[i]);
                    break;
                }
            }
        }
    }

    bool MeshInterface::rollback(uint64_t address, uint64_t mask, Message *out_msg)
    {
        // for(int i = 0; i < (int)m_tx_request_buffer.size(); i++)
        // {
        //     uint64_t comp1 = address & ~(mask - 1);
        //     uint64_t comp2 = m_tx_request_buffer[i].addr & ~(mask - 1);
        //     if(comp1 == comp2)
        //     {
        //         *out_msg = m_tx_request_buffer[i];
        //         m_tx_request_buffer.erase(m_tx_request_buffer.begin() + i);
        //         return true;
        //     }
        // }

        // for(int i = 0; i < (int)m_tx_data_buffer.size(); i++)
        // {
        //     uint64_t comp1 = address & ~(mask - 1);
        //     uint64_t comp2 = m_tx_data_buffer[i].addr & ~(mask - 1);
        //     if(comp1 == comp2)
        //     {
        //         *out_msg = m_tx_data_buffer[i];
        //         m_tx_data_buffer.erase(m_tx_data_buffer.begin() + i);
        //         return true;
        //     }
        // }
        
        return false;
    }
}