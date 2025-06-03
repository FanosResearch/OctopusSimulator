/*
 * File  :      Timercache.h
 * Author:      Safin Bayes
 * Email :      bayess@mcmaster.ca
 *
 * Created On March 02, 2022
 */

#ifndef _TimerCache_H
#define _TimerCache_H

#include "GenericCache.h"
#include "IdGenerator.h"

namespace ns3
{
    class TimerCacheFrmt : public GenericCacheLine
    {
        public:
            uint64_t timeout;
        
        //public:
    };

    class TimerCache : public GenericCache
    {
        public:
            TimerCache(uint32_t size) 
            {
                m_cache = new TimerCacheFrmt[size];
            }
            TimerCache()
            {}

            ~TimerCache()
            {
                delete[] m_cache;
            }

            vector <Message> checktimeout(uint64_t current_cycle, int size, uint16_t core_id)
            {
                vector <Message> timeout_requests;
                TimerCacheFrmt* cacheline = (TimerCacheFrmt*) m_cache;

                for(int i = 0; i < size; i++)
                {
                    if (cacheline[i].timeout == current_cycle)
                    {
                        Message *msg = new Message {  .msg_id = (uint64_t) IdGenerator::nextReqId(),
                                                     .addr = CpuPhyAddr(*(GenericCacheMapFrmt*) cacheline),                
                                                     .cycle = current_cycle,
                                                     .requestor_id = core_id,
                                                     .source = Message::Source::SELF
                        };

                        timeout_requests.push_back(*msg);
                    }
                }
                return(timeout_requests);
            }
    };
}


#endif