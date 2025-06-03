/*
 * File  :      LeastRecentlyUsed.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 13, 2022
 */

#ifndef _LeastRecentlyUsed_H
#define _LeastRecentlyUsed_H

#include "ReplacementPolicy.h"

#include <iostream>
#include <vector>
#include <map>

namespace ns3
{
    class LeastRecentlyUsed : public ReplacementPolicy
    {
    protected:
        std::map<uint64_t, std::vector<uint64_t>> last_access_cycle; //key is the set and value is a vector of access cycles of each way

    public:
        LeastRecentlyUsed(uint32_t ways_count);
        ~LeastRecentlyUsed();

        virtual void update(uint64_t set, int way, uint64_t cycle);
        virtual void getReplacementCandidate(uint64_t set, int* way);
    };
}

#endif