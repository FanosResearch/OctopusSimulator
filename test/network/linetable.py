#!/usr/bin/env python3
"""The coherence story of one cache line from a raw trace (OCTOPUS_TRACE): every FSM transition
of every controller on that line, interleaved in time with the network events of the messages that
carried it (injection and ejection at the endpoints, with the class and the hops).
   linetable.py <run dir> <addr hex> [last N transitions, default all] [--md]
The line is addr & ~63. Columns: cycle, who, what, msg, from->to, kind, class, detail."""
import sys, os, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools', 'octoviz'))
import tracefile as tf

def main():
    run, addr = sys.argv[1], int(sys.argv[2], 16)
    last = None; md = '--md' in sys.argv
    for x in sys.argv[3:]:
        if x.isdigit(): last = int(x)
    line = addr & ~63
    a = tf.read_all(os.path.join(run, 'trace.bin'))
    import numpy as np
    a = a[(a['addr'] & np.uint64(~63 & 0xffffffffffffffff)) == np.uint64(line)]
    names = tf.read_names(os.path.join(run, 'trace.bin'))
    R = {n: i for i, n in enumerate(tf.RESOURCE)}
    rows = []   # (cycle, order, who, what, msg, fromto, kind, cls, detail)
    # messages: injection (bus ENTER) and ejection (bus EXIT); from = trace core? no: the sender is
    # not in the bus record, so it is taken from the first router ENTER's router (the implicit or
    # attached router) -- we report the destination (flags) and the requester (core)
    for r in a:
        res, ph = int(r['resource']), int(r['phase'])
        cyc, mid, kind = int(r['cycle']), int(r['msg_id']), tf.KIND[int(r['kind'])] if int(r['kind']) < len(tf.KIND) else str(int(r['kind']))
        if res == R['FSM']:
            comp = int(r['comp']); st = names.get(comp, {}).get('states', []); ev = names.get(comp, {}).get('events', [])
            old, new = int(r['flags']) >> 8, int(r['flags']) & 0xff
            e = ev[ph] if ph < len(ev) else str(ph)
            rows.append((cyc, 1, 'LLC' if comp == 10 else 'L1 %d' % comp, 'FSM', mid, '', kind,
                         '', '%s --%s--> %s' % (st[old] if old < len(st) else old, e, st[new] if new < len(st) else new)))
        elif res in (R['REQ_BUS'], R['RESP_BUS']):
            dst = int(r['flags'])
            rows.append((cyc, 0 if ph == 0 else 2, 'net', 'inject' if ph == 0 else 'eject', mid, '-> %s' % ('LLC' if dst == 10 else 'L1 %d' % dst), kind,
                         'req' if res == R['REQ_BUS'] else 'data', 'owner %d' % int(r['core'])))
    rows.sort(key=lambda x: (x[0], x[1]))
    if last:
        fsm_idx = [i for i, x in enumerate(rows) if x[3] == 'FSM']
        if len(fsm_idx) > last: rows = rows[fsm_idx[-last]:]
    hdr = ('cycle', 'who', 'what', 'msg', 'to', 'kind', 'class', 'detail')
    if md:
        print('| ' + ' | '.join(hdr) + ' |'); print('|' + '---|' * len(hdr))
        for x in rows: print('| %d | %s | %s | %d | %s | %s | %s | %s |' % (x[0], x[2], x[3], x[4], x[5], x[6], x[7], x[8]))
    else:
        print('%-8s %-5s %-7s %-7s %-8s %-8s %-5s %s' % hdr)
        for x in rows: print('%-8d %-5s %-7s %-7d %-8s %-8s %-5s %s' % (x[0], x[2], x[3], x[4], x[5], x[6], x[7], x[8]))

main()
