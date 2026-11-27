#!/usr/bin/env python3
"""Accepted load and latency past the knee on both simulators (docs/NoC_Garnet_Validation.md s7.2).
  sweep.py <out dir> <pattern> <vcs> <offered loads, comma separated> [--depth D]
Runs from the Octopus-network root; reuses compare_garnet.py's Runner (gem5 tree in $GEM5_GARNET)."""
import os, sys, concurrent.futures as cf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_garnet as C
class A: pass
a = A(); a.k = 8; a.vcs = int(sys.argv[3]); a.depth = int(sys.argv[sys.argv.index('--depth') + 1]) if '--depth' in sys.argv else 4
a.jobs = 6; a.reuse = True; a.octopus = 'build/Octopus_Simulator'; a.garnet_ordered = False
a.gem5 = os.environ.get('GEM5_GARNET', '../gem5-garnet'); a.out = sys.argv[1]
r = C.Runner(a); p = sys.argv[2]
loads = [float(x) for x in sys.argv[4].split(',')]
def one(L):
    g = r.garnet(p, L / 5, 20000, 'sw')
    o = r.octopus(p, int(round(L / 0.005)), 20000, 'sw')
    return (L, g['flits'] / (r.active(p) * 20000), g['hlat'], o['packets'] * 5 / (r.active(p) * 20000), o['lat'] + C.OFFSET_H)
print('pattern,vcs,depth,offered,octopus_accepted,garnet_accepted,octopus_latency_garnet_units,garnet_head_sent_latency')
with cf.ThreadPoolExecutor(6) as ex:
    for L, ga, gl, oa, ol in ex.map(one, loads):
        print(f'{p},{a.vcs},{a.depth},{L:.3f},{oa:.3f},{ga:.3f},{ol:.1f},{gl:.1f}', flush=True)
