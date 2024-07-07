/*
 * File  :      MultiCoreSystem_Mesh.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 1, 2023
 */

#ifndef _MULTICORE_SYSTEM_MESH_H
#define _MULTICORE_SYSTEM_MESH_H

#include "Configurable.h"
#include "DebugPrint.h"

#include "CPU.h"

#include "Bus.h"
#include "DirectInterconnect.h"
#include "Mesh.h"
#include "NoC.h"

#include "BaseController.h"
#include "CacheController.h"
#include "CacheControllerExclusive.h"
#include "CacheController_End2End.h"
#include "CacheControllerDirectory.h"
#include "MainMemoryController.h"

#include <vector>
#include <string>

namespace octopus
{
    class MultiCoreSystem_Mesh : public Configurable
    {
    protected:
        BaseController* createController(string type, ParametersMap map,
                                         CommunicationInterface *upper_interface, 
                                         CommunicationInterface *lower_interface, 
                                         string pname = "");
    public:
        MultiCoreSystem_Mesh(std::vector<std::string> cl_params);
    };
}

#endif /* _MULTICORE_SYSTEM_H */
