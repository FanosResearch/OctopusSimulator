/**
 * DO NOT EDIT THIS FILE!
 * File automatically generated by
 *   build_tools/debugflaghh.py:140
 */

#ifndef __DEBUG_Loader_HH__
#define __DEBUG_Loader_HH__

#include "base/compiler.hh" // For namespace deprecation
#include "base/debug.hh"
namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(Debug, debug);
namespace debug
{

namespace unions
{
inline union Loader
{
    ~Loader() {}
    SimpleFlag Loader = {
        "Loader", "", false
    };
} Loader;
} // namespace unions

inline constexpr const auto& Loader =
    ::gem5::debug::unions::Loader.Loader;

} // namespace debug
} // namespace gem5

#endif // __DEBUG_Loader_HH__
