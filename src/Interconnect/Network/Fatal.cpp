/*
 * File  :      Fatal.cpp
 * The network's single fatal path (docs/NoC_Architecture_Spec.md §16): one message, the hang
 * dump if enabled, exit status 2. Never status 0.
 */
#include "../../../header/Interconnect/Network/NetTypes.h"
#include "../../../header/HangDump.h"
#include <cstdlib>
#include <iostream>

namespace octopus { namespace net
{
    void fatal(const std::string &msg)
    {
        std::cout.flush();
        std::cerr << "Network: " << msg << std::endl;
        if (std::getenv("OCTOPUS_HANG_DUMP")) HangDump::run();
        std::cerr.flush();
        std::exit(2);
    }
}}
