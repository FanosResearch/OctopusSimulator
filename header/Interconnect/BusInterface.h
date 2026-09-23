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

namespace octopus
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

        // Bus-delivery order. Every pushMessage2RX (a delivery by a bus controller)
        // takes the next value of one global counter, so two messages delivered to
        // ANY interfaces compare the same way everywhere: this is the total order the
        // bus imposes, which every snooping controller must observe identically. The
        // per-buffer vectors run parallel to the RX buffers (same index).
        static uint64_t s_rx_delivery_seq;
        vector<uint64_t> m_rx_request_seq;
        vector<uint64_t> m_rx_response_seq;

    public:
        BusInterface(int id, int buffer_max_size);

        virtual bool peekMessage(Message *out_msg) override;
        virtual void popFrontMessage() override;
        virtual bool pushMessage(Message &msg, uint64_t cycle, MessageType type = MessageType::REQUEST) override;
        virtual bool pushMessage2RX(Message &msg, MessageType type = MessageType::REQUEST) override;
        virtual bool canAcceptRX(MessageType type = MessageType::REQUEST) override;
        virtual int txResponseFreeSlots() override { return m_buffer_max_size - (int)m_tx_response_buffer.size(); }

        static void getCongregatedBuffers(vector<CommunicationInterface *>& interfaces, bool request_buffer, vector<vector<Message>*>* buffers);
        
        virtual bool rollback(uint64_t address, uint64_t mask, Message *out_msg) override;
    };
}

#endif /* _BUSINTERFACE_H */
