/*
 * File  :      LLCMSIProtocol.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On July 25, 2021
 */

#ifndef _LLCMSIProtocol_H
#define _LLCMSIProtocol_H

#include "CoherenceProtocolHandler.h"
#include "MSIProtocol.h"

namespace ns3
{
    class LLCMSIProtocol : public CoherenceProtocolHandler
    {
    protected:
        enum class EventId
        {
            GetS = 0,
            GetM,
            Replacement,
            PutM_fromOwner,
            PutM_fromNonOwner,
            Data_fromLowerInterface,
            Data_fromUpperInterface,
            Own_Invalidation,
            Own_Silent_Inv,
        };

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
            Fault
        };

        std::vector<ControllerAction> controller_actions; //used only for returning data

        virtual void readEvent(Message &msg, GenericCacheLine &cache_line, EventId *out_id);

        virtual std::vector<ControllerAction> &handleAction(std::vector<int> &actions, Message &msg,
                                                            GenericCacheLine &cache_line, int next_state);

    public:
        LLCMSIProtocol(CacheDataHandler *cache, const std::string &fsm_path, int coreId, int sharedMemId);
        ~LLCMSIProtocol();

        virtual const std::vector<ControllerAction> &processRequest(Message &request_msg) override;
        virtual FRFCFS_State getRequestState(const Message &, FRFCFS_State) override;
        virtual void createDefaultCacheLine(uint64_t address, GenericCacheLine *cache_line) override;
    };
}

#endif
