/**
 * DO NOT EDIT THIS FILE!
 * File automatically generated by
 *   build_tools/sim_object_param_struct_cc.py:297
 */

#include "pybind11/pybind11.h"
#include "pybind11/stl.h"

#include <type_traits>

#include "base/compiler.hh"
#include "params/NoncoherentXBar.hh"
#include "sim/init.hh"
#include "sim/sim_object.hh"

#include "mem/noncoherent_xbar.hh"

namespace py = pybind11;

namespace gem5
{

static void
module_init(py::module_ &m_internal)
{
py::module_ m = m_internal.def_submodule("param_NoncoherentXBar");
    py::class_<NoncoherentXBarParams, BaseXBarParams, std::unique_ptr<NoncoherentXBarParams, py::nodelete>>(m, "NoncoherentXBarParams")
        .def(py::init<>())
        .def("create", &NoncoherentXBarParams::create)
        ;

    py::class_<gem5::NoncoherentXBar, gem5::BaseXBar, std::unique_ptr<gem5::NoncoherentXBar, py::nodelete>>(m, "gem5_COLONS_NoncoherentXBar")
        ;

}

static EmbeddedPyBind embed_obj("NoncoherentXBar", module_init, "BaseXBar");

} // namespace gem5
namespace gem5
{

namespace
{

/*
 * If we can't define a default create() method for this params
 * struct because the SimObject doesn't have the right
 * constructor, use template magic to make it so we're actually
 * defining a create method for this class instead.
 */
class DummyNoncoherentXBarParamsClass
{
  public:
    gem5::NoncoherentXBar *create() const;
};

template <class CxxClass, class Enable=void>
class DummyNoncoherentXBarShunt;

/*
 * This version directs to the real Params struct and the
 * default behavior of create if there's an appropriate
 * constructor.
 */
template <class CxxClass>
class DummyNoncoherentXBarShunt<CxxClass, std::enable_if_t<
    std::is_constructible_v<CxxClass, const NoncoherentXBarParams &>>>
{
  public:
    using Params = NoncoherentXBarParams;
    static gem5::NoncoherentXBar *
    create(const Params &p)
    {
        return new CxxClass(p);
    }
};

/*
 * This version diverts to the DummyParamsClass and a dummy
 * implementation of create if the appropriate constructor does
 * not exist.
 */
template <class CxxClass>
class DummyNoncoherentXBarShunt<CxxClass, std::enable_if_t<
    !std::is_constructible_v<CxxClass, const NoncoherentXBarParams &>>>
{
  public:
    using Params = DummyNoncoherentXBarParamsClass;
    static gem5::NoncoherentXBar *
    create(const Params &p)
    {
        return nullptr;
    }
};

} // anonymous namespace

/*
 * An implementation of either the real Params struct's create
 * method, or the Dummy one. Either an implementation is
 * mandantory since this was shunted off to the dummy class, or
 * one is optional which will override this weak version.
 */
[[maybe_unused]] gem5::NoncoherentXBar *
DummyNoncoherentXBarShunt<gem5::NoncoherentXBar>::Params::create() const
{
    return DummyNoncoherentXBarShunt<gem5::NoncoherentXBar>::create(*this);
}

} // namespace gem5
