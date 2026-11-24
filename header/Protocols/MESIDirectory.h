/*
 * File  :      MESIDirectory.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 14, 2023
 */

#ifndef _MESIDirectory_H
#define _MESIDirectory_H

#include "MSIDirectory.h"

namespace octopus
{
    class MESIDirectory : public MSIDirectory
    {
    public:
        static const uint64_t REQUEST_TYPE_PUTE             = 7;
        static const uint64_t RESPONSE_TYPE_EXECLUSIVE_DATA = 0xFFFFFFFF;
        // The home's invalidation of the line's owner (E or M) when it evicts the line. Unlike Inv,
        // which a sharer acks, the owner answers it once, with the data. An L1 still waiting for
        // its exclusive data (IS_d) can tell the two apart and waits for the data instead of
        // acking first and returning the data later on the other bus (MESI_directory).
        static const uint64_t REQUEST_TYPE_INVM = 11;   // not 10: Message::isInvalidation() reads 10 as the snoop INV and gates it per line

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
            InvM,                // only in tables that define it (MESI_directory)
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
        };

        virtual void readEvent(Message &msg, MSIDirectory::EventId *out_id) override;

        virtual std::vector<ControllerAction> handleAction(std::vector<int> &actions, Message &msg,
                                                            GenericCacheLine &cache_line, int next_state) override;

    public:
        MESIDirectory(CacheDataHandler *cache, const std::string &fsm_path, int id, int sharedMemId);
        ~MESIDirectory();
    };
}

#endif
