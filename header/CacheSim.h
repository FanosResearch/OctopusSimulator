/*
 * File  :      CacheSim.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On October 4, 2022
 */

#ifndef _CacheSim_H
#define _CacheSim_H

// Kept deliberately light: this header is included by the gem5 bridge. The
// system-configuration headers it used to pull in are included by
// CacheSim.cpp instead, so an embedder needs only this directory on its
// include path.
#include <cstdint>
#include <string>
#include <vector>

namespace octopus
{
    class Configurable;

    class CacheSim
    {
    private:
        Configurable* system_config;

    public:
        // system_name selects the system class (MultiCoreSystem, ...);
        // config_name selects its CSV under configuration/SystemConfigurations,
        // defaulting to the class name. cl_params are name=value overrides.
        CacheSim(std::string system_name, std::vector<std::string> cl_params,
                 bool print_config = false, std::string config_name = "");
        ~CacheSim();

        void run();
        void step();

        /* Simulated time in ns. One step() advances to the next scheduled
         * event, so an embedder pacing this model from its own clock must
         * step until now() has moved far enough rather than counting steps. */
        uint64_t now() const;

        /* Granularity one step() advances by, and the smallest registered
         * clock period. Equal in a well-formed configuration. */
        uint64_t stepGranularity() const;
        uint64_t minPeriod() const;
    };
}

#endif /* _CacheSim_H */
