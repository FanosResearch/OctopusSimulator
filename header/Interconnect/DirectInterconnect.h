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

#include <list>
#include <vector>
#include <map>

using namespace std;

namespace ns3
{
    class DirectInterconnect : public ClockedObj
    {
    protected:
        vector<CommunicationInterface *> m_interfaces;
        map<int, vector<int>> m_topology;

        DirectController* interconnect_controller;

        int m_interconnect_cycle;
        int m_interconnect_cycle_edges;

    public:
        DirectInterconnect(int lower_id, int upper_id, int buffers_max_size);
        ~DirectInterconnect();

        void cycleProcess();

        CommunicationInterface* getInterfaceFor(int id);
        
        virtual void init();
    };
}

#endif /* _DIRECT_INTERCONNECT_H */
