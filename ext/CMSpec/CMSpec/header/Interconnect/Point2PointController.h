/*
 * File  :      Point2PointController.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 10, 2022
 */

#ifndef _Point2Point_CONTROLLER_H
#define _Point2Point_CONTROLLER_H

#include "BusController.h"

using namespace std;

namespace ns3
{
    class Point2PointController : public BusController //it supports one2one or many2one
    {
    protected:        
        int clk_in_slot_lower;
        int clk_in_slot_upper;
        
        Message elected_msg_lower;
        Message elected_msg_upper;

        bool message_available_lower;
        bool message_available_upper;

        vector<vector<Message> *> buffers_lower;
        vector<vector<Message> *> buffers_upper;

        virtual void lowerBusStep(uint64_t cycle_number);
        virtual void upperBusStep(uint64_t cycle_number);

    public:
        Point2PointController(vector<CommunicationInterface *> *interfaces, vector<int> *lower_level_ids, vector<int>* candidates_id = NULL);
        ~Point2PointController();

        virtual void busStep(uint64_t cycle_number) override;
    };
}

#endif /* _Point2Point_CONTROLLER_H */
