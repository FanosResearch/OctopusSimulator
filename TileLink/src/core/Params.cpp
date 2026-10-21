/*
 * File  :      Params.cpp
 * Table 4 range checks.
 */
#include "tilelink/Params.h"

namespace octopus { namespace tl
{
    std::string LinkParams::validate() const
    {
        // Table 4: w 1..4096, a power of two.
        if (w < 1 || w > 4096 || (w & (w - 1)) != 0)
            return "w must be a power of two between 1 and 4096, got " + std::to_string(w);
        if (a > 128) return "a must be between 0 and 128, got " + std::to_string(a);
        if (z < 1 || z > 4) return "z must be between 1 and 4, got " + std::to_string(z);
        if (o > 64) return "o must be between 0 and 64, got " + std::to_string(o);
        if (i > 64) return "i must be between 0 and 64, got " + std::to_string(i);
        return "";
    }

    std::string LinkParams::describe() const
    {
        return std::string("TL-") + levelName(level) + " w=" + std::to_string(w) + " a=" + std::to_string(a) +
               " z=" + std::to_string(z) + " o=" + std::to_string(o) + " i=" + std::to_string(i);
    }
}}
