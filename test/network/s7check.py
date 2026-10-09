#!/usr/bin/env python3
"""Step-7 checks over a run's raw trace and LatencyReports (docs/NoC_Test_Plan.md s10).
   s7check.py <run dir> hops      -> S7-TRACE-HOPS: per (msg_id, kind, router) ENTER = SERVICE = EXIT for every packet
                                     that left the network before the trace ended, and the NOC ENTER count of a
                                     request's packets ejected up to its completion equals the report's NoC Hops
   s7check.py <run dir> sum       -> S7-TRACKER-SUM: NoC Wait + NoC Transfer == sum over the request's packets
                                     ejected up to its completion of (bus EXIT - bus ENTER); packets are told apart
                                     by the destination the network puts in the bus records' flags
   s7check.py <run dir> timebase  -> S7-TIMEBASE: every bus ENTER (injection) has a router ENTER at the same cycle
                                     and every bus EXIT (ejection) a router EXIT at the same cycle
Prints "<checked> <mismatches>" and exits 1 on any mismatch. A request completes, in trace cycles, at
Ready + CPU + Total (the Logger's row is written when its last stage ends)."""
import sys, os, csv, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools', 'octoviz'))
import tracefile as tf
import numpy as np

OFFSET = int(os.environ.get('S7_DONE_OFFSET', '0'))

def reports(run):
    rows = {}
    for f in sorted(os.listdir(run)):
        if not f.startswith('LatencyReport_C'): continue
        with open(os.path.join(run, f), newline='') as fh:
            r = csv.reader(fh); hdr = next(r)
            for row in r:
                if not row or not row[0].isdigit() or len(row) != len(hdr): continue
                d = dict(zip(hdr, row))
                d['_done'] = int(d['Ready Cycle']) + int(d['CPU Latency']) + int(d['Total Latency']) + OFFSET
                rows[int(row[0])] = d
    return rows

def main():
    run, what = sys.argv[1], sys.argv[2]
    a = tf.read_all(os.path.join(run, 'trace.bin'))
    R = {n: i for i, n in enumerate(tf.RESOURCE)}
    noc = a[a['resource'] == R['NOC']]
    bus = a[(a['resource'] == R['REQ_BUS']) | (a['resource'] == R['RESP_BUS'])]
    rep = reports(run)
    end = int(a['cycle'].max()) if len(a) else 0
    checked = bad = 0
    if what == 'hops':
        cnt = collections.Counter(); last = collections.defaultdict(int)
        for r in noc:
            k = (int(r['msg_id']), int(r['kind']), int(r['comp']))
            cnt[k + (int(r['phase']),)] += 1; last[k] = max(last[k], int(r['cycle']))
        for k, lc in last.items():
            if lc > end - 200: continue                   # may still be in flight when the trace ended
            checked += 1
            if not (cnt[k + (0,)] == cnt[k + (1,)] == cnt[k + (2,)]): bad += 1
        # ENTERs of a packet count for the request if the packet was ejected by its completion: find each
        # packet's ejection from the bus EXIT with the same (msg_id, kind, core, dst) and take the ENTERs
        # between that packet's injection and ejection
        inj = collections.defaultdict(list); ej = collections.defaultdict(list)
        for r in bus:
            k = (int(r['msg_id']), int(r['kind']), int(r['core']), int(r['flags']))
            (inj if r['phase'] == 0 else ej)[k].append(int(r['cycle']))
        import bisect
        ent_by = collections.defaultdict(list)                     # (msg_id, kind, core) -> sorted ENTER cycles
        for r in noc[noc['phase'] == 0]: ent_by[(int(r['msg_id']), int(r['kind']), int(r['core']))].append(int(r['cycle']))
        for v in ent_by.values(): v.sort()
        keys_by_id = collections.defaultdict(list)
        for k in inj: keys_by_id[k[0]].append(k)
        skipped = 0
        for mid, row in rep.items():
            # windows per (kind, core): copies of one message (a supply to the requester and to the LLC)
            # share kind and core and may overlap in time; overlapping windows are merged and their
            # ENTERs counted once. A merged group with some copies ejected after the completion and
            # some before cannot be split from the trace alone and is skipped.
            n = 0; skip = False
            groups = collections.defaultdict(list)
            for k in keys_by_id.get(mid, []):
                for i, x in zip(sorted(inj[k]), sorted(ej[k])): groups[(k[1], k[2])].append((i, x, x <= row['_done']))
            for (kind, core), wins in groups.items():
                ents = ent_by.get((mid, kind, core), [])
                clusters = []                                   # [start, end, [counted flags]]
                for (i, x, c) in sorted(wins):
                    if clusters and i <= clusters[-1][1]:
                        clusters[-1][1] = max(clusters[-1][1], x); clusters[-1][2].append(c)
                    else: clusters.append([i, x, [c]])
                for (s0, s1, flags) in clusters:
                    if len(set(flags)) > 1: skip = True
                    elif flags[0]: n += bisect.bisect_right(ents, s1) - bisect.bisect_left(ents, s0)
            if skip: skipped += 1; continue
            checked += 1
            if int(row['NoC Hops']) != n: bad += 1
        print("skipped (copies straddling the completion):", skipped, file=sys.stderr)
    elif what == 'sum':
        inj = collections.defaultdict(list); ej = collections.defaultdict(list)
        for r in bus:
            k = (int(r['msg_id']), int(r['kind']), int(r['core']), int(r['flags']))
            (inj if r['phase'] == 0 else ej)[k].append(int(r['cycle']))
        spans = collections.defaultdict(list)
        for k in inj:
            for i, x in zip(sorted(inj[k]), sorted(ej[k])): spans[k[0]].append((x, x - i))
        for mid, row in rep.items():
            checked += 1
            s = sum(d for (x, d) in spans.get(mid, []) if x <= row['_done'])
            if int(row['NoC Wait']) + int(row['NoC Transfer']) != s: bad += 1
    elif what == 'timebase':
        have = collections.Counter((int(r['msg_id']), int(r['kind']), int(r['core']), int(r['phase']), int(r['cycle'])) for r in noc if r['phase'] != 1)
        for r in bus:
            checked += 1
            k = (int(r['msg_id']), int(r['kind']), int(r['core']), int(r['phase']), int(r['cycle']))
            if have[k] > 0: have[k] -= 1
            else: bad += 1
    print(checked, bad)
    sys.exit(1 if bad else 0)

main()
