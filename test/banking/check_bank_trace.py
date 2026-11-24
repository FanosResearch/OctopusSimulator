#!/usr/bin/env python3
"""check_bank_trace.py -- the banking properties of a multi-bank LLC run (design doc s8.3).

    check_bank_trace.py <trace.bin> --banks 10,11 --shift 0 [--block 64]

Reads the raw event trace (--trace, docs/Trace.md) with the standard library only (no numpy).
A bank record is one whose comp is a bank id and whose resource is LLC, ARRAY, LLC_QUEUE or FSM:
every one of them is written by the bank that admitted or acts on the message, after the bank
filter (s4.4) has dropped what is not for it. Three checks:

  home     every bank record's line has that bank as its home:
           banks[((addr / block) >> shift) % n] == comp   (AddressMap::home, Interleave::index)
  single   no message is handled by more than one bank, so no request receives data from two
           banks: every msg_id seen in bank records has exactly one bank
  reached  every request on the request bus (kind GETS, GETM, PUTM, PUTS) is admitted by exactly
           one bank, and it is the home bank (the sender named homeOf(addr), s4.3); a request sent
           in the last 1000 cycles of the trace is counted as in flight, not as a failure

Also prints the bank-record count per bank and notes a bank with none (mi_pingpong's 4 requests
cannot reach 4 banks; every set of every bank being used is the occupancy test of B5). Exit 0 when
the three checks hold.
"""
import argparse, os, struct, sys
from collections import defaultdict

MAGIC = b"OCTV2\0\0\0"
REC = struct.Struct("<QQQBBBBHH")   # cycle, msg_id, addr, resource, phase, kind, core, comp, flags
LLC, REQ_BUS, ARRAY, LLC_QUEUE, FSM = 4, 2, 9, 10, 11
EXIT = 2
IN_FLIGHT = 1000   # cycles
REQ_KINDS = {2: "GETS", 3: "GETM", 4: "PUTM", 16: "PUTS"}
BANK_RES = {LLC, ARRAY, LLC_QUEUE, FSM}

def chunks(path):
    """(offset, count) of each 64K-record chunk; a file without a trailer is one run of records."""
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        if f.read(8) != MAGIC: sys.exit(f"{path}: not an Octopus v2 trace")
        f.seek(size - 24); idx_off, n = struct.unpack("<QQ", f.read(16))
        if f.read(8) != MAGIC: return [(8, (size - 8) // REC.size)]
        f.seek(idx_off)
        return [struct.unpack("<QQ", f.read(48)[:16]) for _ in range(n)]

def records(path):
    with open(path, "rb") as f:
        for off, count in chunks(path):
            f.seek(off)
            yield from REC.iter_unpack(f.read(count * REC.size))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace"); ap.add_argument("--banks", required=True)
    ap.add_argument("--shift", type=int, default=0); ap.add_argument("--block", type=int, default=64)
    a = ap.parse_args()
    banks = [int(x) for x in a.banks.split(",")]; n = len(banks); bankset = set(banks)
    home = lambda addr: banks[((addr // a.block) >> a.shift) % n]

    per_bank = defaultdict(int)
    handled = defaultdict(set)    # msg_id -> banks with a record of it
    requests = {}                 # msg_id -> (kind, addr) of a request on the request bus
    bad_home = []; n_bad_home = 0; last = 0
    for cycle, mid, addr, res, phase, kind, core, comp, flags in records(a.trace):
        last = max(last, cycle)
        if res == REQ_BUS and phase == EXIT and kind in REQ_KINDS:
            requests.setdefault(mid, (kind, addr, cycle))
        elif res in BANK_RES and comp in bankset:
            per_bank[comp] += 1; handled[mid].add(comp)
            if home(addr) != comp:
                n_bad_home += 1
                if len(bad_home) < 5: bad_home.append((cycle, mid, addr, res, kind, comp))

    multi = {m: b for m, b in handled.items() if len(b) > 1}
    # a request sent in the last IN_FLIGHT cycles may not have reached its bank when the run ended
    unreached = [m for m, (k, addr, c) in requests.items() if m not in handled and last - c >= IN_FLIGHT]
    in_flight = sum(1 for m, (k, addr, c) in requests.items() if m not in handled and last - c < IN_FLIGHT)
    wrong = [m for m, (k, addr, c) in requests.items() if m in handled and handled[m] != {home(addr)}]

    print("bank records: " + ", ".join(f"{b}:{per_bank[b]}" for b in banks)
          + f"; {len(handled)} messages, {len(requests)} bus requests ({in_flight} in flight at the end)")
    fails = 0
    for name, bad, nbad, show in (
            ("home", bad_home, n_bad_home, lambda r: "cycle %d msg %d addr 0x%x res %d kind %d at bank %d, home %d" % (*r, home(r[2]))),
            ("single", list(multi), len(multi), lambda m: "msg %d handled by banks %s" % (m, sorted(multi[m]))),
            ("reached", unreached + wrong, len(unreached) + len(wrong), lambda m: "msg %d %s 0x%x: banks %s, home %d" % (
                m, REQ_KINDS[requests[m][0]], requests[m][1], sorted(handled.get(m, ())), home(requests[m][1])))):
        print(f"{name}: {'PASS' if not nbad else 'FAIL (%d)' % nbad}")
        for x in bad[:5]: print("   " + show(x))
        fails += bool(bad)
    unused = [b for b in banks if per_bank[b] == 0]
    if unused: print(f"note: no records at banks {unused} (a workload too small to reach them, not a fault)")
    sys.exit(1 if fails else 0)

if __name__ == "__main__":
    main()
