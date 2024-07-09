/*
 * File  :      PMSIAsteriskProtocol.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Nov 28, 2021
 */

#include "../../header/Protocols/PMSIAsteriskProtocol.h"
using namespace std;

namespace ns3
{
    PMSIAsteriskProtocol::PMSIAsteriskProtocol(CacheDataHandler *cache, const string &fsm_path, int coreId, int sharedMemId) : MSIProtocol(cache, fsm_path, coreId, sharedMemId)
    {
    }

    PMSIAsteriskProtocol::~PMSIAsteriskProtocol()
    {
    }

    void PMSIAsteriskProtocol::readEvent(Message &msg, MSIProtocol::EventId *out_id)
    {
        MSIProtocol::readEvent(msg, out_id);

        if (*out_id == MSIProtocol::EventId::OwnData)
            *out_id = (MSIProtocol::EventId)((msg.to[0] == this->m_shared_memory_id) ? EventId::RDM : EventId::RDC);
    }
}