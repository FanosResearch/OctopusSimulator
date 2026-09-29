/*
 * File  :      CacheSim.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 4, 2022
 */

#include "../header/CacheSim.h"
#include <filesystem>

using namespace std;

namespace octopus
{
    CacheSim::CacheSim(string system_name, vector<string> cl_params, bool print_config, string output_dir)
    {
        Configurable::print_config_global = print_config;
        if (!output_dir.empty())
        {
            std::error_code error;
            std::filesystem::create_directories(output_dir, error);
            if (error)
            {
                cerr << "Cannot create logger output directory '" << output_dir
                     << "': " << error.message() << endl;
                exit(1);
            }
        }
        
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

        // System constructors supply the default; override before any clocked
        // component initializes or writes reports. Applies to both topologies.
        if (!output_dir.empty())
            Logger::getLogger()->registerReportPath(output_dir);

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
