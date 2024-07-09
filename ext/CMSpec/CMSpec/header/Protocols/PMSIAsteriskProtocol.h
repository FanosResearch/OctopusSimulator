/*
 * File  :      PMSIAsteriskProtocol.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Jan 19, 2021
 */

#ifndef _PMSIAsteriskProtocol_H
#define _PMSIAsteriskProtocol_H

#include "MSIProtocol.h"

namespace ns3
{
    class PMSIAsteriskProtocol : public MSIProtocol
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
            RDC,
        };

        virtual void readEvent(Message &msg, MSIProtocol::EventId *out_id) override;

    public:
        PMSIAsteriskProtocol(CacheDataHandler *cache, const std::string &fsm_path, int coreId, int sharedMemId);
        ~PMSIAsteriskProtocol();
    };
}

#endif
