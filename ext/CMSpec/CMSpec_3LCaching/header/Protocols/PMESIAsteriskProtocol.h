/*
 * File  :      PMESIAsteriskProtocol.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Jan 19, 2021
 */

#ifndef _PMESIAsteriskProtocol_H
#define _PMESIAsteriskProtocol_H

#include "MESIProtocol.h"

namespace ns3
{
    class PMESIAsteriskProtocol : public MESIProtocol
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

            RDM,
            RDM_Execlusive,
            RDC,
        };

        virtual void readEvent(Message &msg, MSIProtocol::EventId *out_id) override;

    public:
        PMESIAsteriskProtocol(CacheDataHandler *cache, const std::string &fsm_path, int coreId, int sharedMemId);
        ~PMESIAsteriskProtocol();
    };
}

#endif
