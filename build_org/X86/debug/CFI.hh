/**
 * DO NOT EDIT THIS FILE!
 * File automatically generated by
 *   build_tools/debugflaghh.py:140
 */

#ifndef __DEBUG_CFI_HH__
#define __DEBUG_CFI_HH__

#include "base/compiler.hh" // For namespace deprecation
#include "base/debug.hh"
namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(Debug, debug);
namespace debug
{

namespace unions
{
inline union CFI
{
    ~CFI() {}
    SimpleFlag CFI = {
        "CFI", "", false
    };
} CFI;
} // namespace unions

inline constexpr const auto& CFI =
    ::gem5::debug::unions::CFI.CFI;

} // namespace debug
} // namespace gem5

#endif // __DEBUG_CFI_HH__
