/*
 * File  :      TripleBus.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 16, 2022
 */

#ifndef _TRIPLE_BUS_H
#define _TRIPLE_BUS_H

#include "Bus.h"
#include "TripleBusController.h"
#include "TripleBusInterface.h"

using namespace std;

namespace octopus
{
    class TripleBus : public Bus
    {
    protected:

    public:
        TripleBus(ParametersMap map, string pname = "",
                  string config_path = string(CONFIGURATION_PATH) + string(INTERCONNECT),
                  string name = STRINGIFY(TripleBus));
        ~TripleBus();
    };
}

#endif /* _TRIPLE_BUS_H */
