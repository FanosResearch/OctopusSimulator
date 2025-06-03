/*
 * File  :      FCFSArbiter.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 20, 2022
 */

#ifndef _FCFS_ARBITER_H
#define _FCFS_ARBITER_H

#include "Arbiter.h"

using namespace std;

namespace ns3
{
    class FCFSArbiter: public Arbiter
    {
    public:
        FCFSArbiter(vector<int>* candidates_ids, int arbiter_period);
        ~FCFSArbiter();

        virtual bool elect(uint64_t cycle_number, vector<vector<Message>*>& buffers, Message *out_msg) override;
    };
}

#endif /* _FCFS_ARBITER_H */
