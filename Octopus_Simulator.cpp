/*
 * File  :      Octopus_Simulator.cpp
 * Author:      Mohammed Ismail
 * Email :      mohamed.hossam@mcmaster.ca
 *
 * Created On Dec 13, 2023
 */

#include "CacheSim.h"
#include "CLParser.h"

using namespace octopus;
using namespace std;

int main (int argc, char *argv[])
{
    // command line parser
    CLParser cl_parser(argc, argv);
    vector<string> cl_params = cl_parser.getParam("-p");
    vector<string> sys_names = cl_parser.getParam("-s");
    vector<string> print_config = cl_parser.getParam("--PrintConfig");

    CacheSim cache_sim(sys_names[sys_names.size() - 1], cl_params, !print_config.empty());
    cache_sim.run();
    
    return 0;
}
