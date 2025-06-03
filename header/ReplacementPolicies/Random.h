/*
 * File  :      Random.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 13, 2022
 */

#ifndef _Random_H
#define _Random_H

#include "ReplacementPolicy.h"

#include <math.h>

namespace ns3
{
    class Random : public ReplacementPolicy
    {
    public:
        Random(uint32_t ways_count);
        ~Random();

        virtual void update(uint64_t set, int way, uint64_t cycle) {}
        virtual void getReplacementCandidate(uint64_t set, int* way);
    };
}

#endif