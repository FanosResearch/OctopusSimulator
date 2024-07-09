/*
 * File  :      LLCPMSIProtocol.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 20, 2021
 */

#ifndef _LLCPMSIProtocol_H
#define _LLCPMSIProtocol_H

#include "LLCMSIProtocol.h"

namespace ns3
{
    class LLCPMSIProtocol : public LLCMSIProtocol
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

            WaitData
        };

        virtual std::vector<ControllerAction> &handleAction(std::vector<int> &actions, Message &msg,
                                                            GenericCacheLine &cache_line_info, int next_state) override;

    public:
        LLCPMSIProtocol(CacheDataHandler *cache, const std::string &fsm_path, int coreId, int sharedMemId);
        ~LLCPMSIProtocol();
    };
}

#endif
