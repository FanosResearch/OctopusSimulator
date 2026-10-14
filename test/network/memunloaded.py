#!/usr/bin/env python3
"""MEM-UNLOADED (docs/NoC_Test_Plan.md s12a): the fastest memory read and fill per LLC bank in a run's raw
trace (OCTOPUS_TRACE), from injection (MEM_BUS ENTER) to ejection (MEM_BUS EXIT) of the same packet.
   memunloaded.py <run dir> <bank id> <bank id> ...   (banks in home-map order, home_interleave_shift 0)
Prints one line per (kind, bank): "<kind> <bank> <min latency> <packets>"."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools', 'octoviz'))
import tracefile as tf
import numpy as np

def main():
    run, banks = sys.argv[1], [int(b) for b in sys.argv[2:]]
    a = tf.read_all(os.path.join(run, 'trace.bin'))
    R = {n: i for i, n in enumerate(tf.RESOURCE)}
    K = {n: i for i, n in enumerate(tf.KIND)}
    a = a[(a['resource'] == R['MEM_BUS']) & np.isin(a['kind'], [K['MEM_READ'], K['FILL']])]
    open_ = {}; best = {}
    for r in a:
        key = (int(r['msg_id']), int(r['kind']), int(r['flags']))     # flags = destination endpoint
        if int(r['phase']) == 0:
            open_[key] = int(r['cycle'])
        elif int(r['phase']) == 2 and key in open_:
            lat = int(r['cycle']) - open_.pop(key)
            bank = banks[(int(r['addr']) >> 6) % len(banks)]
            k = (tf.KIND[int(r['kind'])], bank)
            m, n = best.get(k, (None, 0))
            best[k] = (lat if m is None else min(m, lat), n + 1)
    for (kind, bank), (m, n) in sorted(best.items()):
        print(kind, bank, m, n)

if __name__ == '__main__':
    main()
