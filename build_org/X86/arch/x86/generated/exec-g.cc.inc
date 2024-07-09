// DO NOT EDIT
// This file was automatically generated from an ISA description:
//   x86/isa/main.isa


#include <cmath>
#include <limits>
#include <tuple>

#include "arch/generic/debugfaults.hh"
#include "arch/x86/cpuid.hh"
#include "arch/x86/faults.hh"
#include "arch/x86/memhelpers.hh"
#include "arch/x86/pseudo_inst_abi.hh"
#include "arch/x86/regs/misc.hh"
#include "arch/x86/tlb.hh"
#include "base/compiler.hh"
#include "base/condcodes.hh"
#include "base/logging.hh"
#include "cpu/base.hh"
#include "cpu/exetrace.hh"
#include "debug/X86.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "mem/request.hh"
#include "sim/pseudo_inst.hh"
#include "sim/sim_exit.hh"

namespace gem5::X86ISAInst
{
using namespace X86ISA;
} // namespace gem5::X86ISAInst
