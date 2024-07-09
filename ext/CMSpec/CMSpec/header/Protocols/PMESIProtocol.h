/*
 * File  :      PEMSIProtocol.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Jan 17, 2022
 */

#ifndef _PEMSIProtocol_H
#define _PEMSIProtocol_H

#include "MESIProtocol.h"

namespace ns3
{
    class PMESIProtocol : public MESIProtocol
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
        PMESIProtocol(CacheDataHandler *cache, const std::string &fsm_path, int coreId, int sharedMemId);
        ~PMESIProtocol();
    };
}

#endif
