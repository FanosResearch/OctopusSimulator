/*
 * File  :      ErrorSlave.cpp
 */
#include "tilelink/agents/ErrorSlave.h"

namespace octopus { namespace tl
{
    // §4.4: "denied ... must have no effect"; RamSlave::execute applies it to every request.
    ErrorSlave::ErrorSlave(const std::string &name, const Config &params) : RamSlave(name, params) { deny_all = true; }
}}
