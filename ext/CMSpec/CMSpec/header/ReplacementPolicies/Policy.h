/*
 * File  :      Policy.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 13, 2022
 */

#ifndef _POLICY_H
#define _POLICY_H

#include "ReplacementPolicy.h"
#include "Random.h"
#include "LeastRecentlyUsed.h"

#include <string>

namespace ns3
{
    class Policy
    {    
    public:
        static ReplacementPolicy* getReplacementPolicy(std::string policy_name, uint32_t ways_count)
        {
            if(policy_name == "RANDOM")
                return new Random(ways_count);
            if(policy_name == "LRU")
                return new LeastRecentlyUsed(ways_count);
            else
            {
                std::cout << "Error unvalid policy name" << std::endl;
                exit(0);
                return NULL;
            }
        }
    };
}

#endif