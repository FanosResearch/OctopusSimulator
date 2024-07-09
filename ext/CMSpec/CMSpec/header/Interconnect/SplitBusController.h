/*
 * File  :      SplitBusController.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 10, 2022
 */

#ifndef _SPLIT_BUS_CONTROLLER_H
#define _SPLIT_BUS_CONTROLLER_H

#include "BusController.h"

using namespace std;

namespace ns3
{
    class SplitBusController : public BusController
    {
    protected:
        int clk_in_slot_req;
        Message elected_msg_req;
        bool message_available_req;
        vector<vector<Message> *> buffers_req;
        
        int clk_in_slot_resp;
        Message elected_msg_resp;
        bool message_available_resp;
        vector<vector<Message> *> buffers_resp;

        virtual void requestBusStep(uint64_t cycle_number);
        virtual void responseBusStep(uint64_t cycle_number);

    public:
        SplitBusController(vector<CommunicationInterface *> *interfaces, vector<int> *lower_level_ids);
        ~SplitBusController();

        virtual void busStep(uint64_t cycle_number) override;
    };
}

#endif /* _SPLIT_BUS_CONTROLLER_H */
