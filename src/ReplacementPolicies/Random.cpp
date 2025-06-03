/*
 * File  :      Random.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 13, 2022
 */

#include "../../header/ReplacementPolicies/Random.h"

namespace ns3
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
}