/*
 * File  :      DirectController.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 10, 2022
 */

#ifndef _DIRECT_CONTROLLER_H
#define _DIRECT_CONTROLLER_H

#include "BusInterface.h"

#include "CommunicationInterface.h"
#include "Logger.h"

#include <vector>

using namespace std;

namespace ns3
{
    class DirectController
    {
    protected:
        vector<CommunicationInterface *> *m_interfaces;
        
        vector<vector<Message>*> m_request_buffers;
        vector<vector<Message>*> m_response_buffers;

        virtual bool send(Message &msg, int destination, MessageType type = MessageType::DATA_RESPONSE);

    public:
        DirectController(vector<CommunicationInterface *> *interfaces);
        ~DirectController() {};

        virtual void controllerStep(uint64_t cycle_number);
    };
}

#endif /* _DIRECT_CONTROLLER_H */
