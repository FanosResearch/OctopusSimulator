#!/usr/bin/env python3
"""P2-BANKING: every request a cache sends to a home went to the bank the home map names, from every
core. Reads the raw trace's REQ_BUS ENTER records (flags = destination endpoint).
   homecheck.py <run dir> <home ids, comma separated> [shift, default 0] [block, default 64]
Prints "<checked> <mismatches> <per-bank counts>" and exits 1 on a mismatch."""
import sys, os, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools', 'octoviz'))
import tracefile as tf

def main():
    run = sys.argv[1]; homes = [int(x) for x in sys.argv[2].split(',')]
    shift = int(sys.argv[3]) if len(sys.argv) > 3 else 0; block = int(sys.argv[4]) if len(sys.argv) > 4 else 64
    a = tf.read_all(os.path.join(run, 'trace.bin'))
    R = {n: i for i, n in enumerate(tf.RESOURCE)}; K = {n: i for i, n in enumerate(tf.KIND)}
    to_home = {K['GETS'], K['GETM'], K['PUTS'], K['PUTM']}
    sel = a[(a['resource'] == R['REQ_BUS']) & (a['phase'] == 0)]
    checked = bad = 0; per = collections.Counter()
    for r in sel:
        if int(r['kind']) not in to_home: continue
        dst = int(r['flags']); want = homes[((int(r['addr']) // block) >> shift) % len(homes)]
        checked += 1; per[dst] += 1
        if dst != want: bad += 1
    print(checked, bad, dict(per))
    sys.exit(1 if bad else 0)

main()
