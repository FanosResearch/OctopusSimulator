/*
 * File  :      RTAArbiter.h
 * Author:      Shorouk Abdelhalim
 * Email :      abdels28@mcmaster.ca
 *
 * Created On April 17, 2022
 */

#ifndef _RROF_ARBITER_H
#define _RROF_ARBITER_H

#include "Arbiter.h"
#include "../RequestorsQueues.h"
#include <iostream>
#include <unordered_map>

using namespace std;

namespace ns3
{
    class RROFArbiter: public Arbiter
    {
    private:
		unsigned int RR_order;
		unsigned int Requestors_num;
		unsigned int Requestor_size;
		unsigned int slot;
		bool exitFlag;
    vector<bool> exitVector;
		unsigned int candidate_id;
    int msg_index;
		
		
		
        
    public:
        RROFArbiter(vector<int>* candidates_ids, int arbiter_period);
        ~RROFArbiter();

        virtual bool elect(uint64_t cycle_number, vector<vector<Message>*>& buffers, Message *out_msg) override;

        int findRequest(vector<Message> &buffer, int id);
    };
}

#endif /* _RROF_ARBITER_H */
