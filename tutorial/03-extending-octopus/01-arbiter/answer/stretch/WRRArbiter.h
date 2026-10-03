/*
 * File  :      WRRArbiter.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 21, 2022
 */

#ifndef _WRR_ARBITER_H
#define _WRR_ARBITER_H

#include "Arbiter.h"

using namespace std;

namespace octopus
{
    class WRRArbiter: public Arbiter
    {
    protected:
        uint64_t candidate_id;
        uint64_t m_extra_slots;

        virtual uint64_t selectCandidate(uint64_t cycle_number);
        virtual bool coreElect(vector<vector<Message>*>& buffers, Message *out_msg);
        
    public:
        WRRArbiter(vector<int>* candidates_ids, int arbiter_period, int extra_slots = 1);
        ~WRRArbiter();

        virtual bool elect(uint64_t cycle_number, vector<vector<Message>*>& buffers, Message *out_msg) override;
        virtual bool forceElect(uint64_t cycle_number, vector<vector<Message>*>& buffers, Message *out_msg) override;
    };
}

#endif /* _WRR_ARBITER_H */
