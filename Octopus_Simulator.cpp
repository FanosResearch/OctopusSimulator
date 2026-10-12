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
    for (int i = 1; i < argc; ++i)
    {
        if ((string(argv[i]) == "-o" || string(argv[i]) == "-c" || string(argv[i]) == "--config") &&
            (i + 1 == argc || string(argv[i + 1]).empty() || argv[i + 1][0] == '-'))
        {
            cerr << "Missing value for " << argv[i] << endl;
            return 1;
        }
    }
    // command line parser
    CLParser cl_parser(argc, argv);
    vector<string> cl_params = cl_parser.getParam("-p");
    vector<string> sys_names = cl_parser.getParam("-s");
    vector<string> print_config = cl_parser.getParam("--PrintConfig");
    vector<string> output_dirs = cl_parser.getParam("-o");
    vector<string> configs = cl_parser.getParam("--config");
    bool trace = !cl_parser.getParam("--trace").empty();

    if (sys_names.empty())
    {
        cerr << "Usage: " << argv[0] << " -s <system> [-c <system-csv>] [-o <logger-output-directory>] [--trace] [-p <override>]" << endl;
        return 1;
    }

    CacheSim cache_sim(sys_names.back(), cl_params, !print_config.empty(),
                       configs.empty() ? "" : configs.back(),
                       output_dirs.empty() ? "" : output_dirs.back(), trace);
    cache_sim.run();
    
    return 0;
}
