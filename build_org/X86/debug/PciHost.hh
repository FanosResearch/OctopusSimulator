/**
 * DO NOT EDIT THIS FILE!
 * File automatically generated by
 *   build_tools/debugflaghh.py:140
 */

#ifndef __DEBUG_PciHost_HH__
#define __DEBUG_PciHost_HH__

#include "base/compiler.hh" // For namespace deprecation
#include "base/debug.hh"
namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(Debug, debug);
namespace debug
{

namespace unions
{
inline union PciHost
{
    ~PciHost() {}
    SimpleFlag PciHost = {
        "PciHost", "", false
    };
} PciHost;
} // namespace unions

inline constexpr const auto& PciHost =
    ::gem5::debug::unions::PciHost.PciHost;

} // namespace debug
} // namespace gem5

#endif // __DEBUG_PciHost_HH__
