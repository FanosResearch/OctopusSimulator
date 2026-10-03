/*
 * File  :      WRRArbiter.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 21, 2022
 */

#include "../../header/Arbiters/WRRArbiter.h"

namespace octopus
{
    WRRArbiter::WRRArbiter(vector<int> *candidates_ids, int arbiter_period, int extra_slots) : Arbiter(candidates_ids, arbiter_period)
    {
        candidate_id = 0;
        m_extra_slots = extra_slots;
    }

    WRRArbiter::~WRRArbiter()
    {
    }

    bool WRRArbiter::coreElect(vector<vector<Message> *> &buffers, Message *out_msg)
    {
        // The elected owner's slot serves its oldest pending message wherever it
        // sits (see Arbiter::electOldestOwned) -- the FIFO premise of per-core RR.
        return electOldestOwned(buffers, (int)candidate_id, out_msg);
    }

    bool WRRArbiter::elect(uint64_t cycle_number, vector<vector<Message> *> &buffers, Message *out_msg)
    {
        for (int i = 0; i < (int)m_candidates_ids->size() + m_extra_slots; i++)
        {
            candidate_id = selectCandidate(cycle_number);
            if (coreElect(buffers, out_msg))
                return true;
        }
        return false;
    }

    bool WRRArbiter::forceElect(uint64_t cycle_number, vector<vector<Message> *> &buffers, Message *out_msg)
    {
        return coreElect(buffers, out_msg);
    }

    uint64_t WRRArbiter::selectCandidate(uint64_t cycle_number)
    {
        candidate_index = (candidate_index + 1) % (m_candidates_ids->size() + m_extra_slots);
        if (candidate_index >= m_candidates_ids->size()) return 0;
        else return m_candidates_ids->at(candidate_index);
    }
}