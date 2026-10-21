/*
 * File  :      Params.h
 * Per-link parameters (Table 4) and the conformance level of a link. Plan §1.1, §3.4.
 */
#ifndef _TL_PARAMS_H
#define _TL_PARAMS_H
#include "Types.h"
#include <string>

namespace octopus { namespace tl
{
    // Table 4: w data bytes (1..4096, power of two); a address bits (0..128); z size-field bits
    // (1..4); o source bits (0..64); i sink bits (0..64). The level decides which channels the
    // link has (plan §1.1: A+D for TL-UL/UH, A..E for TL-C).
    struct LinkParams
    {
        unsigned w = 8;
        unsigned a = 32;
        unsigned z = 3;
        unsigned o = 4;
        unsigned i = 0;
        Level    level = Level::UL;

        // The first violated range as an exact message, or "" when every field is in range:
        //   "w must be a power of two between 1 and 4096, got N"
        //   "a must be between 0 and 128, got N"
        //   "z must be between 1 and 4, got N"
        //   "o must be between 0 and 64, got N"
        //   "i must be between 0 and 64, got N"
        std::string validate() const;

        // Table 6: size is a z-bit field, so the largest encodable size is 2^z - 1.
        unsigned maxSize() const { return (1u << z) - 1; }
        // §4: beats of a message of that size on this bus (plan §1.4).
        unsigned beatsOf(bool has_data, unsigned size) const { return beatCount(has_data, size, w); }
        // Plan §1.1: the channels the link has at its level.
        bool hasChannel(Channel ch) const { return channelExists(level, ch); }
        // Address space and ID pools, for the elaboration checks and the ID pools of later steps.
        uint64_t numSources() const { return o >= 64 ? ~uint64_t(0) : (uint64_t(1) << o); }
        uint64_t numSinks() const { return i >= 64 ? ~uint64_t(0) : (uint64_t(1) << i); }
        bool addressFits(uint64_t address) const { return a >= 64 || address < (uint64_t(1) << a); }

        std::string describe() const;   // "TL-UH w=8 a=32 z=3 o=4 i=0"
    };
}}
#endif /* _TL_PARAMS_H */
