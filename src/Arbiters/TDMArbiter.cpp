/*
 * File  :      TDMArbiter.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 10, 2022
 */

#include "../../header/Arbiters/TDMArbiter.h"

namespace octopus
{
    TDMArbiter::TDMArbiter(vector<int> *candidates_ids, int arbiter_period) : Arbiter(candidates_ids, arbiter_period)
    {
    }

    TDMArbiter::~TDMArbiter()
    {
    }

    bool TDMArbiter::coreElect(uint64_t cycle_number, vector<vector<Message> *> &buffers, Message *out_msg)
    {
        uint64_t candidate_id = selectCandidate(cycle_number);
        // The slot owner's oldest pending message, wherever it sits (Arbiter::electOldestOwned).
        return electOldestOwned(buffers, (int)candidate_id, out_msg);
    }

    bool TDMArbiter::elect(uint64_t cycle_number, vector<vector<Message> *> &buffers, Message *out_msg)
    {
        if ((cycle_number % m_arbiter_period) != 0)
            return false;

        return coreElect(cycle_number, buffers, out_msg);
    }

    bool TDMArbiter::forceElect(uint64_t cycle_number, vector<vector<Message> *> &buffers, Message *out_msg)
    {
        return coreElect(cycle_number, buffers, out_msg);
    }

    uint64_t TDMArbiter::selectCandidate(uint64_t cycle_number)
    {
        if (cycle_number % m_arbiter_period == 0)
            candidate_index = (candidate_index + 1) % m_candidates_ids->size();

        return m_candidates_ids->at(candidate_index);
    }
}