/*
 * File  :      LLCMSIDirectory.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 2, 2023
 */

#ifndef _LLCMSIDirectory_H
#define _LLCMSIDirectory_H

#include "DirectoryProtocol.h"
#include "MSIDirectory.h"

namespace ns3
{
    class LLCMSIDirectory : public DirectoryProtocol
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
            ReSendInv,
            Fault,
        };

        virtual void readEvent(Message &msg, GenericCacheLine &cache_line, EventId *out_id);

        virtual std::vector<ControllerAction> handleAction(std::vector<int> &actions, Message &msg,
                                                            GenericCacheLine &cache_line, int next_state);

    public:
        LLCMSIDirectory(CacheDataHandler *cache, const std::string &fsm_path, int id, int sharedMemId);
        ~LLCMSIDirectory();

        virtual std::vector<ControllerAction> processRequest(Message &request_msg, DebugPrint* dprint = NULL) override;
        virtual EntryState getRequestState(const Message &, EntryState) override;
        virtual void createDefaultCacheLine(uint64_t address, GenericCacheLine *cache_line) override;
    };
}

#endif
