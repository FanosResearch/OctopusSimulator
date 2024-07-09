/*
 * File  :      LLCPendulum.h
 * Author:      Safin Bayes
 * Email :      bayess@mcmaster.ca
 *
 * Created On July 25, 2021
 */

#ifndef _LLCPendulum_H
#define _LLCPendulum_H

#include "LLCMSIProtocol.h"

namespace ns3
{
    class LLCPendulum : public LLCMSIProtocol
    {
    protected:
        enum class EventId
        {
            GetS = 0,
            GetM,
            PutM_fromOwner,
            PutM_fromNonOwner,
            SelfInv,
            Inv_GetM,
            Sharers_0,
            Data_fromLowerInteface
        };

        enum class ActionId
        {
            Stall = 0,
            IncrementSharer,
            DecrementSharer,
            SendData,
            SaveData,
            SaveReq,
            SetOwner,
            ClearOwner,
            Fault
        };

        virtual void readEvent(Message &msg, GenericCache::CacheLineInfo cache_line_info, LLCMSIProtocol::EventId *out_id) override;
        virtual std::vector<ControllerAction> &handleAction(std::vector<int> &actions, Message &msg,
                                                            const GenericCache::CacheLineInfo &cache_line_info, int next_state) override;

    public:
        LLCPendulum(GenericCache *cache, const std::string &fsm_path, int coreId);
        ~LLCPendulum();

    };
}

#endif
