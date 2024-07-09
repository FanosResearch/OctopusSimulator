/*
 * File  :      BusController.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 10, 2022
 */

#ifndef _BUS_CONTROLLER_H
#define _BUS_CONTROLLER_H

#include "BusInterface.h"

#include "CommunicationInterface.h"

#include "Arbiter.h"
#include "TDMArbiter.h"
#include "FCFSArbiter.h"
#include "RRArbiter.h"

#include "Logger.h"

#include <vector>
#include <map>
#include <iostream>

using namespace std;

namespace ns3
{
    class BusController
    {
    public:
        enum class BusType
        {
            RequestBus = 0,
            ResponseBus,
        };

    protected:
        vector<CommunicationInterface *> *m_interfaces;
        vector<int> *m_lower_level_ids;
        vector<Arbiter *> m_arbiters;

        int m_request_latency;
        int m_response_latency;

        virtual void broadcast(Message &msg, MessageType type = MessageType::REQUEST);
        virtual void send(Message &msg, MessageType type = MessageType::DATA_RESPONSE);

    public:
        BusController(vector<CommunicationInterface *> *interfaces, vector<int> *lower_level_ids);
        ~BusController();

        virtual void busStep(uint64_t cycle_number){};
    };
}

#endif /* _BUS_CONTROLLER_H */
