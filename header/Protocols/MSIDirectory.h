/*
 * File  :      MSIDirectory.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 23, 2021
 */

#ifndef _MSIDirectory_H
#define _MSIDirectory_H

#include "DirectoryProtocol.h"

namespace octopus
{
    class MSIDirectory : public DirectoryProtocol
    {
    public:
        static const uint64_t REQUEST_TYPE_GETS     = 0;
        static const uint64_t REQUEST_TYPE_GETM     = 1;
        static const uint64_t REQUEST_TYPE_PUTS     = 2;
        static const uint64_t REQUEST_TYPE_PUTM     = 3;
        static const uint64_t REQUEST_TYPE_INV      = 4;
        static const uint64_t REQUEST_TYPE_PUT_ACK  = 5;
        static const uint64_t REQUEST_TYPE_INV_ACK  = 6;

    protected:
        enum class EventId
        {
            Load = 0,
            Store,
            Replacement,

            Fwd_GetS,
            Fwd_GetM,
            Inv,
            Put_Ack,

            Data_zeroAck,
            Data_nonZeroAck,
            DataFromOwner,
            
            InvAck,
            lastInvAck
        };

        enum class ActionId
        {
            Stall = 0,
            Hit,
            GetS,
            GetM,
            PutS,
            PutM_Data,
            Data2Both,
            Data2Req,
            InvAck2Req,
            InvAck_Data,
            Ack_dec,
            AckNum_set,
            SaveReq,
            Fault,
        };

        virtual void readEvent(Message &msg, EventId *out_id);

        virtual std::vector<ControllerAction> handleAction(std::vector<int> &actions, Message &msg,
                                                            GenericCacheLine &cache_line, int next_state);

    public:
        MSIDirectory(CacheDataHandler *cache, const std::string &fsm_path, int id, int sharedMemId);
        ~MSIDirectory();

        virtual std::vector<ControllerAction> processRequest(Message &request_msg, DebugPrint* dprint = NULL) override;
        virtual FRFCFS_State getRequestState(const Message &, FRFCFS_State) override;
    };
}

#endif
