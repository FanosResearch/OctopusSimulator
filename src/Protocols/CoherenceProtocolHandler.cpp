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
    CoherenceProtocolHandler::CoherenceProtocolHandler(CacheDataHandler *cache, const string &fsm_path, 
                                                       int id, int sharedMemId, AllocationType alloc_type)
    {
        this->m_data_handler = cache;
        this->m_fsm = new FSMReader(fsm_path);

        this->m_id = id;
        this->m_shared_memory_id = sharedMemId;
        this->m_alloc_type = alloc_type;
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

    void CoherenceProtocolHandler::allocateLine(uint64_t addr, bool* caused_replacement)
    {
        if(m_alloc_type == AllocationType::ON_MISS)
        {
            GenericCacheLine line; //empty line
            line.valid = true;
            
            if(caused_replacement)
                *caused_replacement = false;

            if(!m_data_handler->writeCacheLine_bypassLatency(addr, &line, true))
            {
                //write failed due to full set
                m_data_handler->makeSpaceFor(addr);
                m_data_handler->writeCacheLine_bypassLatency(addr, &line, true);

                if(caused_replacement)
                    *caused_replacement = true;
            }
        }
        else if(m_alloc_type == AllocationType::ON_REFILL)
        {
            //nothing to do as the dataHandler will take care of it
        }
    }
}