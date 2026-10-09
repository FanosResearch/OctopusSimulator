#!/usr/bin/env python3
"""Query NetworkStats.csv (docs/NoC_Architecture_Spec.md s15.4).
   netstat.py <file> <section> <key1> <col>        -> prints value<col> (1..4) of the row
   netstat.py <file> sum <section> <col> [local|link]  -> sum of value<col> over the section's rows
     (for 'port' rows, 'local' keeps ports named local*, 'link' the others)"""
import csv, sys

def rows(path):
    with open(path, newline='') as f:
        return list(csv.reader(f))[1:]

def main():
    path, what = sys.argv[1], sys.argv[2]
    R = rows(path)
    if what == 'sum':
        section, col = sys.argv[3], int(sys.argv[4]); which = sys.argv[5] if len(sys.argv) > 5 else ''
        total = 0.0
        for r in R:
            if r[0] != section: continue
            if section == 'port' and which == 'local' and not r[3].startswith('local'): continue
            if section == 'port' and which == 'link' and r[3].startswith('local'): continue
            v = r[3 + col]
            total += float(v) if v else 0
        print(int(total) if total == int(total) else total); return
    section, key1, col = what, sys.argv[3], int(sys.argv[4])
    for r in R:
        if r[0] == section and r[1] == key1:
            print(r[3 + col]); return
    print('MISSING'); sys.exit(1)

main()
