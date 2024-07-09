/*
 * File  :      MESIProtocol.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Nov 28, 2021
 */

#ifndef _MESIProtocol_H
#define _MESIProtocol_H

#include "MSIProtocol.h"

namespace ns3
{
    class MESIProtocol : public MSIProtocol
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
            OwnData_Execlusive
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
            removeSavedReq,
        };

        virtual std::vector<int> statesRequireWriteBack() override;
        virtual void readEvent(Message &msg, MSIProtocol::EventId *out_id) override;
        
        virtual std::vector<ControllerAction> &handleAction(std::vector<int> &actions, Message &msg,
                                                            GenericCacheLine &cache_line_info, int next_state) override;

    public:
        MESIProtocol(CacheDataHandler *cache, const std::string &fsm_path, int coreId, int sharedMemId);
        ~MESIProtocol();
    };
}

#endif
