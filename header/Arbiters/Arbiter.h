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

namespace octopus
{
    class Arbiter
    {
    protected:
        vector<int> *m_candidates_ids;

        int m_arbiter_period;
        
        int candidate_index = 0;

        virtual int findMessage(vector<Message> &buffer, int id);

        // Serve the OLDEST message owned by `owner` across ALL buffers (min emit
        // cycle; ties by buffer order) and remove it. Owner-slot arbiters (RR/TDM)
        // must use this rather than "first buffer that has one": a core's slot can
        // hold messages in several senders' buffers at once (its own write-backs in
        // its L1, LLC data for it, a cache-to-cache supply from another L1), and
        // taking the first buffer in interface order starves the others -- a supply
        // parked in a later L1's buffer waited 17k cycles behind a stream of the
        // owner's own write-backs.
        bool electOldestOwned(vector<vector<Message> *> &buffers, int owner, Message *out_msg);

    public:
        Arbiter(vector<int> *candidates_ids, int arbiter_period);
        virtual ~Arbiter();

        virtual bool elect(uint64_t cycle_number, vector<vector<Message> *> &buffers, Message *out_msg) = 0;
        virtual bool forceElect(uint64_t cycle_number, vector<vector<Message> *> &buffers, Message *out_msg);
    };
}

#endif /* _ARBITER_H */
