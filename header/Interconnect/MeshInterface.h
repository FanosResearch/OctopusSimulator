/*
 * File  :      MeshInterface.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 27, 2023
 */

#ifndef _MESHINTERFACE_H
#define _MESHINTERFACE_H

#include "CommunicationInterface.h"

#include <vector>

using namespace std;

namespace octopus
{
    class MeshInterface : public CommunicationInterface
    {
    protected:
        int m_buffer_selector;
        int m_buffer_max_size;

        vector<int> m_link_ids;
        vector<vector<Message>> m_tx_request_buffer;
        vector<vector<Message>> m_tx_data_buffer;

        vector<Message> m_rx_request_buffer;
        vector<Message> m_rx_data_buffer;

    public:
        MeshInterface(int id, int buffer_max_size, vector<int>& link_ids);

        virtual bool peekMessage(Message *out_msg) override;
        virtual void popFrontMessage() override;
        virtual bool pushMessage(Message &msg, uint64_t cycle, MessageType type = MessageType::REQUEST) override; //Ignore type
        virtual bool pushMessage2RX(Message &msg, MessageType type = MessageType::REQUEST) override; //Ignore type

        static void getCongregatedBuffers(vector<CommunicationInterface *>& interfaces, bool request_buffer, vector<vector<Message>*>* buffers);
        
        virtual bool rollback(uint64_t address, uint64_t mask, Message *out_msg) override;
    };
}

#endif /* _MESHINTERFACE_H */
