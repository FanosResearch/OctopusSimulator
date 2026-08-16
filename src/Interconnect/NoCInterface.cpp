/*
 * File  :      NoCInterface.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 27, 2023
 */

#include "../../header/Interconnect/NoCInterface.h"
#include <queue>

namespace ns3
{
    // ---- Original one-hop routing-table builder (kept for reference) ----
    // NoCInterface::NoCInterface(int id, int buffer_max_size, vector<int>& link_ids, vector<vector<int>>& connection_mat, vector<int>& switch_ids)
    //     : MeshInterface(id, buffer_max_size, link_ids)
    // {
    //     for(int id : link_ids)
    //     {
    //         m_routing_table[id] = id;
    //         if(std::find(switch_ids.begin(), switch_ids.end(), id) != switch_ids.end())
    //         {
    //             for(auto vec : connection_mat)
    //             {
    //                 if(vec[0] == id)
    //                 {
    //                     for(auto itr = vec.begin()+1; itr != vec.end(); itr++) //add reachable ids from the switch to the routing_table
    //                     {
    //                         if(*itr == m_interface_id)
    //                             continue;
    //                         m_routing_table[*itr] = id;
    //                     }
    //                 }
    //             }
    //         }
    //     }
    // }

    NoCInterface::NoCInterface(int id, int buffer_max_size, vector<int>& link_ids,
                               vector<vector<int>>& connection_mat, vector<int>& switch_ids)
        : MeshInterface(id, buffer_max_size, link_ids)
    {
        // Build an adjacency map from the connection matrix so we can BFS
        // across the entire graph (not just one hop). Required for multi-hop
        // NoC topologies such as a 2D mesh, while remaining backwards
        // compatible with the original single-switch NoC.csv configuration.
        map<int, vector<int>> adj;
        for(auto& vec : connection_mat)
        {
            int node = vec[0];
            for(auto itr = vec.begin() + 1; itr != vec.end(); ++itr)
                adj[node].push_back(*itr);
        }

        // BFS from this interface. For every reachable destination, record
        // the FIRST hop (a direct neighbor already in link_ids) on the
        // shortest path. Invariant: m_routing_table[dest] must be a value in
        // link_ids so that noc_pushMessage() can match it against a tx buffer.
        map<int, int> first_hop;
        map<int, bool> visited;
        std::queue<int> q;

        visited[m_interface_id] = true;
        for(int neighbor : link_ids)
        {
            if(visited[neighbor]) continue; // defensive: skip duplicates
            first_hop[neighbor] = neighbor;
            visited[neighbor] = true;
            q.push(neighbor);
        }

        // Only switches forward; non-switch endpoints (cores, LLC) terminate
        // a BFS branch. This matches the original one-hop builder's intent.
        while(!q.empty())
        {
            int curr = q.front();
            q.pop();

            bool curr_is_switch = std::find(switch_ids.begin(), switch_ids.end(), curr)
                                  != switch_ids.end();
            if(!curr_is_switch)
                continue;

            int curr_first_hop = first_hop[curr];
            for(int next : adj[curr])
            {
                if(visited[next]) continue;
                visited[next] = true;
                first_hop[next] = curr_first_hop;
                q.push(next);
            }
        }

        for(auto& kv : first_hop)
        {
            if(kv.first == m_interface_id) continue;
            m_routing_table[kv.first] = kv.second;
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

        // Multi-hop routing fix: stamp msg.prev_hop with THIS interface's id
        // at every hop. NoCController::step() uses prev_hop (not from) to
        // decide link direction. Updating `from` here would break the
        // protocol layer, which uses msg.from to detect whether data came
        // from the LLC vs an L1 owner (see MSIDirectory::readEvent).
        msg.prev_hop = m_interface_id;

        // Reachability guard: with multi-hop routing, a config bug that omits
        // a link would otherwise cause the lookup below to silently drop the
        // message. Fail loudly instead so misconfigurations are caught early.
        for(int to_id : msg.to)
        {
            if(m_routing_table.find(to_id) == m_routing_table.end())
            {
                cout << "NoCInterface " << m_interface_id
                     << ": no route to destination " << to_id << endl;
                exit(0);
            }
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