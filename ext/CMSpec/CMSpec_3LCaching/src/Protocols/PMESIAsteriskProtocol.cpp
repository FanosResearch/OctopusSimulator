/*
 * File  :      PMESIAsteriskProtocol.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Nov 28, 2021
 */

#include "../../header/Protocols/PMESIAsteriskProtocol.h"
using namespace std;

namespace ns3
{
    PMESIAsteriskProtocol::PMESIAsteriskProtocol(CacheDataHandler *cache, const string &fsm_path, int coreId, int sharedMemId) : MESIProtocol(cache, fsm_path, coreId, sharedMemId)
    {
    }

    PMESIAsteriskProtocol::~PMESIAsteriskProtocol()
    {
    }

    void PMESIAsteriskProtocol::readEvent(Message &msg, MSIProtocol::EventId *out_id)
    {
        MESIProtocol::readEvent(msg, out_id);

        if ((MESIProtocol::EventId)*out_id == MESIProtocol::EventId::OwnData)
            *out_id = (MSIProtocol::EventId)((msg.to[0] == this->m_shared_memory_id) ? EventId::RDM : EventId::RDC);
        else if ((MESIProtocol::EventId)*out_id == MESIProtocol::EventId::OwnData_Execlusive)
            *out_id = (MSIProtocol::EventId) EventId::RDM_Execlusive;
    }
}