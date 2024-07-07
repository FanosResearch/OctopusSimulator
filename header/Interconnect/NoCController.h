/*
 * File  :      NoCController.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Jan 1, 2024
 */

#ifndef _NOC_CONTROLLER_H
#define _NOC_CONTROLLER_H

#include "MeshController.h"

using namespace std;

namespace octopus
{
    class NoCController : public MeshController
    {
    protected:
        virtual void send(Message &msg, int to_id);

    public:
        NoCController(ParametersMap pmap, vector<CommunicationInterface *> *interfaces, 
                      map<int, vector<int>> *topology, vector<int>& switch_ids, 
                      string pname = "",
                      string config_path = string(CONFIGURATION_PATH) + string(INTERCONNECT),
                      string name = STRINGIFY(NoCController));

        virtual void step(uint64_t cycle_number) override;
    };
}

#endif /* _MESH_CONTROLLER_H */
