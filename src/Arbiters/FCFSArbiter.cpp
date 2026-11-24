/*
 * File  :      FCFSArbiter.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 20, 2022
 */

#include "../../header/Arbiters/FCFSArbiter.h"

namespace octopus
{
    FCFSArbiter::FCFSArbiter(vector<int> *candidates_ids, int arbiter_period) : Arbiter(candidates_ids, arbiter_period)
    {
    }

    FCFSArbiter::~FCFSArbiter()
    {
    }

    bool FCFSArbiter::elect(uint64_t cycle_number, vector<vector<Message> *> &buffers, Message *out_msg)
    {
        uint64_t min_cycle = 0xFFFFFFFFFFFFFFFF;
        int min_buffer_index = -1;

        if (m_eligible)
        {
            // each sender's oldest eligible message (see Arbiter::m_eligible)
            int min_index = -1;
            for (int i = 0; i < (int)buffers.size(); i++)
            {
                int j = firstEligible(*buffers[i]);
                if (j >= 0 && buffers[i]->at(j).cycle < min_cycle)
                {
                    min_buffer_index = i;
                    min_index = j;
                    min_cycle = buffers[i]->at(j).cycle;
                }
            }
            if (min_buffer_index == -1)
                return false;
            out_msg->copy(buffers[min_buffer_index]->at(min_index));
            buffers[min_buffer_index]->erase(buffers[min_buffer_index]->begin() + min_index);
            return true;
        }

        for (int i = 0; i < (int)buffers.size(); i++)
        {
            if (!buffers[i]->empty() && buffers[i]->at(0).cycle < min_cycle)
            {
                min_buffer_index = i;
                min_cycle = buffers[i]->at(0).cycle;
            }
        }

        if (min_buffer_index != -1)
        {
            out_msg->copy(buffers[min_buffer_index]->at(0));
            buffers[min_buffer_index]->erase(buffers[min_buffer_index]->begin());
            return true;
        }

        return false;
    }
}