/*
 * File  :      PredictableDirectorySystem.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 3, 2024
 */

#include "../../header/SystemConfigurations/PredictableDirectorySystem.h"

using namespace std;

namespace ns3
{   
    PredictableDirectorySystem::PredictableDirectorySystem(vector<string> cl_params) : 
        Configurable(cl_params, string(CONFIGURATION_PATH) + string(SYSTEM_CONFIGURATIONS), STRINGIFY(PredictableDirectorySystem))
    {
        string name = STRINGIFY(PredictableDirectorySystem);
        //Parameters initialization
        string workload_path = std::get<string>(parameters.at(STRINGIFY(workload_path)).value);
        int num_cores = std::get<int>(parameters.at(STRINGIFY(num_cores)).value);
        string cache_controller_type = std::get<string>(parameters.at(STRINGIFY(cache_controller_type)).value);
        string interconnect_type = std::get<string>(parameters.at(STRINGIFY(interconnect_type)).value);
        string logger_path = std::get<string>(parameters.at(STRINGIFY(logger_path)).value);
        int RROF_size_limit = std::get<int>(parameters.at(STRINGIFY(RROF_size_limit)).value);
        string system_arbiter = std::get<string>(parameters.at(STRINGIFY(system_arbiter)).value);

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
        m_main_memory = new MainMemoryController(getSubMap(STRINGIFY(m_main_memory)), bus->getInterfaceFor(main_memory_id), name);
        m_main_memory->setLineMask(llc_controller->cachelineSize());

        Logger::getLogger()->registerReportPath(logger_path);
        RROFArbiter::setSizeLimit(RROF_size_limit);

        if(system_arbiter == STRINGIFY(FCFSArbiter))
        {
            vector<int> candidates_ids;
            for (int i = 0; i < num_cores; i++)
                candidates_ids.push_back(i);

            new RROFArbiter(&candidates_ids, 0);
            RROFArbiter::disableArbiter();
        }
    }

    BaseController* PredictableDirectorySystem::createController(string type, ParametersMap map,
                                                       CommunicationInterface *upper_interface, 
                                                       CommunicationInterface *lower_interface,
                                                       string pname)
    {

        if(type == STRINGIFY(BaseController))
            return new BaseController(map, upper_interface, lower_interface, pname);
        else if(type == STRINGIFY(PredictableDirectory))
            return new PredictableDirectory(map, upper_interface, lower_interface, pname);
        else
        {
            cout << "Error: wrong cache controller type." << endl;
            exit(0);
            return NULL;
        }
    }
}