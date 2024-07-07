/*
 * File  :      CacheControllerExclusive.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On March 7, 2022
 */

#ifndef _CacheControllerExclusive_H
#define _CacheControllerExclusive_H

#include "CacheController.h"

namespace octopus
{
    class CacheControllerExclusive : public CacheController
    {
    protected:
        virtual void removedSaveRequest(void *data_ptr);

    public:
        CacheControllerExclusive(ParametersMap map, CommunicationInterface *upper_interface, 
                                 CommunicationInterface *lower_interface, string pname = "",
                                 string config_path = string(CONFIGURATION_PATH) + string(CACHECONTROLLERS),
                                 string name = STRINGIFY(CacheControllerExclusive));
        ~CacheControllerExclusive();
    };
}

#endif /* _CacheController_H */
