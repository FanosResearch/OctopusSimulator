/*
 * File  :      FCFSArbiter.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 20, 2022
 */

#include "../../header/Arbiters/FCFSArbiter.h"

namespace ns3
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