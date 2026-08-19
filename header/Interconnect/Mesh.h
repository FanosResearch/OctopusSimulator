/*
 * File  :      Mesh.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 27, 2023
 */

#ifndef _MESH_H
#define _MESH_H

#include "ClockManager.h"
#include "Configurable.h"
#include "CommunicationInterface.h"
#include "MeshController.h"

#include <vector>
#include <map>

using namespace std;

namespace octopus
{
    class Mesh : public ClockedObj, public Configurable
    {
    protected:
        vector<CommunicationInterface *> m_interfaces;
        map<int, vector<int>> m_topology;

        MeshController* interconnect_controller;
        DebugPrint* dprint;

        uint64_t m_cycle;
        uint64_t m_cycle_edges; // MUST be unsigned (see DirectInterconnect.h): signed
                                // overflow of the edge counter froze the mesh on giant traces.

    public:
        Mesh(ParametersMap map, string pname = "",
             string config_path = string(CONFIGURATION_PATH) + string(INTERCONNECT),
             string name = STRINGIFY(Mesh),
             bool init_interfaces = true);
        ~Mesh();

        virtual void cycleProcess();

        CommunicationInterface* getInterfaceFor(int id);
        vector<int> *getLowerLevelIds();
        
        virtual void init();
    };
}

#endif /* _MESH_H */
