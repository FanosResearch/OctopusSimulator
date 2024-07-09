/*
 * File  :      PMSIProtocol.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 11, 2021
 */

#ifndef _PMSIProtocol_H
#define _PMSIProtocol_H

#include "MSIProtocol.h"

namespace ns3
{
    class PMSIProtocol : public MSIProtocol
    {
    protected:
        enum class ActionId
        {
            Stall = 0,
            Hit,
            GetS,
            GetM,
            PutM,
            Data2Req,
            
            Fault = 8,
            PutM_nonDem,
        };

        virtual std::vector<ControllerAction> &handleAction(std::vector<int> &actions, Message &msg,
                                                            GenericCacheLine &cache_line_info, int next_state) override;

    public:
        PMSIProtocol(CacheDataHandler *cache, const std::string &fsm_path, int coreId, int sharedMemId);
        ~PMSIProtocol();
    };
}

#endif
