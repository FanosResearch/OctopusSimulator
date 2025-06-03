/*
 * File  :      BusInterface.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 5, 2022
 */

#ifndef _BUSINTERFACE_H
#define _BUSINTERFACE_H

#include "CommunicationInterface.h"

#include <vector>

using namespace std;

namespace ns3
{
    class BusInterface : public CommunicationInterface
    {
    protected:
        int m_buffer_selector;
        int m_buffer_max_size;

        vector<Message> m_tx_request_buffer;
        vector<Message> m_tx_response_buffer;
        vector<Message> m_rx_request_buffer;
        vector<Message> m_rx_response_buffer;

    public:
        BusInterface(int id, int buffer_max_size);

        virtual bool peekMessage(Message *out_msg) override;
        virtual void popFrontMessage() override;
        virtual bool pushMessage(Message &msg, uint64_t cycle, MessageType type = MessageType::REQUEST) override;
        virtual bool pushMessage2RX(Message &msg, MessageType type = MessageType::REQUEST) override;

        static void getCongregatedBuffers(vector<CommunicationInterface *>& interfaces, bool request_buffer, vector<vector<Message>*>* buffers);
        
        virtual bool rollback(uint64_t address, uint64_t mask, Message *out_msg) override;
    };
}

#endif /* _BUSINTERFACE_H */
