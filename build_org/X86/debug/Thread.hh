/**
 * DO NOT EDIT THIS FILE!
 * File automatically generated by
 *   build_tools/debugflaghh.py:140
 */

#ifndef __DEBUG_Thread_HH__
#define __DEBUG_Thread_HH__

#include "base/compiler.hh" // For namespace deprecation
#include "base/debug.hh"
namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(Debug, debug);
namespace debug
{

namespace unions
{
inline union Thread
{
    ~Thread() {}
    SimpleFlag Thread = {
        "Thread", "", false
    };
} Thread;
} // namespace unions

inline constexpr const auto& Thread =
    ::gem5::debug::unions::Thread.Thread;

} // namespace debug
} // namespace gem5

#endif // __DEBUG_Thread_HH__
