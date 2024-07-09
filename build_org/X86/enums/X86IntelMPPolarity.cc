/**
 * DO NOT EDIT THIS FILE!
 * File automatically generated by
 *   build_tools/enum_cc.py:170
 */

#include "base/compiler.hh"
#include "enums/X86IntelMPPolarity.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(Enums, enums);
namespace enums
{
    const char *X86IntelMPPolarityStrings[Num_X86IntelMPPolarity] =
    {
        "ActiveHigh",
        "ActiveLow",
        "ConformPolarity",
    };
} // namespace enums
} // namespace gem5
#include "pybind11/pybind11.h"
#include "pybind11/stl.h"

#include <sim/init.hh>

namespace py = pybind11;

namespace gem5
{

static void
module_init(py::module_ &m_internal)
{
    py::module_ m = m_internal.def_submodule("enum_X86IntelMPPolarity");

py::enum_<enums::X86IntelMPPolarity>(m, "enum_X86IntelMPPolarity")
        .value("ActiveHigh", enums::ActiveHigh)
        .value("ActiveLow", enums::ActiveLow)
        .value("ConformPolarity", enums::ConformPolarity)
        .value("Num_X86IntelMPPolarity", enums::Num_X86IntelMPPolarity)
        .export_values()
        ;
    }
static EmbeddedPyBind embed_enum("enum_X86IntelMPPolarity", module_init);

} // namespace gem5
    
