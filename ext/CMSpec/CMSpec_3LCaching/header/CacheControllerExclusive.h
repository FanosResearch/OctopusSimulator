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

namespace ns3
{
    class CacheControllerExclusive : public CacheController
    {
    protected:
        virtual void callActionFunction(ControllerAction);

        virtual void removedSaveRequset(void *data_ptr);

    public:
        CacheControllerExclusive(CacheXml &cacheXml, string &fsm_path,
                        CommunicationInterface* upper_interface, CommunicationInterface* lower_interface,
                        bool cach2Cache, int sharedMemId, CohProtType pType);
        ~CacheControllerExclusive();
    };
}

#endif /* _CacheController_H */
