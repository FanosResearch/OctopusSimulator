/*
 * File  :      CacheControllerExclusive.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 23, 2021
 */

#include "../header/CacheControllerExclusive.h"

namespace ns3
{
    // private controller constructor
    CacheControllerExclusive::CacheControllerExclusive(CacheXml &cacheXml, string &fsm_path, CommunicationInterface *upper_interface,
                                     CommunicationInterface *lower_interface, bool cach2Cache,
                                     int sharedMemId, CohProtType pType) : CacheController(cacheXml, fsm_path, upper_interface,
                                     lower_interface, cach2Cache, sharedMemId, pType)
    {
        
    }

    CacheControllerExclusive::~CacheControllerExclusive()
    {}

    void CacheControllerExclusive::callActionFunction(ControllerAction action)
    {
        switch(action.type)
        {
            case ControllerAction::Type::REMOVE_SAVED_REQ: this->removedSaveRequset(action.data); return;

            default: CacheController::callActionFunction(action); return;
        }
    }

    void CacheControllerExclusive::removedSaveRequset(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        if (this->m_saved_requests_for_wb.find(this->getAddressKey(msg->addr)) !=
            this->m_saved_requests_for_wb.end())
        {
            this->m_saved_requests_for_wb.erase(this->getAddressKey(msg->addr));
        }

        delete msg;
    }
}