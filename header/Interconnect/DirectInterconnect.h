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

        uint64_t m_interconnect_cycle;
        uint64_t m_interconnect_cycle_edges; // MUST be unsigned: signed int wrapped negative
                                             // at 2^31 edges (~1.07B cyc) and `edges % 2 == 1`
                                             // went false forever -> interconnect stopped moving
                                             // messages -> giant-trace deadlock.

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
