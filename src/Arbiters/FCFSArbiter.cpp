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
        m_respect_order = false;
    }

    FCFSArbiter::~FCFSArbiter()
    {
    }

    bool FCFSArbiter::elect(uint64_t cycle_number, vector<vector<Message> *> &buffers, Message *out_msg)
    {
        uint64_t min_cycle = 0xFFFFFFFFFFFFFFFF;
        int min_buffer_index = -1;
        int j;

        for (int i = 0; i < (int)buffers.size(); i++)
        {
            for(j = 0; j < (int)buffers[i]->size(); j++)
            {
                if (!buffers[i]->empty() && buffers[i]->at(j).cycle < min_cycle)
                {
                    if(!(m_respect_order && RROFArbiter::isDepender(buffers[i]->at(j))))
                    {
                        min_buffer_index = i;
                        min_cycle = buffers[i]->at(0).cycle;
                        break;
                    }
                }
            }
            if(j != (int)buffers[i]->size())
                break;
        }
        if (min_buffer_index != -1)
        {
            out_msg->copy(buffers[min_buffer_index]->at(j));
            buffers[min_buffer_index]->erase(buffers[min_buffer_index]->begin() + j);
            return true;
        }

        return false;
    }

    void FCFSArbiter::setRespectOrder()
    {
        m_respect_order = true;
    }
}