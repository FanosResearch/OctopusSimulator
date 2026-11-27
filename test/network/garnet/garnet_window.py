# gem5 configuration for docs/NoC_Garnet_Validation.md: configs/example/garnet_synth_traffic.py
# with a warm-up window and a measurement window, and the tester's skip_self switch
# (test/network/garnet/gem5-validation.patch).
#
#   gem5.opt [-d outdir] garnet_window.py <garnet_synth_traffic.py options>
#            --warmup-cycles W --measure-cycles M [--skip-self]
#
# The tester runs for W + M cycles. The statistics are reset after W cycles and dumped after
# W + M, so stats.txt (first dump) covers the measurement window only: packets received in it,
# their latencies, and each source's MessageBuffer (its queue in front of the network
# interface). The gem5 tree is found from the GEM5 environment variable or from the
# gem5 binary's location (<gem5>/build/NULL/gem5.opt).
import argparse
import os
import sys

import m5
from m5.objects import *
from m5.util import addToPath

gem5_root = os.environ.get("GEM5")
if not gem5_root:
    gem5_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(sys.executable))))
addToPath(os.path.join(gem5_root, "configs"))

from common import Options
from ruby import Ruby

parser = argparse.ArgumentParser()
Options.addNoISAOptions(parser)
parser.add_argument(
    "--synthetic",
    default="uniform_random",
    choices=["uniform_random", "tornado", "bit_complement", "bit_reverse", "bit_rotation",
             "neighbor", "shuffle", "transpose"],
)
parser.add_argument("-i", "--injectionrate", type=float, default=0.1)
parser.add_argument("--precision", type=int, default=3)
parser.add_argument("--num-packets-max", type=int, default=-1)
parser.add_argument("--single-sender-id", type=int, default=-1)
parser.add_argument("--single-dest-id", type=int, default=-1)
parser.add_argument("--inj-vnet", type=int, default=-1, choices=[-1, 0, 1, 2])
parser.add_argument("--warmup-cycles", type=int, default=10000)
parser.add_argument("--measure-cycles", type=int, default=20000)
parser.add_argument("--skip-self", action="store_true")
parser.add_argument("--buffers-per-data-vc", type=int, default=4)
parser.add_argument("--ordered-vnet", action="store_true",
                    help="declare vnet 2 ordered (stock Garnet: the NI then sends the oldest packet first and SA "
                         "prefers the oldest VC of an input port towards the same output; no flit interleaving)")
Ruby.define_options(parser)
args = parser.parse_args()

period = 1000   # ticks per cycle: 1 ps ticks, --sys-clock and --ruby-clock 1GHz (checked below)
total = args.warmup_cycles + args.measure_cycles
cpus = [
    GarnetSyntheticTraffic(
        num_packets_max=args.num_packets_max,
        single_sender=args.single_sender_id,
        single_dest=args.single_dest_id,
        # the tester compares sim_cycles with curTick() (ticks); stop it well after the window
        sim_cycles=min(2**31 - 1, (total + 10) * period),
        traffic_type=args.synthetic,
        inj_rate=args.injectionrate,
        inj_vnet=args.inj_vnet,
        precision=args.precision,
        num_dest=args.num_dirs,
        skip_self=args.skip_self,
    )
    for i in range(args.num_cpus)
]
system = System(cpu=cpus, mem_ranges=[AddrRange(args.mem_size)])
system.voltage_domain = VoltageDomain(voltage=args.sys_voltage)
system.clk_domain = SrcClockDomain(clock=args.sys_clock, voltage_domain=system.voltage_domain)
Ruby.create_system(args, False, system)
system.ruby.network.buffers_per_data_vc = args.buffers_per_data_vc
if args.ordered_vnet:
    for i in range(args.num_cpus):
        getattr(system.ruby, f"l1_cntrl{i}").responseFromCache.ordered = True
system.ruby.clk_domain = SrcClockDomain(clock=args.ruby_clock, voltage_domain=system.voltage_domain)
for i, ruby_port in enumerate(system.ruby._cpu_ports):
    cpus[i].test = ruby_port.in_ports
if args.sys_clock != "1GHz" or args.ruby_clock != "1GHz":
    sys.exit("garnet_window.py: pass --sys-clock 1GHz --ruby-clock 1GHz (one tester cycle = one network cycle)")
root = Root(full_system=False, system=system)
root.system.mem_mode = "timing"
m5.ticks.setGlobalFrequency("1ps")
m5.instantiate()
if args.warmup_cycles:
    m5.simulate(args.warmup_cycles * period)
    m5.stats.reset()
ev = m5.simulate(args.measure_cycles * period)
m5.stats.dump()
print("WINDOW", args.warmup_cycles, args.measure_cycles, "ended @ tick", m5.curTick(), ev.getCause())
