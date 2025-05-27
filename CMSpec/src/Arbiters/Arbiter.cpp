/*
 * File  :      Arbiter.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 10, 2022
 */

#include "../../header/Arbiters/Arbiter.h"

namespace ns3
{
    Arbiter::Arbiter(vector<int> *candidates_ids, int arbiter_period)
    {
        m_candidates_ids = candidates_ids;
        m_arbiter_period = arbiter_period;
        
        candidate_index = 0;
    }

    Arbiter::~Arbiter()
    {
    }

    int Arbiter::findMessage(vector<Message>& buffer, int id)
    {
        for(int i = 0; i < (int)buffer.size(); i++)
        {
            if(buffer[i].owner == id)
                return i;
        }

        return -1;
    }

    bool Arbiter::forceElect(uint64_t cycle_number, vector<vector<Message> *> &buffers, Message *out_msg)
    {
        return elect(cycle_number, buffers, out_msg);
    }
}