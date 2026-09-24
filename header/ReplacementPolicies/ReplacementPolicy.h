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

namespace octopus
{
    class ReplacementPolicy
    {    
    protected:
        uint32_t m_ways_count;

    public:
        ReplacementPolicy(uint32_t ways_count) { m_ways_count = ways_count; }
        virtual ~ReplacementPolicy(){}

        virtual void update(uint64_t set, int way, uint64_t cycle) = 0;
        virtual void getReplacementCandidate(uint64_t set, int* way) = 0;
        // way partitioning (docs/Tasks.md "Protection"): the victim must be one of the ways
        // in `allowed` (bit i = way i). Default: the unrestricted choice if it is allowed,
        // else the lowest allowed way.
        virtual void getReplacementCandidate(uint64_t set, int* way, uint32_t allowed)
        {
            getReplacementCandidate(set, way);
            if (allowed & (1u << *way)) return;
            for (uint32_t i = 0; i < m_ways_count; i++) if (allowed & (1u << i)) { *way = (int)i; return; }
        }
    };
}

#endif