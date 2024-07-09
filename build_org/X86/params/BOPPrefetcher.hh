/**
 * DO NOT EDIT THIS FILE!
 * File automatically generated by
 *   build_tools/sim_object_param_struct_hh.py:235
 */

#ifndef __PARAMS__BOPPrefetcher__
#define __PARAMS__BOPPrefetcher__

namespace gem5 {
namespace prefetch {
class BOP;
} // namespace prefetch
} // namespace gem5
#include <cstddef>
#include "base/types.hh"
#include <cstddef>
#include "base/types.hh"
#include <cstddef>
#include <cstddef>
#include "base/types.hh"
#include <cstddef>
#include <cstddef>
#include "base/types.hh"
#include <cstddef>
#include "base/types.hh"
#include <cstddef>
#include "base/types.hh"
#include <cstddef>
#include "base/types.hh"
#include <cstddef>
#include "base/types.hh"

#include "params/QueuedPrefetcher.hh"

namespace gem5
{
struct BOPPrefetcherParams
    : public QueuedPrefetcherParams
{
    gem5::prefetch::BOP * create() const;
    unsigned bad_score;
    Cycles delay_queue_cycles;
    bool delay_queue_enable;
    unsigned delay_queue_size;
    bool negative_offsets_enable;
    unsigned offset_list_size;
    unsigned round_max;
    unsigned rr_size;
    unsigned score_max;
    unsigned tag_bits;
};

} // namespace gem5

#endif // __PARAMS__BOPPrefetcher__
