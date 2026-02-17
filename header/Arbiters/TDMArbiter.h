/*
 * File  :      TDMArbiter.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 10, 2022
 */

#ifndef _TDM_ARBITER_H
#define _TDM_ARBITER_H

#include "Arbiter.h"

using namespace std;

namespace ns3
{
    class TDMArbiter: public Arbiter
    {
    protected:
        virtual uint64_t selectCandidate(uint64_t cycle_number);
        virtual bool coreElect(uint64_t cycle_number, vector<vector<Message>*>& buffers, Message *out_msg);
        
    public:
        TDMArbiter(vector<int>* candidates_ids, int arbiter_period);
        ~TDMArbiter();

        virtual bool elect(uint64_t cycle_number, vector<vector<Message>*>& buffers, Message *out_msg) override;
        virtual bool forceElect(uint64_t cycle_number, vector<vector<Message>*>& buffers, Message *out_msg) override;
    };
}

#endif /* _TDM_ARBITER_H */
