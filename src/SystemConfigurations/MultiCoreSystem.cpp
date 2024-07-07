/*
 * File  :      MultiCoreSystem.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 1, 2023
 */

#include "../../header/SystemConfigurations/MultiCoreSystem.h"

using namespace std;

namespace octopus
{   
    MultiCoreSystem::MultiCoreSystem(vector<string> cl_params) : 
        Configurable(cl_params, string(CONFIGURATION_PATH) + string(SYSTEM_CONFIGURATIONS), STRINGIFY(MultiCoreSystem))
    {
        string name = STRINGIFY(MultiCoreSystem);
        //Parameters initialization
        string workload_path = std::get<string>(parameters.at(STRINGIFY(workload_path)).value);
        int num_cores = std::get<int>(parameters.at(STRINGIFY(num_cores)).value);
        vector<string> bus_type = std::get<vector<string>>(parameters.at(STRINGIFY(bus_type)).value);
        string cache_controller_type = std::get<string>(parameters.at(STRINGIFY(cache_controller_type)).value);

        //Constructor
        Bus *bus[bus_type.size()];
        for(int i = 0; i < bus_type.size(); i++)
        {
            if(bus_type[i] == "TripleBus")
                bus[i] = new TripleBus(getSubMap(STRINGIFY(bus), i), name);
            else if(bus_type[i] == "Bus")
                bus[i] = new Bus(getSubMap(STRINGIFY(bus), i), NULL, name);
            else
            {
                cout << "Error unsupported bus type" << endl;
                exit(0);
            }
        }

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
                                                bus[0]->getInterfaceFor(cache_id),
                                                cpu_interconnect->getInterfaceFor(cache_id), name);
        }

        BaseController *llc_controller;
        int llc_id = std::get<int>(getSubMap(STRINGIFY(llc_controller)).at("m_id").value);
        llc_controller = createController(cache_controller_type, 
                                          getSubMap(STRINGIFY(llc_controller)), 
                                          bus[1]->getInterfaceFor(llc_id), 
                                          bus[0]->getInterfaceFor(llc_id), name);

        MainMemoryController *m_main_memory;
        int main_memory_id = std::get<int>(getSubMap(STRINGIFY(m_main_memory)).at("m_id").value);
        m_main_memory = new MainMemoryController(getSubMap(STRINGIFY(MainMemoryController)), bus[1]->getInterfaceFor(main_memory_id), name);

        Logger::getLogger()->registerReportPath(workload_path + string("/newLogger"));
    }

    BaseController* MultiCoreSystem::createController(string type, ParametersMap map,
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