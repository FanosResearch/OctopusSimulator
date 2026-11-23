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
        uint64_t m_llc_period = 0;   // MCsim ticks once per core cycle: the LLC's period (as in MultiCoreSystem)
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

        // Memory on the network (spec s25): each LLC bank reaches memory through its memory port, a
        // second endpoint; the memory controller is an endpoint too. Otherwise the banks share the
        // point-to-point memory bus.
        bool memory_on_network = parameters.count("memory_on_network") && std::get<int>(parameters.at("memory_on_network").value) != 0;
        if (memory_on_network && interconnect_type != STRINGIFY(Network))
        {
            cerr << "MultiCoreSystem_Mesh: memory_on_network needs interconnect_type Network" << endl;
            exit(2);
        }
        if (memory_on_network)
            Logger::getLogger()->enableNocMemColumns();
        Bus *bus = nullptr;
        if (!memory_on_network)
        {
            bus = new Bus(getSubMap(STRINGIFY(bus)), NULL, name);
            bus->setMemBus();   // the LLC<->memory link: its crossings log as MEM_BUS (as in MultiCoreSystem)
        }
        // LLC banks (spec s24): llc_banks(i) controllers, llc_controller[k] when banked, the plain
        // llc_controller map when there is one (every existing preset). Each bank is a complete LLC
        // controller with its own endpoint; the cores' home_ids map lines onto them.
        int llc_banks = parameters.count("llc_banks") ? std::get<int>(parameters.at("llc_banks").value) : 1;
        for (int k = 0; k < llc_banks; k++)
        {
            ParametersMap lmap = llc_banks == 1 ? getSubMap(STRINGIFY(llc_controller)) : getSubMap(STRINGIFY(llc_controller), k);
            int llc_id = std::get<int>(lmap.at("m_id").value);
            CommunicationInterface *memory_side;
            if (memory_on_network)
            {
                if (!lmap.count("mem_port_id") || std::get<int>(lmap.at("mem_port_id").value) < 0)
                {
                    cerr << "MultiCoreSystem_Mesh: memory_on_network: LLC " << llc_id << " has no mem_port_id" << endl;
                    exit(2);
                }
                memory_side = interfaceFor(std::get<int>(lmap.at("mem_port_id").value));
            }
            else
                memory_side = bus->getInterfaceFor(llc_id);
            BaseController *llc_controller = createController(cache_controller_type, lmap, memory_side, interfaceFor(llc_id), name);
            // Without this the LLC's events carry no role: the LatencyReport's DRAM and LLC
            // stages read 0 on Mesh/NoC and the event-path validator reports thousands of rows
            // whose stages do not sum to Total. Mirrors MultiCoreSystem.
            llc_controller->setLogRole(Logger::Role::LLC);
            m_llc_period = llc_controller->getClkPeriod();
        }

        // the memory controllers: m_main_memory.m_id, or the union of the banks' mem_ids (spec s25.4)
        int main_memory_id = std::get<int>(getSubMap(STRINGIFY(m_main_memory)).at("m_id").value);
        std::vector<int> memory_ids;
        for (int k = 0; k < llc_banks; k++)
        {
            ParametersMap lmap = llc_banks == 1 ? getSubMap(STRINGIFY(llc_controller)) : getSubMap(STRINGIFY(llc_controller), k);
            if (lmap.count("mem_ids"))
                for (int id : std::get<vector<int>>(lmap.at("mem_ids").value))
                    if (id >= 0 && std::find(memory_ids.begin(), memory_ids.end(), id) == memory_ids.end()) memory_ids.push_back(id);
        }
        if (memory_ids.empty()) memory_ids.push_back(main_memory_id);
        if (memory_ids.size() > 1 && !memory_on_network)
        {
            cerr << "MultiCoreSystem_Mesh: several memory controllers (mem_ids) need memory_on_network" << endl;
            exit(2);
        }
        // the basic MainMemoryController by default, or MCsim (main_memory_type MCsim, mcsim_scheduler), one
        // instance per controller; with several, each sees the line addresses with the controller-select bits
        // removed (spec s25.4)
        string main_memory_type = STRINGIFY(MainMemoryController);
        if (parameters.count(STRINGIFY(main_memory_type)))
            main_memory_type = std::get<string>(parameters.at(STRINGIFY(main_memory_type)).value);
        string mem_system = "FRFCFS";
        if (parameters.count(STRINGIFY(mcsim_scheduler)))
            mem_system = std::get<string>(parameters.at(STRINGIFY(mcsim_scheduler)).value);
        ParametersMap bank0 = llc_banks == 1 ? getSubMap(STRINGIFY(llc_controller)) : getSubMap(STRINGIFY(llc_controller), 0);
        int mem_shift = bank0.count("mem_interleave_shift") ? std::get<int>(bank0.at("mem_interleave_shift").value) : 0;
        int first_llc = std::get<int>(bank0.at("m_id").value);
        uint64_t core_clock_mhz = parameters.count(STRINGIFY(mcsim_core_clock_mhz))
            ? (uint64_t)std::get<int>(parameters.at(STRINGIFY(mcsim_core_clock_mhz)).value) : 2400;
        for (int mem_id : memory_ids)
        {
            CommunicationInterface *mem_iface = memory_on_network ? interfaceFor(mem_id) : bus->getInterfaceFor(mem_id);
            if (main_memory_type == "MCsim")
                new MCsimInterface(mem_iface, mem_id, first_llc, num_cores, /*block_size=*/64, m_llc_period, mem_system,
                                   (int)memory_ids.size(), mem_shift, core_clock_mhz);
            else
            {
                ParametersMap mmap = getSubMap(STRINGIFY(MainMemoryController));
                mmap.erase("m_id");
                mmap.insert({"m_id", Parameter("m_id", Parameter::Integer, mem_id)});
                new MainMemoryController(mmap, mem_iface, name);
            }
        }

        if (interconnect_type == STRINGIFY(Network))
            m_network->checkProtocols(BaseController::controllers(), memory_on_network);      // spec s14.3, s25.3: after every controller exists

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