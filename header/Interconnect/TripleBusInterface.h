/*
 * File  :      TripleBusInterface.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 15, 2022
 */

#ifndef _TRIPLEBUSINTERFACE_H
#define _TRIPLEBUSINTERFACE_H

#include "BusInterface.h"

using namespace std;

namespace ns3
{
    class TripleBusInterface : public BusInterface
    {
    protected:
        vector<Message> m_tx_service_buffer;
        vector<Message> m_rx_service_buffer;

    public:
        TripleBusInterface(int id, int buffer_max_size);

        virtual bool peekMessage(Message *out_msg) override;
        virtual void popFrontMessage() override;

        virtual bool pushMessage(Message &msg, uint64_t cycle, MessageType type = MessageType::REQUEST) override;
        virtual bool pushMessage2RX(Message &msg, MessageType type = MessageType::REQUEST) override;
        
        static void getCongregatedServiceBuffers(vector<CommunicationInterface *>& interfaces, vector<vector<Message>*>* buffers);
    };
}

#endif /* _TRIPLEBUSINTERFACE_H */
