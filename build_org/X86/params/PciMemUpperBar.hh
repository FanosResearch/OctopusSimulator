/**
 * DO NOT EDIT THIS FILE!
 * File automatically generated by
 *   build_tools/sim_object_param_struct_hh.py:235
 */

#ifndef __PARAMS__PciMemUpperBar__
#define __PARAMS__PciMemUpperBar__

namespace gem5 {
class PciMemUpperBar;
} // namespace gem5

#include "params/PciBar.hh"

namespace gem5
{
struct PciMemUpperBarParams
    : public PciBarParams
{
    gem5::PciMemUpperBar * create() const;
};

} // namespace gem5

#endif // __PARAMS__PciMemUpperBar__
