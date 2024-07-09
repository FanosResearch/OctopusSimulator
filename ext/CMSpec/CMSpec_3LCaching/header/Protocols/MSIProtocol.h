/*
 * File  :      MSIProtocol.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 23, 2021
 */

#ifndef _MSIProtocol_H
#define _MSIProtocol_H

#include "CoherenceProtocolHandler.h"

namespace ns3
{
    class MSIProtocol : public CoherenceProtocolHandler
    {
    public:
        static const uint64_t REQUEST_TYPE_GETS       = 0;
        static const uint64_t REQUEST_TYPE_GETM       = 1;
        static const uint64_t REQUEST_TYPE_PUTM       = 2;
        static const uint64_t REQUEST_TYPE_INV        = 10;
        static const uint64_t REQUEST_TYPE_SILENT_INV = 11;

    protected:
        enum class EventId
        {
            Load = 0,
            Store,
            Replacement,

            Own_GetS,
            Own_GetM,
            Own_PutM,

            Other_GetS,
            Other_GetM,
            Other_PutM,

            OwnData,
            Invalidation,
            Silent_Inv,
        };

        enum class ActionId
        {
            Stall = 0,
            Hit,
            GetS,
            GetM,
            PutM,
            Data2Req,
            Data2Both,
            SaveReq,
            Fault
        };

        std::vector<ControllerAction> controller_actions; //used only for returning data

        virtual std::vector<int> statesRequireWriteBack();

        virtual void readEvent(Message &msg, EventId *out_id);

        virtual std::vector<ControllerAction> &handleAction(std::vector<int> &actions, Message &msg,
                                                            GenericCacheLine &cache_line, int next_state);

    public:
        MSIProtocol(CacheDataHandler *cache, const std::string &fsm_path, int coreId, int sharedMemId);
        ~MSIProtocol();

        virtual const std::vector<ControllerAction> &processRequest(Message &request_msg) override;
        virtual FRFCFS_State getRequestState(const Message &, FRFCFS_State) override;
    };
}

#endif
