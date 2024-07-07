/*
 * File  :      NoCInterface.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 27, 2023
 */

#include "../../header/Interconnect/NoCInterface.h"

namespace octopus
{
    NoCInterface::NoCInterface(int id, int buffer_max_size, vector<int>& link_ids, vector<vector<int>>& connection_mat, vector<int>& switch_ids) 
        : MeshInterface(id, buffer_max_size, link_ids)
    {
        for(int id : link_ids)
        {
            m_routing_table[id] = id;
            if(std::find(switch_ids.begin(), switch_ids.end(), id) != switch_ids.end())
            {
                for(auto vec : connection_mat)
                {
                    if(vec[0] == id)
                    {
                        for(auto itr = vec.begin()+1; itr != vec.end(); itr++) //add reachable ids from the switch to the routing_table
                        {
                            if(*itr == m_interface_id)
                                continue;
                            m_routing_table[*itr] = id;
                        }
                    }
                }
            }
        }
    }

    bool NoCInterface::pushMessage(Message &msg, uint64_t cycle, MessageType type) //Ignore type
    {
        if (cycle != 0)
            msg.cycle = cycle;
        msg.from = m_interface_id;
        return noc_pushMessage(msg);
    }

    bool NoCInterface::noc_pushMessage(Message &msg)
    {
        if(msg.to.empty())
        {
            cout << "Error missing \"to\" vector\n";
            exit(0);
        }

        if (msg.data == NULL && (int)m_tx_request_buffer.size() < m_buffer_max_size)
        {
            for(int to_id : msg.to)
            {
                int link_id = m_routing_table[to_id];
                for(int i = 0; i < m_link_ids.size(); i++)
                {
                    if(m_link_ids[i] == link_id)
                    {
                        Message msg_copy = msg;
                        msg_copy.to.clear();            // remove extra entries in "to" vector 
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
                int link_id = m_routing_table[to_id];
                for(int i = 0; i < m_link_ids.size(); i++)
                {
                    if(m_link_ids[i] == link_id)
                    {
                        Message msg_copy = msg;
                        msg_copy.to.clear();        // remove extra entries in "to" vector
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
}