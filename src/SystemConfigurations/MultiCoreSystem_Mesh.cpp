/*
 * File  :      MultiCoreSystem_Mesh.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 30, 2023
 */

#include "../../header/SystemConfigurations/MultiCoreSystem_Mesh.h"

using namespace std;

namespace octopus
{   
    MultiCoreSystem_Mesh::MultiCoreSystem_Mesh(vector<string> cl_params) : 
        Configurable(cl_params, string(CONFIGURATION_PATH) + string(SYSTEM_CONFIGURATIONS), STRINGIFY(MultiCoreSystem_Mesh))
    {
        string name = STRINGIFY(MultiCoreSystem_Mesh);
        //Parameters initialization
        string workload_path = std::get<string>(parameters.at(STRINGIFY(workload_path)).value);
        int num_cores = std::get<int>(parameters.at(STRINGIFY(num_cores)).value);
        string cache_controller_type = std::get<string>(parameters.at(STRINGIFY(cache_controller_type)).value);
        string interconnect_type = std::get<string>(parameters.at(STRINGIFY(interconnect_type)).value);

        //Constructor
        Mesh* interconnect;
        if(interconnect_type == STRINGIFY(Mesh))
            interconnect = new Mesh(getSubMap(STRINGIFY(interconnect)), name);
        else if(interconnect_type == STRINGIFY(NoC))
            interconnect = new NoC(getSubMap(STRINGIFY(interconnect)), name);

        // iterate over each core
        for (int i = 0; i < num_cores; i++)
        {
            string file_path = workload_path + "/trace_C" + std::to_string(i) + ".trc.shared";
            BaseController *cache_controller;
            int cache_id = std::get<int>(getSubMap(STRINGIFY(cache_controller), i).at("m_id").value);

            DirectInterconnect *cpu_interconnect = new DirectInterconnect(getSubMap(STRINGIFY(cpu_interconnect), i), cache_id, -1, name);

            CPU *cpu = new CPU(getSubMap(STRINGIFY(cpu), i), cache_id, cpu_interconnect->getInterfaceFor(-1), file_path, name);

            cache_controller = createController(cache_controller_type, 
                                                getSubMap(STRINGIFY(cache_controller), i),
                                                interconnect->getInterfaceFor(cache_id),
                                                cpu_interconnect->getInterfaceFor(cache_id), name);
        }

        Bus *bus = new Bus(getSubMap(STRINGIFY(bus)), NULL, name);
        BaseController *llc_controller;
        int llc_id = std::get<int>(getSubMap(STRINGIFY(llc_controller)).at("m_id").value);
        llc_controller = createController(cache_controller_type, 
                                          getSubMap(STRINGIFY(llc_controller)), 
                                          bus->getInterfaceFor(llc_id), 
                                          interconnect->getInterfaceFor(llc_id), name);

        MainMemoryController *m_main_memory;
        int main_memory_id = std::get<int>(getSubMap(STRINGIFY(m_main_memory)).at("m_id").value);
        m_main_memory = new MainMemoryController(getSubMap(STRINGIFY(MainMemoryController)), bus->getInterfaceFor(main_memory_id), name);

        Logger::getLogger()->registerReportPath(workload_path + string("/newLogger"));
    }

    BaseController* MultiCoreSystem_Mesh::createController(string type, ParametersMap map,
                                                       CommunicationInterface *upper_interface, 
                                                       CommunicationInterface *lower_interface,
                                                       string pname)
    {

        if(type == STRINGIFY(BaseController))
            return new BaseController(map, upper_interface, lower_interface, pname);
        else if(type == STRINGIFY(CacheController))
            return new CacheController(map, upper_interface, lower_interface, pname);
        else if(type == STRINGIFY(CacheControllerExclusive))
            return new CacheControllerExclusive(map, upper_interface, lower_interface, pname);
        else if(type == STRINGIFY(CacheController_End2End))
            return new CacheController_End2End(map, upper_interface, lower_interface, pname);
        else if(type == STRINGIFY(CacheControllerDirectory))
            return new CacheControllerDirectory(map, upper_interface, lower_interface, pname);
        else
        {
            cout << "Error: wrong cache controller type." << endl;
            exit(0);
            return NULL;
        }
    }
}