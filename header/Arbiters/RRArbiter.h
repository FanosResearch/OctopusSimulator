/*
 * File  :      RRArbiter.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 21, 2022
 */

#ifndef _RR_ARBITER_H
#define _RR_ARBITER_H

#include "Arbiter.h"

using namespace std;

namespace ns3
{
    class RRArbiter: public Arbiter
    {
    protected:
        uint64_t candidate_id;

        virtual uint64_t selectCandidate(uint64_t cycle_number);
        virtual bool coreElect(vector<vector<Message>*>& buffers, Message *out_msg);
        
    public:
        RRArbiter(vector<int>* candidates_ids, int arbiter_period);
        ~RRArbiter();

        virtual bool elect(uint64_t cycle_number, vector<vector<Message>*>& buffers, Message *out_msg) override;
        virtual bool forceElect(uint64_t cycle_number, vector<vector<Message>*>& buffers, Message *out_msg) override;
    };
}

#endif /* _RR_ARBITER_H */
