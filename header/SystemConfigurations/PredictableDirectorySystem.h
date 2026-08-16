/*
 * File  :      PredictableDirectorySystem.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 3, 2024
 */

#ifndef _PREDICTABLE_DIRECTORY_SYSTEM_H
#define _PREDICTABLE_DIRECTORY_SYSTEM_H

#include "Configurable.h"
#include "DebugPrint.h"

#include "CPU.h"

#include "Bus.h"
#include "DirectInterconnect.h"
#include "Mesh.h"
#include "NoC.h"

#include "BaseController.h"
#include "PredictableDirectory.h"
#include "MainMemoryController.h"

#include <vector>
#include <string>

namespace ns3
{
    class PredictableDirectorySystem : public Configurable
    {
    protected:
        BaseController* createController(string type, ParametersMap map,
                                         CommunicationInterface *upper_interface, 
                                         CommunicationInterface *lower_interface, 
                                         string pname = "");
    public:
        PredictableDirectorySystem(std::vector<std::string> cl_params);
    };
}

#endif /* _PREDICTABLE_DIRECTORY_SYSTEM_H */
