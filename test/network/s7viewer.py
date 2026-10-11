#!/usr/bin/env python3
"""S7-VIEWER-API: the viewer's server answers what the network lanes need, for a converted run.
   s7viewer.py <root dir holding the converted run> <routers> <bank ids, comma separated>
Checks: /api/lanes lists one NOC lane per router and one ARRAY lane per LLC bank; /api/nocheat returns
a cell for every router; /api/occupancy returns router intervals carrying an input port. Prints
"ok <summary>" or "fail <reason>" and exits accordingly. The drawing itself stays a by-eye check."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools', 'octoviz'))
import server

def main():
    root, routers, banks = sys.argv[1], int(sys.argv[2]), [int(b) for b in sys.argv[3].split(',')]
    server.ROOT = os.path.abspath(root)
    runs = server.runs()
    if not runs: print("fail no converted run under", root); sys.exit(1)
    run = runs[0]
    lanes = server.lanes({"run": run["run"]})["lanes"]
    noc = sorted(c for r, c, n in lanes if r == "NOC"); arr = sorted(c for r, c, n in lanes if r == "ARRAY" and c >= 10)
    if noc != list(range(routers)): print("fail router lanes", noc); sys.exit(1)
    if arr != sorted(banks): print("fail bank lanes", arr); sys.exit(1)
    heat = server.nocheat({"run": run["run"], "t0": str(run["tmin"]), "t1": str(run["tmax"])})["cells"]
    if sorted(set(c[0] for c in heat)) != list(range(routers)): print("fail heat routers", sorted(set(c[0] for c in heat))); sys.exit(1)
    mid = (run["tmin"] + run["tmax"]) // 2
    occ = server.occupancy({"run": run["run"], "t0": str(mid), "t1": str(mid + 2000)})
    cols = occ["cols"]; rows = [r for r in occ.get("rows", []) if r[cols.index("resource")] == "NOC"]
    if not rows: print("fail no router intervals in", mid, mid + 2000); sys.exit(1)
    print("ok %d router lanes, banks %s, %d heat cells, %d router intervals in a 2000-cycle window" % (len(noc), arr, len(heat), len(rows)))

main()
