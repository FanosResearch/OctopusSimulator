#!/usr/bin/env python3
"""Per-source throughput with every source backlogged, from compare_garnet.py's saturation runs
(docs/NoC_Garnet_Validation.md s7.2). Prints total, min, max per source and Jain's index.
  fairness.py <compare_garnet.py --out dir, e.g. test/network/out/garnet/mesh88>"""
import re, csv, sys
mesh=sys.argv[1].rstrip("/")
pats=['uniform_random','transpose','bit_complement','bit_reverse','shuffle','tornado','neighbor','bit_rotation']
print('pattern,sim,total,min_src,max_src,jain')
for p in pats:
    f=f'{mesh}/gar/{p}/sat-0.200000-w10000-m20000/stats.txt'
    src={}
    for line in open(f):
        m=re.match(r'system\.ruby\.network\.data_traffic_distribution\.n(\d+)\.n(\d+)\s+(\S+)',line)
        if m: src[int(m.group(1))]=src.get(int(m.group(1)),0)+float(m.group(3))*5/20000
    def stat(v):
        v=[x for x in v if x>0]; n=len(v); s=sum(v); return s/n, min(v), max(v), s*s/(n*sum(x*x for x in v))
    g=stat(src.values())
    def gen(t):
        return {int(r[1]):int(r[4]) for r in csv.reader(open(f'{mesh}/oct/{p}/sat-r1000-t{t}/NetworkStats.csv')) if r[0]=='generator'}
    a,b=gen(10000),gen(30000)
    o=stat([(b[i]-a[i])*5/20000 for i in a])
    print(f'{p},garnet,{g[0]:.3f},{g[1]:.3f},{g[2]:.3f},{g[3]:.3f}'); print(f'{p},octopus,{o[0]:.3f},{o[1]:.3f},{o[2]:.3f},{o[3]:.3f}')
