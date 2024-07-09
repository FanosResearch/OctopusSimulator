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

namespace ns3
{
    class Bus : public ClockedObj
    {
    protected:
        vector<CommunicationInterface *> m_interfaces;
        vector<int> m_lower_level_ids;
        map<int, vector<int>> m_topology;

        BusController* interconnect_controller;

        int m_bus_cycle;
        int m_bus_cycle_edges;

    public:
        Bus();
        Bus(list<CacheXml>& lower_level_caches, list<CacheXml>& upper_level_caches, int buffers_max_size);
        Bus(list<CacheXml> &lower_level_caches, int upper_level_id, int buffers_max_size, vector<int>* candidates_id = NULL);
        ~Bus();

        void cycleProcess();

        CommunicationInterface* getInterfaceFor(int id);

        vector<int> *getLowerLevelIds();
        
        virtual void init();
    };
}

#endif /* _BUS_H */
