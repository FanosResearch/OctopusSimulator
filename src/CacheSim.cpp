/*
 * File  :      CacheSim.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 4, 2022
 */

#include "../header/CacheSim.h"

using namespace std;

namespace octopus
{
    CacheSim::CacheSim(string system_name, vector<string> cl_params, bool print_config)
    {
        Configurable::print_config_global = print_config;
        
        // setup simulation environment
        if(system_name == STRINGIFY(MultiCoreSystem))
            system_config = new MultiCoreSystem(cl_params);
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
}