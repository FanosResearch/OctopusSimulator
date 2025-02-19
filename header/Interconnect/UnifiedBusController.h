/*
 * File  :      UnifiedBusController.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 10, 2022
 */

#ifndef _UNIFIED_BUS_CONTROLLER_H
#define _UNIFIED_BUS_CONTROLLER_H

#include "BusController.h"

using namespace std;

namespace octopus
{
    class UnifiedBusController : public BusController
    {
    protected:
        int clk_in_slot;
        Message elected_msg;
        bool message_available;

        virtual void requestBusStep(uint64_t cycle_number);

    public:
        UnifiedBusController(ParametersMap map, vector<CommunicationInterface *> *interfaces, vector<int> *lower_level_ids,
                             string pname = "",
                             string config_path = string(CONFIGURATION_PATH) + string(INTERCONNECT),
                             string name = STRINGIFY(UnifiedBusController));
        ~UnifiedBusController();

        virtual void busStep(uint64_t cycle_number) override;
    };
}

#endif /* _UNIFIED_BUS_CONTROLLER_H */
