/*
 * File  :      TripleBusController.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 15, 2022
 */

#ifndef _TRIPLE_BUS_CONTROLLER_H
#define _TRIPLE_BUS_CONTROLLER_H

#include "SplitBusController.h"
#include "TripleBusInterface.h"

using namespace std;

//It is a split bus plus a service bus between LCC and private caches
namespace octopus
{
    class TripleBusController : public SplitBusController
    {
    protected:
        void serviceBusStep(uint64_t cycle_number);

    public:
        TripleBusController(ParametersMap map, vector<CommunicationInterface *> *interfaces, vector<int> *lower_level_ids,
                            string pname = "",
                            string config_path = string(CONFIGURATION_PATH) + string(INTERCONNECT),
                            string name = STRINGIFY(TripleBusController));
        ~TripleBusController();

        virtual void busStep(uint64_t cycle_number) override;
    };
}

#endif /* _TRIPLE_BUS_CONTROLLER_H */
