/*
 * File  :      CacheController_End2End.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On May 22, 2022
 */

#ifndef _CacheController_End2End_H
#define _CacheController_End2End_H

#include "CacheController.h"
#include <map>

namespace ns3
{
    class CacheController_End2End : public CacheController
    {
    protected:
        int m_owner_of_latest_data;
        map <uint64_t, uint16_t> m_wb_cores;
        map <uint64_t, uint64_t> m_wb_address;

        virtual void addRequests2ProcessingQueue(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &buf) override;
        
        virtual void callActionFunction(ControllerAction) override;

        virtual void sendBusRequest(void *) override;
        virtual void performWriteBack(void *) override;
        virtual void sendInvalidationMessage(void *);
        virtual void checkReplacements(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &buf);

    public:
        CacheController_End2End(CacheXml &cacheXml, string &fsm_path,
                                CommunicationInterface *upper_interface, CommunicationInterface *lower_interface,
                                bool cach2Cache, vector<int> sharedMemId, CohProtType pType, int order, vector<int> *private_caches_id = NULL);
        ~CacheController_End2End();

        virtual void initialize(uint64_t address, const uint8_t* data, int size) override; //for Initializable
    };
}

#endif /* _CacheController_End2End_H */
