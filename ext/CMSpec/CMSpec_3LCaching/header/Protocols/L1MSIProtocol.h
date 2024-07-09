/*
 * File  :      L1MSIProtocol.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 13, 2024
 */

#ifndef _L1MSIProtocol_H
#define _L1MSIProtocol_H

#include "MSIProtocol.h"

namespace ns3
{
    class L1MSIProtocol : public MSIProtocol
    {
    protected:
        enum class EventId
        {
            Load = 0,
            Store,
            Replacement,
            
            OwnData,
            Invalidation,
        };

        enum class ActionId
        {
            Stall = 0,
            Hit,
            GetS,
            GetM,
            SendData,
            Fault,
        };

        virtual void readEvent(Message &msg, MSIProtocol::EventId *out_id) override;
        virtual std::vector<ControllerAction> &handleAction(std::vector<int> &actions, Message &msg,
                                                            GenericCacheLine &cache_line, int next_state) override;

    public:
        L1MSIProtocol(CacheDataHandler *cache, const std::string &fsm_path, int coreId, int sharedMemId);
        ~L1MSIProtocol();
    };
}

#endif
