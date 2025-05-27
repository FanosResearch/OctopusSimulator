/*
 * File  :      ReplacementPolicy.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 13, 2022
 */

#ifndef _REPLACEMENTPOLICY_H
#define _REPLACEMENTPOLICY_H

#include <stdint.h>

namespace ns3
{
    class ReplacementPolicy
    {    
    protected:
        uint32_t m_ways_count;

    public:
        ReplacementPolicy(uint32_t ways_count) { m_ways_count = ways_count; }
        ~ReplacementPolicy(){}

        virtual void update(uint64_t set, int way, uint64_t cycle) = 0;
        virtual void getReplacementCandidate(uint64_t set, int* way) = 0;
    };
}

#endif