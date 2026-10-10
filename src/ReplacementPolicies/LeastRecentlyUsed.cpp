/*
 * File  :      LeastRecentlyUsed.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 13, 2022
 */

#include "../../header/ReplacementPolicies/LeastRecentlyUsed.h"

namespace octopus
{
    LeastRecentlyUsed::LeastRecentlyUsed(uint32_t ways_count) : ReplacementPolicy(ways_count)
    {
    }

    LeastRecentlyUsed::~LeastRecentlyUsed()
    {
    }

    void LeastRecentlyUsed::update(uint64_t set, int way, uint64_t cycle)
    {
        if (way < 0)
            return;
        if(last_access_cycle[set].size() > way)
            last_access_cycle[set][way] = cycle;
        else
            last_access_cycle[set].push_back(cycle);
    }

    // least recently used among the allowed ways (a way never touched counts as oldest)
    void LeastRecentlyUsed::getReplacementCandidate(uint64_t set, int* way, uint32_t allowed)
    {
        std::vector<uint64_t> &v = last_access_cycle[set];
        *way = -1; uint64_t best = 0;
        for (uint32_t i = 0; i < m_ways_count; i++)
        {
            if (!(allowed & (1u << i))) continue;
            uint64_t stamp = (i < v.size()) ? v[i] : 0;
            if (*way < 0 || stamp < best) { *way = (int)i; best = stamp; }
        }
        if (*way < 0) getReplacementCandidate(set, way);
    }

    void LeastRecentlyUsed::getReplacementCandidate(uint64_t set, int* way)
    {
        if(last_access_cycle.find(set) == last_access_cycle.end() &&
           (int)last_access_cycle[set].size() == m_ways_count)
        {
            std::cout << "LeastRecentlyUsed: No record for set: " << set; 
            std::cout<< " or it's not full." << std::endl;
            exit(0);
        }

        *way = 0;
        for(int i = 1; i < (int)last_access_cycle[set].size(); i++)
        {
            if(last_access_cycle[set][*way] > last_access_cycle[set][i])
                *way = i;
        }
    }
}