/*
 * File  :      CoherenceProtocolHandler.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 23, 2021
 */

#include "../../header/Protocols/CoherenceProtocolHandler.h"
using namespace std;

namespace octopus
{
    CoherenceProtocolHandler::CoherenceProtocolHandler(CacheDataHandler *cache, const string &fsm_path, int id, int sharedMemId)
    {
        this->m_data_handler = cache;
        this->m_fsm = new FSMReader(fsm_path);

        this->m_id = id;
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

    int CoherenceProtocolHandler::homeOf(uint64_t addr) const
    {
        return m_home.ids.empty() ? m_shared_memory_id : m_home.select(addr / m_data_handler->getBlockSize());
    }

    bool CoherenceProtocolHandler::isHome(int id) const
    {
        return m_home.ids.empty() ? id == m_shared_memory_id : m_home.indexOf(id) >= 0;
    }
}
