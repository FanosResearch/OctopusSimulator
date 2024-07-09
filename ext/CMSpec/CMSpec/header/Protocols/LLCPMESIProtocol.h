/*
 * File  :      LLCPMESIProtocol.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Jan 18, 2022
 */

#ifndef _LLCPMESIProtocol_H
#define _LLCPMESIProtocol_H

#include "LLCMESIProtocol.h"

namespace ns3
{
    class LLCPMESIProtocol : public LLCMESIProtocol
    {
    protected:
        enum class ActionId
        {
            Stall = 0,
            GetData,
            SendData,
            SaveData,
            SetOwner,
            ClearOwner,
            IssueInv,
            WriteBack,
            Fault,

            WaitData,
            SendExeclusiveData
        };

        virtual std::vector<ControllerAction> &handleAction(std::vector<int> &actions, Message &msg,
                                                            GenericCacheLine &cache_line_info, int next_state) override;

    public:
        LLCPMESIProtocol(CacheDataHandler *cache, const std::string &fsm_path, int coreId, int sharedMemId);
        ~LLCPMESIProtocol();
    };
}

#endif
