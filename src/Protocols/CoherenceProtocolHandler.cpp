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
        this->m_home_ids = {sharedMemId};
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
namespace octopus
{
    int CoherenceProtocolHandler::homeOf(uint64_t addr) const
    {
        if (m_home_ids.size() <= 1) return m_home_ids.empty() ? m_shared_memory_id : m_home_ids[0];
        uint64_t line = addr / m_data_handler->getBlockSize();
        return m_home_ids[(line >> m_home_shift) % m_home_ids.size()];
    }
    bool CoherenceProtocolHandler::isHome(int id) const
    {
        for (int h : m_home_ids) if (h == id) return true;
        return m_home_ids.empty() && id == m_shared_memory_id;
    }
}
