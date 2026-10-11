/*
 * File  :      MultiCoreSystem_Mesh.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 30, 2023
 */

#include "../../header/SystemConfigurations/MultiCoreSystem_Mesh.h"
#include <algorithm>
#include <iostream>

using namespace std;

namespace octopus
{   
    MultiCoreSystem_Mesh::MultiCoreSystem_Mesh(vector<string> cl_params, string config_path, string config_name) :
        Configurable(cl_params, config_path, config_name)
    {
        string name = STRINGIFY(MultiCoreSystem_Mesh);
        //Parameters initialization
        string workload_path = std::get<string>(parameters.at(STRINGIFY(workload_path)).value);
        int num_cores = std::get<int>(parameters.at(STRINGIFY(num_cores)).value);
        string cache_controller_type = std::get<string>(parameters.at(STRINGIFY(cache_controller_type)).value);
        string interconnect_type = std::get<string>(parameters.at(STRINGIFY(interconnect_type)).value);

        //Constructor
        std::function<CommunicationInterface *(int)> interfaceFor;
        net::Network *m_network = nullptr;
        if(interconnect_type == STRINGIFY(Mesh))
        {
            Mesh *mesh = new Mesh(getSubMap(STRINGIFY(interconnect)), name);
            interfaceFor = [mesh](int id) { return mesh->getInterfaceFor(id); };
        }
        else if(interconnect_type == STRINGIFY(NoC))
        {
            NoC *noc = new NoC(getSubMap(STRINGIFY(interconnect)), name);
            interfaceFor = [noc](int id) { return noc->getInterfaceFor(id); };
        }
        else if(interconnect_type == STRINGIFY(Network))
        {
            // The multi-hop network (docs/NoC_Architecture_Spec.md s3.3). Two values come from the
            // system level: the home ids (default: the cores' m_shared_memory_id values) and the
            // protocol's ordering requirement (requires_ordering, set by the presets).
            ParametersMap net_map = getSubMap(STRINGIFY(interconnect));
            auto h = net_map.find("home_ids");
            bool have_homes = h != net_map.end() && !std::get<vector<int>>(h->second.value).empty() && std::get<vector<int>>(h->second.value)[0] >= 0;
            if (!have_homes)
            {
                vector<int> homes;
                for (int i = 0; i < num_cores; i++)
                {
                    ParametersMap cmap = getSubMap(STRINGIFY(cache_controller), i);
                    vector<int> ids;
                    if (cmap.count("home_ids")) ids = std::get<vector<int>>(cmap.at("home_ids").value);
                    if (ids.empty() || ids[0] < 0) ids = {std::get<int>(cmap.at("m_shared_memory_id").value)};
                    for (int sid : ids) if (std::find(homes.begin(), homes.end(), sid) == homes.end()) homes.push_back(sid);
                }
                net_map.erase("home_ids");
                net_map.insert({"home_ids", Parameter("home_ids", Parameter::Vector_Integer, homes)});
            }
            net::Network *net = new net::Network(net_map, name);
            m_network = net;
            if (parameters.count("requires_ordering"))
                net->requireOrdering(std::get<string>(parameters.at("requires_ordering").value));
            interfaceFor = [net](int id) { return net->getInterfaceFor(id); };
        }
        else
        {
            cerr << "MultiCoreSystem_Mesh: unknown interconnect_type '" << interconnect_type << "' (Mesh, NoC, Network)" << endl;
            exit(2);
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
                                                interfaceFor(cache_id),
                                                cpu_interconnect->getInterfaceFor(cache_id), name);
        }

        Bus *bus = new Bus(getSubMap(STRINGIFY(bus)), NULL, name);
        bus->setMemBus();   // the LLC<->memory link: its crossings log as MEM_BUS (as in MultiCoreSystem)
        // LLC banks (spec s24): llc_banks(i) controllers, llc_controller[k] when banked, the plain
        // llc_controller map when there is one (every existing preset). Each bank is a complete LLC
        // controller with its own endpoint; the cores' home_ids map lines onto them.
        int llc_banks = parameters.count("llc_banks") ? std::get<int>(parameters.at("llc_banks").value) : 1;
        for (int k = 0; k < llc_banks; k++)
        {
            ParametersMap lmap = llc_banks == 1 ? getSubMap(STRINGIFY(llc_controller)) : getSubMap(STRINGIFY(llc_controller), k);
            int llc_id = std::get<int>(lmap.at("m_id").value);
            BaseController *llc_controller = createController(cache_controller_type, lmap, bus->getInterfaceFor(llc_id), interfaceFor(llc_id), name);
            // Without this the LLC's events carry no role: the LatencyReport's DRAM and LLC
            // stages read 0 on Mesh/NoC and the event-path validator reports thousands of rows
            // whose stages do not sum to Total. Mirrors MultiCoreSystem.
            llc_controller->setLogRole(Logger::Role::LLC);
        }

        MainMemoryController *m_main_memory;
        int main_memory_id = std::get<int>(getSubMap(STRINGIFY(m_main_memory)).at("m_id").value);
        m_main_memory = new MainMemoryController(getSubMap(STRINGIFY(MainMemoryController)), bus->getInterfaceFor(main_memory_id), name);

        if (interconnect_type == STRINGIFY(Network))
            m_network->checkProtocols(BaseController::controllers());      // spec s14.3: after every controller exists

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