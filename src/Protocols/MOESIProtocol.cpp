/*
 * File  :      MOESIProtocol.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Nov 29, 2021
 */

#include "../../header/Protocols/MOESIProtocol.h"
using namespace std;

namespace octopus
{
    MOESIProtocol::MOESIProtocol(CacheDataHandler *cache, const string &fsm_path, int id, int sharedMemId) : MESIProtocol(cache, fsm_path, id, sharedMemId)
    {
    }

    MOESIProtocol::~MOESIProtocol()
    {
    }

    std::vector<int> MOESIProtocol::statesRequireWriteBack()
    {
        vector<int> states = MESIProtocol::statesRequireWriteBack();
        states.push_back(this->m_fsm->getState(string("O")));
        return states;
    }
}