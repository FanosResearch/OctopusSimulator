/*
 * File  :      Random.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 13, 2022
 */

#include "../../header/ReplacementPolicies/Random.h"

namespace octopus
{
    Random::Random(uint32_t ways_count) : ReplacementPolicy(ways_count)
    {
    }

    Random::~Random()
    {
    }

    void Random::getReplacementCandidate(uint64_t set, int* way)
    {
        *way = rand() % m_ways_count;
    }
    void Random::getReplacementCandidate(uint64_t set, int* way, uint32_t allowed)
    {
        int n = 0; for (uint32_t i = 0; i < m_ways_count; i++) if (allowed & (1u << i)) n++;
        if (n == 0) { getReplacementCandidate(set, way); return; }
        int k = rand() % n;
        for (uint32_t i = 0; i < m_ways_count; i++) if (allowed & (1u << i)) { if (k-- == 0) { *way = (int)i; return; } }
    }
}