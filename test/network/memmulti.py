#!/usr/bin/env python3
"""MEM-MULTI (docs/NoC_Test_Plan.md s12a): every memory read and write in a run's raw trace went to the memory
controller the map names, mem_ids[((addr / 64) >> shift) mod M] (spec s25.4), and each controller's receive
count in NetworkStats.csv equals the trace's.
   memmulti.py <run dir> <shift> <memory id> <memory id> ...
Prints "<packets checked> <misrouted> <count mismatches>" and one "<id> <packets>" line per controller."""
import sys, os, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools', 'octoviz'))
import tracefile as tf
import numpy as np

def main():
    run, shift, mem = sys.argv[1], int(sys.argv[2]), [int(x) for x in sys.argv[3:]]
    a = tf.read_all(os.path.join(run, 'trace.bin'))
    R = {n: i for i, n in enumerate(tf.RESOURCE)}
    K = {n: i for i, n in enumerate(tf.KIND)}
    a = a[(a['resource'] == R['MEM_BUS']) & (a['phase'] == 0) & np.isin(a['kind'], [K['MEM_READ'], K['MEM_WRITE']])]
    per = collections.Counter(); bad = 0
    for r in a:
        dst = int(r['flags']); per[dst] += 1
        if dst != mem[((int(r['addr']) >> 6) >> shift) % len(mem)]:
            bad += 1
    stats = {}
    with open(os.path.join(run, 'NetworkStats.csv')) as fh:
        for line in fh:
            f = line.strip().split(',')
            if f[0] == 'received' and int(f[1]) in mem:
                stats[int(f[1])] = int(f[4])
    mism = sum(1 for m in mem if stats.get(m, 0) != per.get(m, 0))
    print(len(a), bad, mism)
    for m in mem:
        print(m, per.get(m, 0))

if __name__ == '__main__':
    main()
