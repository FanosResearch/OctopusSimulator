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

        int m_cycle;
        int m_cycle_edges;

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
