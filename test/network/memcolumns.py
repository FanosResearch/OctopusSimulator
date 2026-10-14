#!/usr/bin/env python3
"""MEM-COLUMNS (docs/NoC_Test_Plan.md s12a): every LatencyReport row's NoC Mem Hops equals the router hops of
that request's memory packets (MEM_READ and FILL, which carry its id) counted in the raw trace (one NOC
ENTER record per router a packet's head enters).
   memcolumns.py <run dir>      prints "<rows checked> <mismatches>" """
import sys, os, glob, csv, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools', 'octoviz'))
import tracefile as tf
import numpy as np

def main():
    run = sys.argv[1]
    a = tf.read_all(os.path.join(run, 'trace.bin'))
    R = {n: i for i, n in enumerate(tf.RESOURCE)}
    K = {n: i for i, n in enumerate(tf.KIND)}
    a = a[(a['resource'] == R['NOC']) & (a['phase'] == 0) & np.isin(a['kind'], [K['MEM_READ'], K['FILL']])]
    hops = collections.Counter(int(m) for m in a['msg_id'])
    rows = bad = 0
    for f in sorted(glob.glob(os.path.join(run, 'LatencyReport_C*.csv'))):
        with open(f, newline='') as fh:
            r = csv.reader(fh); hdr = next(r)
            if 'NoC Mem Hops' not in hdr:
                sys.exit(f'{f}: no NoC Mem Hops column')
            ci = hdr.index('NoC Mem Hops')
            for row in r:
                if len(row) <= ci or not row[0].isdigit():
                    continue                                    # footer
                rows += 1
                if int(row[ci]) != hops.get(int(row[0]), 0):
                    bad += 1
                    if bad <= 5:
                        print(f'mismatch: request {row[0]} report {row[ci]} trace {hops.get(int(row[0]), 0)}', file=sys.stderr)
    print(rows, bad)

if __name__ == '__main__':
    main()
