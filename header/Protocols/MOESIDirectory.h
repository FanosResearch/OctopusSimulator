/*
 * File  :      MOESIDirectory.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 15, 2023
 */

#ifndef _MOESIDirectory_H
#define _MOESIDirectory_H

#include "MESIDirectory.h"

namespace octopus
{
    class MOESIDirectory : public MESIDirectory
    {
    public:
        static const uint64_t REQUEST_TYPE_PUTO = 8;
        static const uint64_t RESPONSE_TYPE_ACKCOUNT = 9;
        static const uint64_t REQUEST_TYPE_INVM = 10;

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
            lastInvAck,
            Data_execlusive,
            AckCount,
            AckCount_zero,
            DataFromOwner_nonZeroAck,
            InvM,
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
            PutE,
            PutO_Data,
            SetAck,
        };

        virtual void readEvent(Message &msg, MSIDirectory::EventId *out_id) override;

        virtual std::vector<ControllerAction> handleAction(std::vector<int> &actions, Message &msg,
                                                            GenericCacheLine &cache_line, int next_state) override;

    public:
        MOESIDirectory(CacheDataHandler *cache, const std::string &fsm_path, int id, int sharedMemId);
        ~MOESIDirectory();
    };
}

#endif
