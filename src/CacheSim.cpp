/*
 * File  :      CacheSim.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 4, 2022
 */

#include "../header/CacheSim.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include "../header/Configurable.h"
#include "../header/SystemConfigurations/MultiCoreSystem.h"
#include "../header/SystemConfigurations/MultiCoreSystem_Mesh.h"
#include "../header/ClockManager.h"
#include "../header/CacheControllers/BaseController.h"

using namespace std;

namespace octopus
{
    CacheSim::CacheSim(string system_name, vector<string> cl_params, bool print_config, string config)
        : CacheSim(system_name, cl_params, print_config, config, "", false)
    {
    }

    CacheSim::CacheSim(string system_name, vector<string> cl_params, bool print_config, string config, string output_dir, bool trace)
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
        
        // Route only configuration printing into the run directory. Restore stdout
        // and close the file even if construction throws.
        unique_ptr<FILE, void (*)(FILE*)> config_log(nullptr, [](FILE* file) {
            Configurable::print_config_output = stdout;
            fclose(file);
        });
        if (print_config && !output_dir.empty())
        {
            const auto path = std::filesystem::path(output_dir) / "config.log";
            config_log.reset(fopen(path.string().c_str(), "w"));
            if (!config_log)
            {
                cerr << "Cannot write configuration log: " << path << endl;
                exit(1);
            }
            Configurable::print_config_output = config_log.get();
        }

        // Bare names select built-in presets; paths with a directory component
        // are relative to the caller (or absolute). Only system defaults change.
        std::filesystem::path config_file;
        if (config.empty()) config = system_name;
        config_file = std::filesystem::path(config);
        if (!config_file.has_parent_path())
            config_file = std::filesystem::path(CONFIGURATION_PATH) / SYSTEM_CONFIGURATIONS / config_file;
        if (config_file.extension() != ".csv") config_file += ".csv";
        config_file = std::filesystem::absolute(config_file).lexically_normal();
        if (!std::filesystem::is_regular_file(config_file) || !std::ifstream(config_file).good())
        {
            cerr << "Cannot read system configuration: " << config_file << endl;
            exit(1);
        }
        const string config_dir = config_file.parent_path().string();
        const string config_name = config_file.stem().string();

        // setup simulation environment
        if(system_name == STRINGIFY(MultiCoreSystem))
            system_config = new MultiCoreSystem(cl_params, config_name, config_dir);
        else if(system_name == STRINGIFY(MultiCoreSystem_Mesh))
            system_config = new MultiCoreSystem_Mesh(cl_params, config_dir, config_name);
        else
        {
            cout << "Error wrong system configuration." << endl;
            exit(0);
        }

        config_log.reset();

        // System constructors supply the default; override before any clocked
        // component initializes or writes reports. Applies to both topologies.
        if (!output_dir.empty())
            Logger::getLogger()->registerReportPath(output_dir);
        if (trace)
            Logger::getLogger()->enableTrace();

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

    void CacheSim::reportOccupancy(std::ostream &os) const
    {
        BaseController::reportOccupancy(os);
    }
}
