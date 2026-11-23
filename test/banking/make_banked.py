#!/usr/bin/env python3
"""make_banked.py -- derive an N-bank LLC preset from a single-LLC bus preset
(docs/LLC_Banking_and_Partitioning_Design.md s4.1).

  python3 test/banking/make_banked.py <in.csv> <out.csv> <banks> [shift]

Bank k gets id 10+k, an equal share of the LLC's capacity, MSHRs, write-back buffer and
processing queue (the banks together are the original LLC), and every bank is an endpoint of
bus[0] and bus[1]. The L1s get home_ids = the bank ids and home_interleave_shift = shift
(0: consecutive lines alternate; 6: 4 KiB pages). Comments in the output name the source.
"""
import csv, sys

src, dst, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
shift = int(sys.argv[4]) if len(sys.argv) > 4 else 0
ids = [str(10 + k) for k in range(n)]
SPLIT = ("m_data_handler.cache_size", "num_mshr", "m_data_handler.pwb_size", "processing_queue_size")

rows = list(csv.reader(open(src, newline="")))
# the note goes in a comment column: Configurable reads a line's first field into a fixed buffer
out = [["llc_banks(i)", str(n), "", "", f"#{n}-bank LLC derived from {src.split('/')[-1]} by test/banking/make_banked.py (home_interleave_shift {shift})"]]
for r in rows:
    if not r or not r[0]:
        out.append(r); continue
    key = r[0]
    if key.startswith("llc_controller.m_id("):
        for k, i in enumerate(ids):
            out.append([f"llc_controller[{k}].m_id(i)", i])
        continue
    if key.startswith("llc_controller."):
        r = [key.replace("llc_controller.", "llc_controller[*].", 1)] + r[1:]
        if any(r[0].startswith(f"llc_controller[*].{s}(") for s in SPLIT) and r[1].lstrip("-").isdigit() and int(r[1]) > 0:
            r[1] = str(max(1, int(r[1]) // n))
    elif key.startswith("bus[0].upper_level_cache_ids("):
        r = [key] + ids
    elif key.startswith("bus[1].m_lower_level_ids("):
        r = [key] + ids
    elif key.startswith("bus[1].interconnect_controller.candidates_id("):
        vals = [v for v in r[1:] if v and v not in ("10",) and not v.startswith("#")]
        r = [key] + vals + ids
    elif key.startswith("cache_controller[*].m_shared_memory_id("):
        out.append(r)
        out.append(["cache_controller[*].home_ids(vi)"] + ids)
        out.append(["cache_controller[*].home_interleave_shift(i)", str(shift)])
        continue
    out.append(r)
if not any(r and r[0].startswith("bus[0].upper_level_cache_ids(") for r in out):
    out.insert(1, ["bus[0].upper_level_cache_ids(vi)"] + ids)   # the preset used the Bus.csv default (10)
with open(dst, "w", newline="") as f:
    csv.writer(f, lineterminator="\n").writerows(out)
