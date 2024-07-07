/*
 * File  :      CacheControllerExclusive.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 23, 2021
 */

#include "../../header/CacheControllers/CacheControllerExclusive.h"

namespace octopus
{
    CacheControllerExclusive::CacheControllerExclusive(ParametersMap map, CommunicationInterface *upper_interface, CommunicationInterface *lower_interface,
                                                       string pname, string config_path, string name)
            : CacheController(map, upper_interface, lower_interface, pname, config_path, name)
    {
        action_functions[ControllerAction::Type::REMOVE_SAVED_REQ] = [&](void* ptr) {removedSaveRequest(ptr);};   
    }

    CacheControllerExclusive::~CacheControllerExclusive()
    {}

    void CacheControllerExclusive::removedSaveRequest(void *data_ptr)
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