/*
 * File  :      L2MSIProtocol.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 23, 2021
 */

#ifndef _L2MSIProtocol_H
#define _L2MSIProtocol_H

#include "MSIProtocol.h"

namespace ns3
{
    class L2MSIProtocol : public MSIProtocol
    {
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
            DataFromLowerInterface,
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
            Fault,
            SendInv,
            SaveData,
        };

        virtual void readEvent(Message &msg, MSIProtocol::EventId *out_id) override;
        virtual std::vector<ControllerAction> &handleAction(std::vector<int> &actions, Message &msg,
                                                            GenericCacheLine &cache_line, int next_state) override;

    public:
        L2MSIProtocol(CacheDataHandler *cache, const std::string &fsm_path, int coreId, int sharedMemId);
        ~L2MSIProtocol();
    };
}

#endif
