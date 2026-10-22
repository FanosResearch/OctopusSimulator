/*
 * File  :      MultiCoreSystem_Mesh.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 30, 2023
 */

#include "../../header/SystemConfigurations/MultiCoreSystem_Mesh.h"
#include "../../header/Interconnect/TileLink/TLMemSlave.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace std;

namespace octopus
{   
    // Memory over TileLink (docs/TileLink_Plan.md s5.3 I-B): the memory crossbar is a second
    // TileLinkInterconnect. TileLinkInterconnect's own atexit handler finishes one instance (the
    // last built), so this one, registered after it and therefore run before it, finishes both,
    // the inner crossbar first: each prints its monitors' verdict. finish() runs once per instance.
    static TileLinkInterconnect *s_tl_inner = nullptr, *s_tl_memory = nullptr;
    static void finishTileLinkMemory()
    {
        if (s_tl_inner) s_tl_inner->finish();
        if (s_tl_memory) s_tl_memory->finish();
    }

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
        TileLinkInterconnect *m_tilelink = nullptr;
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
        else if(interconnect_type == STRINGIFY(TileLink))
        {
            // TileLink over the library's crossbar (docs/TileLink_Plan.md s5.3 I-A): the L1s are the
            // masters and the LLC the slave unless the preset lists them (master_ids, slave_ids); memory
            // stays on the point-to-point memory bus.
            ParametersMap tl_map = getSubMap(STRINGIFY(interconnect));
            auto unset = [&](const char *k) {
                auto it = tl_map.find(k);
                return it == tl_map.end() || std::get<vector<int>>(it->second.value).empty() || std::get<vector<int>>(it->second.value)[0] < 0;
            };
            int banks = parameters.count("llc_banks") ? std::get<int>(parameters.at("llc_banks").value) : 1;
            if (unset("master_ids"))
            {
                vector<int> ids;
                for (int i = 0; i < num_cores; i++) ids.push_back(std::get<int>(getSubMap(STRINGIFY(cache_controller), i).at("m_id").value));
                tl_map.erase("master_ids");
                tl_map.insert({"master_ids", Parameter("master_ids", Parameter::Vector_Integer, ids)});
            }
            if (unset("slave_ids"))
            {
                // the LLC, or its banks in llc_controller[k] order (= the cores' home_ids order): the
                // crossbar then interleaves them by line (TileLinkInterconnect, docs/TileLink_Plan.md s5.3 I-A)
                vector<int> ids;
                for (int k = 0; k < banks; k++)
                    ids.push_back(std::get<int>((banks == 1 ? getSubMap(STRINGIFY(llc_controller)) : getSubMap(STRINGIFY(llc_controller), k)).at("m_id").value));
                tl_map.erase("slave_ids");
                tl_map.insert({"slave_ids", Parameter("slave_ids", Parameter::Vector_Integer, ids)});
            }
            // the bank map's granularity: the cores' home_interleave_shift unless the preset sets the crossbar's
            ParametersMap core0 = getSubMap(STRINGIFY(cache_controller), 0);
            if (banks > 1 && !tl_map.count("home_interleave_shift") && core0.count("home_interleave_shift"))
                tl_map.insert({"home_interleave_shift", core0.at("home_interleave_shift")});
            TileLinkInterconnect *tl_ic = new TileLinkInterconnect(tl_map, name);
            m_tilelink = tl_ic;
            interfaceFor = [tl_ic](int id) { return tl_ic->getInterfaceFor(id); };
        }
        else
        {
            cerr << "MultiCoreSystem_Mesh: unknown interconnect_type '" << interconnect_type << "' (Mesh, NoC, Network, TileLink)" << endl;
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
        // point-to-point memory bus. With interconnect_type TileLink the same key means memory over
        // TileLink (docs/TileLink_Plan.md s5.3 I-B): the memory ports and the controllers are the
        // masters and slaves of a second crossbar on TL-UH links.
        bool memory_on_network = parameters.count("memory_on_network") && std::get<int>(parameters.at("memory_on_network").value) != 0;
        bool memory_on_tilelink = memory_on_network && interconnect_type == STRINGIFY(TileLink);
        if (memory_on_network && interconnect_type != STRINGIFY(Network) && !memory_on_tilelink)
        {
            cerr << "MultiCoreSystem_Mesh: memory_on_network needs interconnect_type Network (or TileLink: memory over TileLink)" << endl;
            exit(2);
        }
        if (memory_on_network && !memory_on_tilelink)
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
        TileLinkInterconnect *mem_tilelink = nullptr;
        if (memory_on_tilelink)
            mem_tilelink = buildMemoryTileLink(llc_banks, name);
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
                if (memory_on_tilelink)   // the bank's memory side is a TL-UH master (plan s5.3 I-B)
                    memory_side = new TLMemMaster(mem_tilelink->port(std::get<int>(lmap.at("mem_port_id").value)));
                else
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
        std::vector<int> memory_ids = memoryIds(llc_banks);
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
        for (int mem_id : memory_ids)
        {
            CommunicationInterface *mem_iface = memory_on_tilelink ? new TLMemSlave(mem_tilelink->port(mem_id))   // plan s5.3 I-B
                                              : memory_on_network ? interfaceFor(mem_id) : bus->getInterfaceFor(mem_id);
            if (main_memory_type == "MCsim")
                new MCsimInterface(mem_iface, mem_id, first_llc, num_cores, /*block_size=*/64, m_llc_period, mem_system,
                                   (int)memory_ids.size(), mem_shift);
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
        if (m_tilelink)
            m_tilelink->elaborate(BaseController::controllers());   // one clock per kernel (plan s3.2), the kernel's elaboration
        if (mem_tilelink)
        {
            mem_tilelink->elaborate(BaseController::controllers());   // the memory crossbar: the same clock (plan s3.2)
            s_tl_inner = m_tilelink; s_tl_memory = mem_tilelink;
            static bool registered = false;
            if (!registered) { std::atexit(finishTileLinkMemory); registered = true; }
        }

        Logger::getLogger()->registerReportPath(workload_path + string("/newLogger"));
    }

    // the memory controllers: m_main_memory.m_id, or the union of the banks' mem_ids (spec s25.4)
    std::vector<int> MultiCoreSystem_Mesh::memoryIds(int llc_banks)
    {
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
        return memory_ids;
    }

    // Memory over TileLink (docs/TileLink_Plan.md s5.3 I-B): a second crossbar of the same kernel
    // kind, separate from the L1s'. One crossbar cannot carry both: it routes A by address alone,
    // and the same line must reach the LLC from an L1 and memory from the LLC. Separate, the outer
    // A/D share no crossbar output stage and no TLPort queue with the inner A..E, so the agent graph
    // L1 -> crossbar -> LLC -> memory crossbar -> memory is acyclic (plan s5.3: the inner and outer
    // links decoupled, as in TileLink s5.1's DMA example); the outer D drains because the LLC sinks
    // every fill and acknowledgement, the outer A because memory takes every write and completes a
    // read only with a D slot free (spec s25.3).
    // Keys: the mem_interconnect sub-map over configuration/Interconnect/TileLink.csv; the system sets
    //   master_ids  the banks' mem_port_id, slave_ids the memory controllers (memoryIds),
    //   memory_ids  all of them (every crossing logs as MEM_BUS, spec s25.5),
    //   level       UH unless the preset names one (Get/PutFullData of a block need bursts),
    //   routes      several controllers: the interleave of the banks' mem_ids / mem_interleave_shift
    //               (spec s25.4), so that the wire routes every line to the controller the bank addresses,
    //   tl_stats    0: TLStats.csv is the inner crossbar's.
    TileLinkInterconnect *MultiCoreSystem_Mesh::buildMemoryTileLink(int llc_banks, const string &pname)
    {
        std::vector<int> ports, memory_ids = memoryIds(llc_banks);
        for (int k = 0; k < llc_banks; k++)
        {
            ParametersMap lmap = llc_banks == 1 ? getSubMap(STRINGIFY(llc_controller)) : getSubMap(STRINGIFY(llc_controller), k);
            if (!lmap.count("mem_port_id") || std::get<int>(lmap.at("mem_port_id").value) < 0)
            {
                cerr << "MultiCoreSystem_Mesh: memory_on_network: LLC " << std::get<int>(lmap.at("m_id").value) << " has no mem_port_id" << endl;
                exit(2);
            }
            ports.push_back(std::get<int>(lmap.at("mem_port_id").value));
        }
        ParametersMap mm = getSubMap("mem_interconnect");
        auto put = [&](const Parameter &p) { mm.erase(p.name); mm.insert({p.name, p}); };
        std::vector<int> all = ports;
        all.insert(all.end(), memory_ids.begin(), memory_ids.end());
        put(Parameter("master_ids", Parameter::Vector_Integer, ports));
        put(Parameter("slave_ids", Parameter::Vector_Integer, memory_ids));
        put(Parameter("memory_ids", Parameter::Vector_Integer, all));
        put(Parameter("tl_stats", Parameter::Integer, 0));
        if (!mm.count("level")) put(Parameter("level", Parameter::String, string("UH")));
        if (memory_ids.size() > 1 && !mm.count("routes"))
        {
            ParametersMap bank0 = llc_banks == 1 ? getSubMap(STRINGIFY(llc_controller)) : getSubMap(STRINGIFY(llc_controller), 0);
            int shift = bank0.count("mem_interleave_shift") ? std::get<int>(bank0.at("mem_interleave_shift").value) : 0;
            put(Parameter("routes", Parameter::Vector_String, memoryRoutes(memory_ids, shift)));
        }
        return new TileLinkInterconnect(mm, pname);
    }

    // The memory crossbar's address map for several controllers: line L goes to
    // memory_ids[(L >> shift) mod M], as the banks' homeOf addresses it (spec s25.4), in the Xbar's
    // interleave form <slave>:interleave:<shift>:<count>:<index> (agents/Xbar.h).
    std::vector<std::string> MultiCoreSystem_Mesh::memoryRoutes(const std::vector<int> &memory_ids, int shift)
    {
        std::vector<std::string> routes;
        for (size_t k = 0; k < memory_ids.size(); k++)
            routes.push_back(std::to_string(memory_ids[k]) + ":interleave:" + std::to_string(shift) + ":" +
                             std::to_string(memory_ids.size()) + ":" + std::to_string(k));
        return routes;
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