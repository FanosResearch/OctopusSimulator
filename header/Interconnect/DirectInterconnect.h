/*
 * File  :      DirectInterconnect.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 5, 2022
 */

#ifndef _DIRECT_INTERCONNECT_H
#define _DIRECT_INTERCONNECT_H

#include "ClockManager.h"
#include "CommunicationInterface.h"
#include "DirectController.h"
#include "Configurable.h"
#include "DebugPrint.h"

#include <list>
#include <vector>
#include <map>

using namespace std;

namespace octopus
{
    class DirectInterconnect : public ClockedObj, public Configurable
    {
    protected:
        vector<CommunicationInterface *> m_interfaces;
        map<int, vector<int>> m_topology;

        DirectController* interconnect_controller;
        DebugPrint* dprint;

        int m_interconnect_cycle;
        int m_interconnect_cycle_edges;

    public:
        DirectInterconnect(ParametersMap map, int upper_id, int lower_id = -1,
                           string pname = "",
                           string config_path = string(CONFIGURATION_PATH) + string(INTERCONNECT),
                           string name = STRINGIFY(DirectInterconnect));
        ~DirectInterconnect();

        void cycleProcess();

        CommunicationInterface* getInterfaceFor(int id);
        
        virtual void init();
    };
}

#endif /* _DIRECT_INTERCONNECT_H */
