/*
 * File  :      Arbiter.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 10, 2022
 */

#include "../../header/Arbiters/Arbiter.h"

namespace octopus
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

    bool Arbiter::electOldestOwned(vector<vector<Message> *> &buffers, int owner, Message *out_msg)
    {
        int best_buf = -1, best_idx = -1;
        uint64_t best_cycle = 0xFFFFFFFFFFFFFFFFull;

        for (int i = 0; i < (int)buffers.size(); i++)
        {
            // Within one buffer the first match is that owner's oldest (FIFO push).
            int idx = findMessage(*buffers[i], owner);
            if (idx != -1 && buffers[i]->at(idx).cycle < best_cycle)
            {
                best_buf = i;
                best_idx = idx;
                best_cycle = buffers[i]->at(idx).cycle;
            }
        }

        if (best_buf == -1)
            return false;

        out_msg->copy(buffers[best_buf]->at(best_idx));
        buffers[best_buf]->erase(buffers[best_buf]->begin() + best_idx);
        return true;
    }

    bool Arbiter::forceElect(uint64_t cycle_number, vector<vector<Message> *> &buffers, Message *out_msg)
    {
        return elect(cycle_number, buffers, out_msg);
    }
}