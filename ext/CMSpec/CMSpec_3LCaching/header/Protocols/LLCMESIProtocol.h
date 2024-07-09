/*
 * File  :      LLCMESIProtocol.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Nov 28, 2021
 */

#ifndef _LLCMESIProtocol_H
#define _LLCMESIProtocol_H

#include "LLCMSIProtocol.h"

namespace ns3
{
    class LLCMESIProtocol : public LLCMSIProtocol
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
            SendExeclusiveData,
        };

        virtual std::vector<ControllerAction> &handleAction(std::vector<int> &actions, Message &msg,
                                                            GenericCacheLine &cache_line_info, int next_state) override;

    public:
        LLCMESIProtocol(CacheDataHandler *cache, const std::string &fsm_path, int coreId, int sharedMemId);
        ~LLCMESIProtocol();

        void createDefaultCacheLine(uint64_t address, GenericCacheLine *cache_line) override;
    };
}

#endif
