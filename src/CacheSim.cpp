/*
 * File  :      CacheSim.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 4, 2022
 */

#include "../header/CacheSim.h"
#include "../header/Configurable.h"
#include "../header/SystemConfigurations/MultiCoreSystem.h"
#include "../header/SystemConfigurations/MultiCoreSystem_Mesh.h"
#include "../header/ClockManager.h"

using namespace std;

namespace octopus
{
    CacheSim::CacheSim(string system_name, vector<string> cl_params, bool print_config,
                       string config_name)
    {
        Configurable::print_config_global = print_config;

        // setup simulation environment
        if(system_name == STRINGIFY(MultiCoreSystem))
            system_config = new MultiCoreSystem(cl_params,
                                                config_name.empty() ? string(STRINGIFY(MultiCoreSystem)) : config_name);
        else if(system_name == STRINGIFY(MultiCoreSystem_Mesh))
            system_config = new MultiCoreSystem_Mesh(cl_params);
        else
        {
            cout << "Error wrong system configuration." << endl;
            exit(0);
        }

        ClockManager::getClockManager()->init();
    }

    CacheSim::~CacheSim()
    {
        delete system_config;
    }

    void CacheSim::run()
    {
        ClockManager::getClockManager()->run();
    }

    void CacheSim::step()
    {
        ClockManager::getClockManager()->clkStep();
    }

    uint64_t CacheSim::now() const
    {
        return ClockManager::getClockManager()->getCurrentTime();
    }

    uint64_t CacheSim::stepGranularity() const
    {
        return ClockManager::getClockManager()->getStepGranularity();
    }

    uint64_t CacheSim::minPeriod() const
    {
        return ClockManager::getClockManager()->getMinPeriod();
    }
}
