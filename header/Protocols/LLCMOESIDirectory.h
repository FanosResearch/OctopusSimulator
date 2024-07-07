/*
 * File  :      LLCMOESIDirectory.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 15, 2023
 */

#ifndef _LLCMOESIDirectory_H
#define _LLCMOESIDirectory_H

#include "MOESIDirectory.h"
#include "LLCMESIDirectory.h"

namespace octopus
{
    class LLCMOESIDirectory : public LLCMESIDirectory
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
            GetM_fromOwner,
            PutO_Data_fromOwner,
            PutO_Data_fromNonOwner,
            last_PutO_fromOwner,
            Data_fromLowerInterface_lastAck,
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
            sendAckC,
            SendInvM,
        };

        virtual void readEvent(Message &msg, GenericCacheLine &cache_line, LLCMSIDirectory::EventId *out_id) override;

        virtual std::vector<ControllerAction> handleAction(std::vector<int> &actions, Message &msg,
                                                            GenericCacheLine &cache_line, int next_state) override;

    public:
        LLCMOESIDirectory(CacheDataHandler *cache, const std::string &fsm_path, int id, int sharedMemId);
        ~LLCMOESIDirectory();
    };
}

#endif
