/*
 * File  :      RRArbiter.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 21, 2022
 */

#include "../../header/Arbiters/RRArbiter.h"

namespace octopus
{
    RRArbiter::RRArbiter(vector<int> *candidates_ids, int arbiter_period) : Arbiter(candidates_ids, arbiter_period)
    {
        candidate_id = 0;
    }

    RRArbiter::~RRArbiter()
    {
    }

    bool RRArbiter::coreElect(vector<vector<Message> *> &buffers, Message *out_msg)
    {
        // The elected owner's slot serves its oldest pending message wherever it
        // sits (see Arbiter::electOldestOwned) -- the FIFO premise of per-core RR.
        return electOldestOwned(buffers, (int)candidate_id, out_msg);
    }

    bool RRArbiter::elect(uint64_t cycle_number, vector<vector<Message> *> &buffers, Message *out_msg)
    {
        for (int i = 0; i < (int)m_candidates_ids->size(); i++)
        {
            candidate_id = selectCandidate(cycle_number);
            if (coreElect(buffers, out_msg))
                return true;
        }
        return false;
    }

    bool RRArbiter::forceElect(uint64_t cycle_number, vector<vector<Message> *> &buffers, Message *out_msg)
    {
        return coreElect(buffers, out_msg);
    }

    uint64_t RRArbiter::selectCandidate(uint64_t cycle_number)
    {
        candidate_index = (candidate_index + 1) % m_candidates_ids->size();

        return m_candidates_ids->at(candidate_index);
    }
}