/**
 * DO NOT EDIT THIS FILE!
 * File automatically generated by
 *   build_tools/sim_object_param_struct_cc.py:297
 */

#include "pybind11/pybind11.h"
#include "pybind11/stl.h"

#include <type_traits>

#include "base/compiler.hh"
#include "params/BaseRoutingUnit.hh"
#include "sim/init.hh"
#include "sim/sim_object.hh"

#include "mem/ruby/network/simple/routing/BaseRoutingUnit.hh"

namespace py = pybind11;

namespace gem5
{

static void
module_init(py::module_ &m_internal)
{
py::module_ m = m_internal.def_submodule("param_BaseRoutingUnit");
    py::class_<BaseRoutingUnitParams, SimObjectParams, std::unique_ptr<BaseRoutingUnitParams, py::nodelete>>(m, "BaseRoutingUnitParams")
        ;

    py::class_<gem5::ruby::BaseRoutingUnit, gem5::SimObject, std::unique_ptr<gem5::ruby::BaseRoutingUnit, py::nodelete>>(m, "gem5_COLONS_ruby_COLONS_BaseRoutingUnit")
        ;

}

static EmbeddedPyBind embed_obj("BaseRoutingUnit", module_init, "SimObject");

} // namespace gem5
