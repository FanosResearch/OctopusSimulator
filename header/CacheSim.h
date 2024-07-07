/*
 * File  :      CacheSim.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On October 4, 2022
 */

#ifndef _CacheSim_H
#define _CacheSim_H

#include "Configurable.h"
#include "MultiCoreSystem.h"
#include "MultiCoreSystem_Mesh.h"
#include "ClockManager.h"

#include <string>
#include <vector>

namespace octopus
{
    class CacheSim
    {
    private:
        Configurable* system_config;

    public:
        CacheSim(std::string system_name, std::vector<std::string> cl_params, bool print_config = false);
        ~CacheSim();

        void run();
        void step();
    };
}

#endif /* _CacheSim_H */
