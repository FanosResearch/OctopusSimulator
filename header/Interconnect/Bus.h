/*
 * File  :      Bus.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 5, 2022
 */

#ifndef _BUS_H
#define _BUS_H

#include "ClockManager.h"
#include "Configurable.h"
#include "CacheXml.h"
#include "CommunicationInterface.h"
#include "BusController.h"
#include "SplitBusController.h"
#include "UnifiedBusController.h"
#include "Point2PointController.h"

#include <list>
#include <vector>
#include <map>

using namespace std;

namespace octopus
{
    class Bus : public ClockedObj, public Configurable
    {
    protected:
        vector<CommunicationInterface *> m_interfaces;
        vector<int> m_lower_level_ids;
        map<int, vector<int>> m_topology;

        BusController* interconnect_controller;
        DebugPrint* dprint;

        int m_bus_cycle;
        int m_bus_cycle_edges;

    public:
        Bus(ParametersMap map, 
            vector<int>* candidates_id = NULL,
            string pname = "",
            string config_path = string(CONFIGURATION_PATH) + string(INTERCONNECT),
            string name = STRINGIFY(Bus));
        ~Bus();

        void cycleProcess();

        CommunicationInterface* getInterfaceFor(int id);

        vector<int> *getLowerLevelIds();
        
        virtual void init();
    };
}

#endif /* _BUS_H */
