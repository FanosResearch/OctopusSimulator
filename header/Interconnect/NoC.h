/*
 * File  :      NoC.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Jan 1, 2024
 */

#ifndef _NOC_H
#define _NOC_H

#include "Mesh.h"
#include "NoCInterface.h"
#include "NoCController.h"

using namespace std;

namespace octopus
{
    class NoC : public Mesh
    {
    public:
        class Switch
        {
        public:
            int id;
            NoCInterface* interface;

            Switch(int id) : id(id) {}
        };
    
    protected:
        vector<Switch> m_switches;
    
    public:
        NoC(ParametersMap map, string pname = "",
             string config_path = string(CONFIGURATION_PATH) + string(INTERCONNECT),
             string name = STRINGIFY(NoC));
        ~NoC();

        virtual void cycleProcess() override;
    };
}

#endif /* _MESH_H */
