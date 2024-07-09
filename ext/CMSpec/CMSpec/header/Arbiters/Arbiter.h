/*
 * File  :      Arbiter.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 12, 2022
 */

#ifndef _ARBITER_H
#define _ARBITER_H

#include "CommunicationInterface.h"

#include <vector>
#include <map>

using namespace std;

namespace ns3
{
    class Arbiter
    {
    protected:
        vector<int> *m_candidates_ids;

        int m_arbiter_period;
        
        int candidate_index = 0;

        virtual int findMessage(vector<Message> &buffer, int id);

    public:
        Arbiter(vector<int> *candidates_ids, int arbiter_period);
        virtual ~Arbiter();

        virtual bool elect(uint64_t cycle_number, vector<vector<Message> *> &buffers, Message *out_msg) = 0;
        virtual bool forceElect(uint64_t cycle_number, vector<vector<Message> *> &buffers, Message *out_msg);
    };
}

#endif /* _ARBITER_H */
