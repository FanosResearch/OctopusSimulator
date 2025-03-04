/*
 * File  :      CacheController_End2End.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On May 22, 2022
 */

#ifndef _CacheController_End2End_H
#define _CacheController_End2End_H

#include "CacheControllers/CacheController.h"

namespace octopus
{
    class CacheController_End2End : public CacheController
    {
    protected:
        int m_owner_of_latest_data;

        virtual void addRequests2ProcessingQueue(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &buf) override;

        virtual void sendBusRequest(void *) override;
        virtual void performWriteBack(void *) override;
        virtual void sendInvalidationMessage(void *);

    public:
        CacheController_End2End(ParametersMap map, CommunicationInterface *upper_interface, 
                                CommunicationInterface *lower_interface, string pname = "",
                                string config_path = string(CONFIGURATION_PATH) + string(CACHECONTROLLERS),
                                string name = STRINGIFY(CacheController_End2End));
        ~CacheController_End2End();

        virtual void initialize(uint64_t address, const uint8_t* data, int size) override; //for Initializable
    };
}

#endif /* _CacheController_End2End_H */
