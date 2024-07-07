/*
 * File  :      LLCMESIDirectory.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 15, 2023
 */

#ifndef _LLCMESIDirectory_H
#define _LLCMESIDirectory_H

#include "MESIDirectory.h"
#include "LLCMSIDirectory.h"

namespace octopus
{
    class LLCMESIDirectory : public LLCMSIDirectory
    {
    protected:
        enum class EventId
        {
            GetS = 0,
            GetM,
            Replacement,
            PutS,
            last_PutS,
            InvAck,
            last_InvAck,
            PutM_Data_fromOwner,
            PutM_Data_fromNonOwner,
            Data_fromLowerInterface,
            Data_fromUpperInterface,
            PutE_fromOwner,
            PutE_fromNonOwner,
        };

        enum class ActionId
        {
            Stall = 0,
            GetData,
            SendData,
            SaveData,
            SetOwner,
            ClearOwner,
            SendInv,
            WriteBack,
            FwdGetS,
            FwdGetM,
            PutAck,
            IncSharers,
            DecSharers,
            Fault,
            SendEData,
        };

        virtual void readEvent(Message &msg, GenericCacheLine &cache_line, LLCMSIDirectory::EventId *out_id) override;

        virtual std::vector<ControllerAction> handleAction(std::vector<int> &actions, Message &msg,
                                                            GenericCacheLine &cache_line, int next_state) override;

    public:
        LLCMESIDirectory(CacheDataHandler *cache, const std::string &fsm_path, int id, int sharedMemId);
        ~LLCMESIDirectory();
    };
}

#endif
