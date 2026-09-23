#!/usr/bin/env python3
"""server.py -- local query server for the Octopus request/resource timeline viewer.

Serves tools/octoviz/static/ and a small JSON API over the Parquet files written by
convert.py.  Everything stays on this machine.

  GET /api/runs                       -> [{run, requests, tmin, tmax}]  (runs found under --root)
  GET /api/window?run=..&t0=..&t1=..  -> rows (small window) or per-bin aggregates (wide window)
        filters: core=0,1  addr=<hex> mask=<hex>  id=<n> | ids=a-b  cls=stable,miss,...
                 stall=<state ids>  res=reqbus,respbus,array,membus,dram (only requests that used
                 these resources)  oldest=1  ahead_min=<k>  limit=<n>
        LOD: if (t1-t0) > --lod-cycles (default 40000) the response is {mode:'bins', bins:[...]}
             with per-bin request counts and per-resource occupancy; else {mode:'rows', rows:[...]}
  GET /api/request?run=..&id=..&core=..  -> the row + the other requests on the same 64-B line
                                            overlapping its lifetime (coalescing/transients context)
  GET /api/minimap?run=..&bins=..        -> request density over the whole run
  GET /api/hist?run=..&col=total&..filters  -> histogram + CDF + p50/p90/p99/max of a column over the
                                            filtered set (pos=1: only rows where the column is > 0)
  GET /api/hotlines?run=..&by=coalesced&n=25 -> top 64-B lines by requests of that class in the window
  GET /api/occupancy?run=..&t0=..&t1=..  -> resource-occupancy intervals of ALL traffic from the raw event
        trace (occupancy.parquet next to octoviz.parquet, see docs/Trace.md): rows (small window,
        filters kinds=GETS,FILL,.. core=0,1 res=REQ_BUS,ARRAY,..) or per-bin busy cycles per lane
        (wide window). {"trace": false} when the run has no trace. addr=<hex>&mask=<hex> filters by line.
  GET /api/fsm?run=..&addr=<hex>[&mask=<hex>][&t0=..&t1=..][&limit=3000] -> the coherence transitions
        every controller made on that line (fsm.parquet from the trace): cycle, agent, message, event,
        old -> new state, in cycle order. The UI turns it into the per-line transition table.

Usage: python tools/octoviz/server.py --root results/pcc_par/wl_eembc [--port 8765]
"""
import argparse, glob, json, os, urllib.parse
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
import duckdb

ROOT = None; LOD = 40000; _DB = duckdb.connect()
# DuckDB connections are not thread-safe; the threaded HTTP server hands each request its own cursor.
class _Q:
    def execute(self, sql): return _DB.cursor().execute(sql)
CON = _Q()
RESOURCES = ["reqbus", "array", "membus", "dram", "respbus"]

def runs():
    out = []
    for p in sorted(glob.glob(os.path.join(ROOT, "**", "octoviz.parquet"), recursive=True)):
        rel = os.path.relpath(os.path.dirname(os.path.dirname(p)), ROOT).replace(os.sep, "/")
        n, tmin, tmax = CON.execute(f"SELECT count(*), min(issue), max(retire) FROM '{p.replace(os.sep,'/')}'").fetchone()
        d = os.path.dirname(p)   # occupancy/fsm: a single file, or parts occupancy_NNN.parquet read as one table via a glob
        occ = next((g for g in (os.path.join(d, "occupancy.parquet"), os.path.join(d, "occupancy_*.parquet")) if glob.glob(g)), None)
        fsm = next((g for g in (os.path.join(d, "fsm.parquet"), os.path.join(d, "fsm_*.parquet")) if glob.glob(g)), None)
        out.append({"run": rel, "path": p.replace(os.sep, "/"), "requests": n, "tmin": tmin, "tmax": tmax,
                    "trace": occ is not None, "occ": occ.replace(os.sep, "/") if occ else None, "fsm": fsm.replace(os.sep, "/") if fsm else None})
    return out

def fsm_path(run):
    for r in runs():
        if r["run"] == run: return r["fsm"]
    raise KeyError(run)

def fsm(q):
    p = fsm_path(q["run"])
    if not p: return {"trace": False, "rows": []}
    mask = int(q.get("mask", "ffffffffffffffc0"), 16); addr = int(q["addr"], 16) & mask
    w = [f"(addr & {mask}) = {addr}"]
    if "t0" in q and "t1" in q: w.append(f"cycle BETWEEN {int(q['t0'])} AND {int(q['t1'])}")
    W = " AND ".join(w); limit = int(q.get("limit", 3000))
    cols = "cycle, comp, msg_id, kind, core, event, old, new, addr"
    rows = CON.execute(f"SELECT {cols} FROM '{p}' WHERE {W} ORDER BY cycle, comp LIMIT {limit}").fetchall()
    total = CON.execute(f"SELECT count(*) FROM '{p}' WHERE {W}").fetchone()[0]
    agents = [r[0] for r in CON.execute(f"SELECT DISTINCT comp FROM '{p}' ORDER BY comp").fetchall()]
    return {"trace": True, "cols": cols.replace(" ", "").split(","), "rows": [[*r[:8], format(r[8], "x")] for r in rows], "total": total, "agents": agents}

def occ_path(run):
    for r in runs():
        if r["run"] == run: return r["occ"]
    raise KeyError(run)

# trace lanes: (resource, comp) -> the memory bus is full duplex (comp = direction), see docs/Trace.md
TRACE_LANES = [("REQ_BUS", 0), ("SVC_BUS", 0), ("ARRAY", 10), ("MEM_BUS", 0), ("MEM_BUS", 1), ("DRAM", 100), ("RESP_BUS", 0)]
OCC_COLS = 'resource, comp, start, "end", kind, core, msg_id, flags, cutin'

def occupancy(q):
    p = occ_path(q["run"])
    if not p: return {"trace": False}
    t0, t1 = int(q["t0"]), int(q["t1"])
    w = [f'"end" >= {t0} AND start <= {t1}', "resource <> 'LLC_QUEUE'"]
    if q.get("kinds"): w.append("kind IN (" + ",".join(f"'{k}'" for k in q["kinds"].split(",") if k.isalnum() or "_" in k) + ")")
    if q.get("core"): w.append("core IN (" + ",".join(str(int(c)) for c in q["core"].split(",")) + ")")
    if q.get("res"): w.append("resource IN (" + ",".join(f"'{r}'" for r in q["res"].split(",") if r.replace("_", "").isalnum()) + ")")
    if q.get("addr"):
        mask = int(q.get("mask", "ffffffffffffffff"), 16); addr = int(q["addr"], 16) & mask
        w.append(f"(addr & {mask}) = {addr}")
    W = " AND ".join(w)
    if t1 - t0 <= LOD:
        limit = int(q.get("limit", 60000))
        rows = CON.execute(f"SELECT {OCC_COLS} FROM '{p}' WHERE {W} ORDER BY start LIMIT {limit}").fetchall()
        total = CON.execute(f"SELECT count(*) FROM '{p}' WHERE {W}").fetchone()[0]
        return {"trace": True, "mode": "rows", "cols": OCC_COLS.replace('"', "").replace(" ", "").split(","), "rows": rows, "total": total, "t0": t0, "t1": t1}
    nb = int(q.get("bins", 400)); bw = max(1, (t1 - t0) // nb)
    bins = CON.execute(f"""
      WITH b AS (SELECT range AS i, {t0} + range*{bw} AS b0, {t0} + (range+1)*{bw} AS b1 FROM range({nb})),
           o AS (SELECT * FROM '{p}' WHERE {W})
      SELECT b.i, o.resource, o.comp, SUM(GREATEST(0, LEAST(o."end", b.b1) - GREATEST(o.start, b.b0))) AS busy,
             SUM(CASE WHEN o.kind IN ('FILL','WB_DATA','WB_INV','INV','MEM_WRITE','MEM_READ','PUTM','EVICT') THEN GREATEST(0, LEAST(o."end", b.b1) - GREATEST(o.start, b.b0)) ELSE 0 END) AS busy_nonreq
      FROM b JOIN o ON o."end" >= b.b0 AND o.start < b.b1 GROUP BY 1,2,3 ORDER BY 1,2,3""").fetchall()
    return {"trace": True, "mode": "bins", "bw": bw, "cols": ["i", "resource", "comp", "busy", "busy_nonreq"], "bins": bins, "t0": t0, "t1": t1}

def run_path(run):
    for r in runs():
        if r["run"] == run: return r["path"]
    raise KeyError(run)

def where(q):
    w = []
    if "t0" in q and "t1" in q: w.append(f"retire >= {int(q['t0'])} AND issue <= {int(q['t1'])}")
    if q.get("core"): w.append("core IN (" + ",".join(str(int(c)) for c in q["core"].split(",")) + ")")
    if q.get("addr"):
        mask = int(q.get("mask", "ffffffffffffffff"), 16); addr = int(q["addr"], 16) & mask
        w.append(f"(addr & {mask}) = {addr}")
    if q.get("id"): w.append(f"id = {int(q['id'])}")
    if q.get("ids"):
        a, b = q["ids"].split("-"); w.append(f"id BETWEEN {int(a)} AND {int(b)}")
    if q.get("cls"): w.append("cls IN (" + ",".join(f"'{c}'" for c in q["cls"].split(",")) + ")")
    if q.get("stall"): w.append("llc_stall IN (" + ",".join(str(int(s)) for s in q["stall"].split(",")) + ")")
    if q.get("res"):
        for r in q["res"].split(","):
            if r in RESOURCES: w.append(f"o_{r}_s IS NOT NULL")
    if q.get("oldest") == "1": w.append("oldest > 0")
    if q.get("ahead_min"): w.append(f"resp_ahead >= {int(q['ahead_min'])}")
    return " AND ".join(w) if w else "TRUE"

def window(q):
    p = run_path(q["run"]); t0, t1 = int(q["t0"]), int(q["t1"]); W = where(q)
    cols = ("id,core,addr_hex,issue,retire,total,oldest,t_oldest,cls,llc_state,llc_stall,llc_gate,resp_ahead,"
            "resp_ahead_ref,array_ahead,array_ahead_w,s_l1s,e_l1s,s_reqb,e_reqb,s_l2s,e_l2s,s_membus,e_membus,"
            "s_dram,e_dram,s_l2a,e_l2a,s_resp,e_resp,s_l1a,e_l1a," +
            ",".join(f"o_{r}_s,o_{r}_e" for r in RESOURCES))
    if t1 - t0 <= LOD:
        limit = int(q.get("limit", 20000))
        rows = CON.execute(f"SELECT {cols} FROM '{p}' WHERE {W} ORDER BY issue LIMIT {limit}").fetchall()
        total = CON.execute(f"SELECT count(*) FROM '{p}' WHERE {W}").fetchone()[0]
        return {"mode": "rows", "cols": cols.split(","), "rows": rows, "total": total, "t0": t0, "t1": t1}
    nb = int(q.get("bins", 400)); bw = max(1, (t1 - t0) // nb)
    # per bin: requests issued, and for each resource the number of occupancy cycles falling in the bin
    occ = ", ".join(
        f"SUM(CASE WHEN o_{r}_s IS NULL THEN 0 ELSE GREATEST(0, LEAST(o_{r}_e, b.b1) - GREATEST(o_{r}_s, b.b0)) END) AS occ_{r}" for r in RESOURCES)
    sql = f"""
      WITH b AS (SELECT range AS i, {t0} + range*{bw} AS b0, {t0} + (range+1)*{bw} AS b1 FROM range({nb})),
           r AS (SELECT * FROM '{p}' WHERE {W})
      SELECT b.i, b.b0, b.b1,
             COUNT(r.id) FILTER (WHERE r.issue >= b.b0 AND r.issue < b.b1) AS issued,
             COUNT(r.id) FILTER (WHERE r.cls='coalesced' AND r.issue >= b.b0 AND r.issue < b.b1) AS coalesced,
             COUNT(r.id) FILTER (WHERE r.cls='miss' AND r.issue >= b.b0 AND r.issue < b.b1) AS misses,
             MAX(r.total) FILTER (WHERE r.issue >= b.b0 AND r.issue < b.b1) AS max_total,
             {occ}
      FROM b LEFT JOIN r ON r.retire >= b.b0 AND r.issue < b.b1
      GROUP BY b.i, b.b0, b.b1 ORDER BY b.i"""
    bins = CON.execute(sql).fetchall()
    return {"mode": "bins", "bw": bw, "cols": ["i", "b0", "b1", "issued", "coalesced", "misses", "max_total"] + [f"occ_{r}" for r in RESOURCES],
            "bins": bins, "t0": t0, "t1": t1}

def request(q):
    p = run_path(q["run"]); rid = int(q["id"]); core = q.get("core")
    W = f"id = {rid}" + (f" AND core = {int(core)}" if core else "")
    cur = CON.execute(f"SELECT * FROM '{p}' WHERE {W} LIMIT 1"); row = cur.fetchone()
    if row is None: return {"error": "not found"}
    r = {d[0]: (int(v) if hasattr(v, "item") else v) for d, v in zip(cur.description, row)}   # plain dict, no pandas
    line = int(r["addr"]) & ~63
    same = CON.execute(f"""SELECT id, core, addr_hex, issue, retire, total, cls, llc_state, llc_stall, oldest
                           FROM '{p}' WHERE (addr & ~63) = {line} AND retire >= {r['issue']} - 2000 AND issue <= {r['retire']} + 2000
                           ORDER BY issue""").fetchall()
    out = {"row": r, "same_line": same}
    op = occ_path(q["run"])
    if op:   # every trace interval carrying this id: its own transfers, the supply/fill that served it, the queue residency
        out["occ"] = CON.execute(f"SELECT {OCC_COLS} FROM '{op}' WHERE msg_id = {rid} ORDER BY start").fetchall()
    return out

HIST_COLS = {"total","oldest","eff","l1s","reqb","l2s","l2a","resp","membus","dram","l1a","resp_ahead","array_ahead","array_ahead_w"}

def hist(q):
    """Histogram + CDF + percentiles of one column over the filtered set (window included)."""
    p = run_path(q["run"]); col = q.get("col", "total"); W = where(q)
    if col not in HIST_COLS: raise ValueError("bad col")
    nb = int(q.get("bins", 60)); only_pos = q.get("pos") == "1"
    W2 = W + (f" AND {col} > 0" if only_pos else "")
    st = CON.execute(f"""SELECT count(*), avg({col}), quantile_cont({col}, 0.5), quantile_cont({col}, 0.9),
                                quantile_cont({col}, 0.99), max({col}), min({col}) FROM '{p}' WHERE {W2}""").fetchone()
    n, mean, p50, p90, p99, mx, mn = st
    if not n: return {"col": col, "n": 0}
    lo, hi = int(mn), int(mx); bw = max(1, (hi - lo + 1 + nb - 1) // nb)
    bins = CON.execute(f"""SELECT ({col}-{lo})//{bw} AS i, count(*) FROM '{p}' WHERE {W2} GROUP BY i ORDER BY i""").fetchall()
    return {"col": col, "n": n, "mean": mean, "p50": p50, "p90": p90, "p99": p99, "max": mx, "min": mn, "lo": lo, "bw": bw, "bins": bins}

def hotlines(q):
    """Top-N 64-B lines by number of requests of a class (coalesced/transient/miss) in the window."""
    p = run_path(q["run"]); n = int(q.get("n", 25)); by = q.get("by", "coalesced"); W = where({k: v for k, v in q.items() if k in ("run","t0","t1","core")})
    if by not in ("coalesced", "transient", "miss", "stable", "all"): raise ValueError("bad by")
    cond = "TRUE" if by == "all" else f"cls = '{by}'"
    rows = CON.execute(f"""SELECT (addr & ~63) AS line, count(*) FILTER (WHERE {cond}) AS k, count(*) AS total,
                                  max(total) AS wc, max(l2s) AS wc_l2s, min(issue) AS first, max(retire) AS last
                           FROM '{p}' WHERE {W} GROUP BY line HAVING k > 0 ORDER BY k DESC, wc DESC LIMIT {n}""").fetchall()
    return {"by": by, "rows": [[format(r[0], "x"), r[1], r[2], r[3], r[4], r[5], r[6]] for r in rows]}

def minimap(q):
    p = run_path(q["run"]); nb = int(q.get("bins", 1000))
    tmin, tmax = CON.execute(f"SELECT min(issue), max(retire) FROM '{p}'").fetchone(); bw = max(1, (tmax - tmin) // nb)
    rows = CON.execute(f"SELECT (issue-{tmin})//{bw} AS i, count(*), max(total) FROM '{p}' GROUP BY i ORDER BY i").fetchall()
    return {"tmin": tmin, "tmax": tmax, "bw": bw, "bins": rows}

class H(SimpleHTTPRequestHandler):
    def __init__(self, *a, **k): super().__init__(*a, directory=os.path.join(os.path.dirname(__file__), "static"), **k)
    def log_message(self, *a): pass
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")   # the UI is edited often; never let the browser cache it
        super().end_headers()
    def do_GET(self):
        u = urllib.parse.urlparse(self.path); q = {k: v[0] for k, v in urllib.parse.parse_qs(u.query).items()}
        if not u.path.startswith("/api/"): return super().do_GET()
        try:
            if u.path == "/api/runs": body = runs()
            elif u.path == "/api/window": body = window(q)
            elif u.path == "/api/request": body = request(q)
            elif u.path == "/api/minimap": body = minimap(q)
            elif u.path == "/api/hist": body = hist(q)
            elif u.path == "/api/hotlines": body = hotlines(q)
            elif u.path == "/api/occupancy": body = occupancy(q)
            elif u.path == "/api/fsm": body = fsm(q)
            else: self.send_error(404); return
            data = json.dumps(body, default=lambda o: int(o) if hasattr(o, "item") else str(o)).encode()
            self.send_response(200); self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)
        except Exception as e:
            data = json.dumps({"error": str(e)}).encode(); self.send_response(500); self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)

if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("--root", required=True); ap.add_argument("--port", type=int, default=8765); ap.add_argument("--lod-cycles", type=int, default=40000)
    a = ap.parse_args(); ROOT = os.path.abspath(a.root); LOD = a.lod_cycles
    print(f"octoviz: root={ROOT} runs={len(runs())}  ->  http://localhost:{a.port}/")
    ThreadingHTTPServer(("127.0.0.1", a.port), H).serve_forever()
