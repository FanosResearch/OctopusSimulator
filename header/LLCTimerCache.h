/*
 * File  :      LLCTimercache.h
 * Author:      Safin Bayes
 * Email :      bayess@mcmaster.ca
 *
 * Created On March 09, 2022
 */

#ifndef _LLCTimerCache_H
#define _LLCTimerCache_H

#include "GenericCache.h"

namespace ns3
{
    class LLCTimerCacheFrmt : public GenericCacheLine
    {
    //    protected:
    //        int sharer_count;
        
        public:
            LLCTimerCacheFrmt()
            {};
            ~LLCTimerCacheFrmt()
            {};

            int sharer_count; 
    };

    class LLCTimerCache : public GenericCache
    {
        public:
        LLCTimerCache(uint32_t size) 
            {
                m_cache = new LLCTimerCacheFrmt[size];
            }
    };
}
#endif