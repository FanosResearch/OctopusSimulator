/*
 * File  :      MultiCoreSystem.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 1, 2023
 */

#include <algorithm>
#include "../../header/SystemConfigurations/MultiCoreSystem.h"

using namespace std;

namespace octopus
{   
    MultiCoreSystem::MultiCoreSystem(vector<string> cl_params, string config_name, string config_path) :
        Configurable(cl_params, config_path, config_name)
    {
        string name = STRINGIFY(MultiCoreSystem);
        //Parameters initialization
        string workload_path = std::get<string>(parameters.at(STRINGIFY(workload_path)).value);
        int num_cores = std::get<int>(parameters.at(STRINGIFY(num_cores)).value);
        vector<string> bus_type = std::get<vector<string>>(parameters.at(STRINGIFY(bus_type)).value);
        string cache_controller_type = std::get<string>(parameters.at(STRINGIFY(cache_controller_type)).value);
        // LLC may use a different controller than the L1s (e.g. CacheController_End2End,
        // which drives the TripleBus service channel for back-invalidation on eviction).
        // Falls back to cache_controller_type when not specified.
        string llc_controller_type = cache_controller_type;
        if (parameters.count(STRINGIFY(llc_controller_type)))
            llc_controller_type = std::get<string>(parameters.at(STRINGIFY(llc_controller_type)).value);
        // Core model: the trace-driven CPU by default, or ExternalCPU when an
        // embedder (the gem5 bridge) injects the requests itself.
        string cpu_type = STRINGIFY(CPU);
        if (parameters.count(STRINGIFY(cpu_type)))
            cpu_type = std::get<string>(parameters.at(STRINGIFY(cpu_type)).value);

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

        // Design B: bus[1] is the LLC<->DRAM bus; its crossings log as MEM_BUS.
        if (bus_type.size() > 1)
            bus[1]->setMemBus();

        std::vector<BaseController *> cores;   // for the LLC bank check below
        // iterate over each core
        for (int i = 0; i < num_cores; i++)
        {
            // Workload per core: a periodic task program (docs/Tasks.md) if present, else the
            // access trace, else nothing (an idle core).
            string file_path = workload_path + "/task_C" + std::to_string(i) + ".task.csv";
            if (!std::ifstream(file_path).good())
            {
                file_path = workload_path + "/trace_C" + std::to_string(i) + ".trc.shared";
                if (!std::ifstream(file_path).good())
                    file_path = "";
            }
            BaseController *cache_controller;
            int cache_id = std::get<int>(getSubMap(STRINGIFY(cache_controller), i).at("m_id").value);

            DirectInterconnect *cpu_interconnect = new DirectInterconnect(getSubMap(STRINGIFY(cpu_interconnect), i), cache_id, -1, name);

            ExternalCPU *external_cpu = NULL;
            if (cpu_type == STRINGIFY(ExternalCPU))
            {
                external_cpu = new ExternalCPU(getSubMap(STRINGIFY(cpu), i), cache_id,
                                               cpu_interconnect->getInterfaceFor(-1), name);
                ExternalCPU::getExtCPUs()->emplace(cache_id, external_cpu);
            }
            else
                new CPU(getSubMap(STRINGIFY(cpu), i), cache_id, cpu_interconnect->getInterfaceFor(-1), file_path, name);

            cache_controller = createController(cache_controller_type, 
                                                getSubMap(STRINGIFY(cache_controller), i),
                                                bus[0]->getInterfaceFor(cache_id),
                                                cpu_interconnect->getInterfaceFor(cache_id), name);
            cores.push_back(cache_controller);
            if (external_cpu != NULL)
            {
                cache_controller->setCpuPort(external_cpu);
                external_cpu->attachCache(cache_controller);
            }
        }

        // LLC banks (docs/LLC_Banking_and_Partitioning_Design.md s4.1): llc_banks(i) complete LLC
        // controllers, llc_controller[k] when banked, the plain llc_controller map when there is one
        // (every existing preset). Each bank is its own endpoint on bus[0] and bus[1]; the cores'
        // home_ids map lines onto them.
        int llc_banks = parameters.count(STRINGIFY(llc_banks)) ? std::get<int>(parameters.at(STRINGIFY(llc_banks)).value) : 1;
        BaseController *llc_controller = NULL;
        int llc_id = -1;
        std::vector<BaseController *> banks; std::vector<int> bank_ids;
        for (int k = 0; k < llc_banks; k++)
        {
            ParametersMap lmap = llc_banks == 1 ? getSubMap(STRINGIFY(llc_controller)) : getSubMap(STRINGIFY(llc_controller), k);
            int bank_id = std::get<int>(lmap.at("m_id").value);
            if (bus[0]->getInterfaceFor(bank_id) == NULL || bus[1]->getInterfaceFor(bank_id) == NULL)
            {
                cerr << "MultiCoreSystem: LLC bank " << bank_id << " is not an endpoint of bus[0] (upper_level_cache_ids) and bus[1] (m_lower_level_ids)" << endl;
                exit(2);
            }
            BaseController *bank = createController(llc_controller_type, lmap,
                                                    bus[1]->getInterfaceFor(bank_id),
                                                    bus[0]->getInterfaceFor(bank_id), name);
            bank->setLogRole(Logger::Role::LLC);   // Design B: tag LLC events
            if (k == 0) { llc_controller = bank; llc_id = bank_id; }   // MCsim's default reply target and clock
            banks.push_back(bank); bank_ids.push_back(bank_id);
        }
        if (llc_banks > 1)
        {
            // The banks interleave lines exactly as the cores' home map does (s4.2, s5.2): every core
            // must name the same banks with the same granularity, and name only banks.
            Interleave home = cores.empty() ? Interleave() : cores[0]->homeMap();
            std::vector<int> want = bank_ids, got = home.ids;
            std::sort(want.begin(), want.end()); std::sort(got.begin(), got.end());
            bool ok = (got == want);
            for (BaseController *c : cores)
                ok = ok && c->homeMap().ids == home.ids && c->homeMap().shift == home.shift;
            if (!ok)
            {
                cerr << "MultiCoreSystem: llc_banks(i) = " << llc_banks << " needs every core's home_ids(vi) to list exactly the"
                     << " LLC bank ids, with one home_interleave_shift(i) for all cores" << endl;
                exit(2);
            }
            for (BaseController *b : banks) b->setBanks(home);
            bus[0]->setLLCBanks(bank_ids);   // B6: broadcasts skip the banks they do not name
        }

        // Main memory: the basic MainMemoryController by default, or the MCsim DRAM
        // simulator when `main_memory_type=MCsim` (configured for DDR4 in MCsimInterface).
        int main_memory_id = std::get<int>(getSubMap(STRINGIFY(m_main_memory)).at("m_id").value);
        string main_memory_type = STRINGIFY(MainMemoryController);
        if (parameters.count(STRINGIFY(main_memory_type)))
            main_memory_type = std::get<string>(parameters.at(STRINGIFY(main_memory_type)).value);

        if (main_memory_type == "MCsim")
        {
            string mem_system = "FRFCFS";
            if (parameters.count(STRINGIFY(mcsim_scheduler)))
                mem_system = std::get<string>(parameters.at(STRINGIFY(mcsim_scheduler)).value);
            new MCsimInterface(bus[1]->getInterfaceFor(main_memory_id), main_memory_id, llc_id,
                               num_cores, /*block_size=*/64, llc_controller->getClkPeriod(), mem_system);
        }
        else
        {
            new MainMemoryController(getSubMap(STRINGIFY(MainMemoryController)), bus[1]->getInterfaceFor(main_memory_id), name);
        }

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