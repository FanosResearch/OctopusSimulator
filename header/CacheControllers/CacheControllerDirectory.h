/*
 * File  :      CacheControllerDirectory.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 10, 2023
 */

#ifndef _CacheControllerDirectory_H
#define _CacheControllerDirectory_H

#include "CacheController.h"
#include "CacheDataHandler_COTS.h"
#include "DirectoryProtocol.h"

namespace octopus
{
    class CacheControllerDirectory : public CacheController
    {
    protected:
        virtual void addRequests2ProcessingQueue(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &) override;
    
        // key is the addr & mask(nbits of CacheLineSize) and the value is a list of sharers ids
        std::map<uint64_t, std::list<uint64_t>> m_sharers; 
        // key is the addr & mask(nbits of CacheLineSize) and the value is the ack counter
        std::map<uint64_t, int64_t> m_ack_counters; 
        
        virtual void addData2Message(void *);
        virtual void setACKNumber(void *);
        virtual void decrementACK(void *);
        virtual void incrementSharers(void *);
        virtual void decrementSharers(void *);
        virtual void setSharers(void *);
        virtual void clearSharers(void *);
        virtual void sendForwardMessage(void *);
        virtual void removePendingNoResponse(void *);
        
    public:
        CacheControllerDirectory(ParametersMap map, CommunicationInterface *upper_interface, 
                                 CommunicationInterface *lower_interface, string pname = "",
                                 string config_path = string(CONFIGURATION_PATH) + string(CACHECONTROLLERS),
                                 string name = STRINGIFY(CacheControllerDirectory));
        ~CacheControllerDirectory() {}
    };
}

#endif /* _CacheControllerDirectory_H */
