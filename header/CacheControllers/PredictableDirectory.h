/*
 * File  :      PredictableDirectory.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 3, 2023
 */

#ifndef _PredictableDirectory_H
#define _PredictableDirectory_H

#include "CacheControllerDirectory.h"
#include "MOESIDirectory.h"

namespace ns3
{
    class PredictableDirectory : public CacheControllerDirectory
    {
    private:
        bool is_LLC = false;
        bool is_RROF = false;

    protected:
        Message cause_repl_msg;
        
        virtual void cycleProcess() override;
        virtual void processLogic() override;
        virtual void addRequests2ProcessingQueue(ProcessingBuffer<Message> &buf) override;
        
        virtual void causedReplacement(void *);
        virtual void updateCacheLine(void *) override;
        virtual void writeCacheLineData(void *) override;
        virtual void performWriteBack(void *) override;
        virtual void sendForwardMessage(void *) override;
        virtual void sendBusRequest(void *) override;
        virtual void hitAction(void *) override;
        virtual void removePendingAndRespond(void *) override;

        virtual void checkReplacements(ProcessingBuffer<Message> &) override;

        // key is the msg.m_id and the value is the Message that contains the data
        std::map<uint64_t, Message> m_victim_cache;

    public:
        PredictableDirectory(ParametersMap map, CommunicationInterface *upper_interface, 
                                 CommunicationInterface *lower_interface, string pname = "",
                                 string config_path = string(CONFIGURATION_PATH) + string(CACHECONTROLLERS),
                                 string name = STRINGIFY(PredictableDirectory));
        ~PredictableDirectory() {}
    };
}

#endif /* _PredictableDirectory_H */
