/*
 * File  :      CoherenceProtocolHandler.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 23, 2021
 */

#include "../../header/Protocols/CoherenceProtocolHandler.h"
using namespace std;

namespace ns3
{
    CoherenceProtocolHandler::CoherenceProtocolHandler(CacheDataHandler *cache, const string &fsm_path, int coreId, int sharedMemId)
    {
        this->m_data_handler = cache;
        this->m_fsm = new FSMReader(fsm_path);

        this->m_core_id = coreId;
        this->m_shared_memory_id = sharedMemId;
    }

    CoherenceProtocolHandler::~CoherenceProtocolHandler()
    {
        delete this->m_fsm;
    }

    void CoherenceProtocolHandler::initializeCacheStates()
    {   //This function should be overridden if the name
        //of the invalid state is different than "I"
        int initState = this->m_fsm->getState(string("I"));
        m_data_handler->initializeCacheStates(initState);
    }
}