#!/usr/bin/env python3
"""analyze_trace_ahead.py -- what really sat AHEAD of a request while it waited, from the raw
event trace (occupancy tables written by sweeps/trace_pcc.sh), instead of the Logger counters.

For every request of a run (octoviz.parquet) that waited at a shared resource, the occupants of
its wait window are taken from occupancy*.parquet (every message: write-backs, fills,
invalidations, supplies, other cores' responses) and classified:

  LLC array port, wait window [s_l2a, e_l2a)   (the L2-Access stage before the grant):
     read_own / read_other   demand reads for a response (RESP, GETS/GETM) by own / other core
     wb_own / wb_other       write-backs and snooped-supply copies written into the array (WB_DATA, WB_INV, SUPPLY*)
     fill                    DRAM fills written into the array (FILL, FILL_ROLLBACK)
     evict                   LLC eviction reads for a memory write (MEM_WRITE)
     cutin                   claims admitted while the port was busy (MSHR/PWB-resident; subset of the above)
  response bus, wait window [s_resp, e_resp - A_res)  (before the request's own transfer slot):
     resp_own_hit / resp_own_miss   younger own-core responses (LLC RESP / supplies) whose request was a hit / a miss (DRAM fill behind it)
     resp_other_hit / resp_other_miss  other cores' responses, same split
     supply                  cache-to-cache supplies (SUPPLY, SUPPLY_DEFERRED) by other cores
     wb                      write-backs on the bus (WB_DATA, WB_INV)

Output: one CSV per run (<run>/newLogger/ahead.csv, one row per waiting request with the counts)
and a summary table over the cells: for oldest hits (stable + transient, oldest > 0) and for
misses, the worst case and the mean of each category, plus the cross-check against the Logger
counters (Array Ahead Writes == wb+fill+evict, Resp Ahead == resp_own_*, Resp Ahead Refills == resp_own_miss).

Usage: python sweeps/analyze_trace_ahead.py results/pcc_par/wl_trace_eembc [--out results/pcc_par/trace_ahead_eembc.csv]
"""
import argparse, glob, os, sys
import duckdb

A_RES = 5

ARRAY_SQL = """
WITH r AS (SELECT id, core, cls, oldest, s_l2a, e_l2a, array_ahead, array_ahead_w FROM read_parquet('{viz}')
           WHERE o_array_s IS NOT NULL AND e_l2a > s_l2a),
     o AS (SELECT msg_id, core AS ocore, kind, start, "end", cutin FROM read_parquet('{occ}') WHERE resource = 'ARRAY' AND comp = 10 AND "end" > start)
SELECT r.id, r.core, r.cls, r.oldest, r.array_ahead, r.array_ahead_w,
       count(o.msg_id) AS n_ahead,
       count(*) FILTER (WHERE o.kind IN ('RESP','GETS','GETM','DEMAND') AND o.ocore = r.core) AS read_own,
       count(*) FILTER (WHERE o.kind IN ('RESP','GETS','GETM','DEMAND') AND o.ocore <> r.core) AS read_other,
       count(*) FILTER (WHERE o.kind IN ('WB_DATA','WB_INV','SUPPLY','SUPPLY_DEFERRED') AND o.ocore = r.core) AS wb_own,
       count(*) FILTER (WHERE o.kind IN ('WB_DATA','WB_INV','SUPPLY','SUPPLY_DEFERRED') AND o.ocore <> r.core) AS wb_other,
       count(*) FILTER (WHERE o.kind IN ('FILL','FILL_ROLLBACK')) AS fill,
       count(*) FILTER (WHERE o.kind = 'MEM_WRITE') AS evict,
       count(*) FILTER (WHERE o.cutin) AS cutin
FROM r LEFT JOIN o ON o.start < r.e_l2a AND o."end" > r.s_l2a AND NOT (o.msg_id = r.id AND o.ocore = r.core)
GROUP BY ALL
"""

RESP_SQL = """
WITH r AS (SELECT id, core, cls, oldest, s_resp, e_resp - {ares} AS w_end, resp_ahead, resp_ahead_ref FROM read_parquet('{viz}')
           WHERE resp > {ares}),
     m AS (SELECT id, core, cls = 'miss' AS miss, issue FROM read_parquet('{viz}')),
     o AS (SELECT o.msg_id, o.core AS ocore, o.kind, o.start, o."end", coalesce(m.miss, FALSE) AS miss, m.issue
           FROM read_parquet('{occ}') o LEFT JOIN m ON m.id = o.msg_id AND m.core = o.core
           WHERE o.resource = 'RESP_BUS' AND o."end" > o.start)
SELECT r.id, r.core, r.cls, r.oldest, r.resp_ahead, r.resp_ahead_ref,
       count(o.msg_id) AS n_ahead,
       count(*) FILTER (WHERE o.kind IN ('RESP','SUPPLY','SUPPLY_DEFERRED') AND o.ocore = r.core AND NOT o.miss) AS resp_own_hit,
       count(*) FILTER (WHERE o.kind IN ('RESP','SUPPLY','SUPPLY_DEFERRED') AND o.ocore = r.core AND o.miss) AS resp_own_miss,
       count(*) FILTER (WHERE o.kind = 'RESP' AND o.ocore <> r.core AND NOT o.miss) AS resp_other_hit,
       count(*) FILTER (WHERE o.kind = 'RESP' AND o.ocore <> r.core AND o.miss) AS resp_other_miss,
       count(*) FILTER (WHERE o.kind IN ('SUPPLY','SUPPLY_DEFERRED') AND o.ocore <> r.core) AS supply,
       count(*) FILTER (WHERE o.kind IN ('WB_DATA','WB_INV')) AS wb
FROM r LEFT JOIN o ON o.start < r.w_end AND o."end" > r.s_resp AND NOT (o.msg_id = r.id AND o.ocore = r.core)
GROUP BY ALL
"""

def occ_path(d):
    s = os.path.join(d, "occupancy.parquet")
    return (s if os.path.exists(s) else os.path.join(d, "occupancy_*.parquet")).replace(os.sep, "/")

def run_dirs(root):
    for p in sorted(glob.glob(os.path.join(root, "*", "*", "newLogger", "octoviz.parquet"))):
        d = os.path.dirname(p); cell = os.path.basename(os.path.dirname(os.path.dirname(d))); bench = os.path.basename(os.path.dirname(d))
        if glob.glob(os.path.join(d, "occupancy*.parquet")): yield cell, bench, d

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("root"); ap.add_argument("--out"); ap.add_argument("--a-res", type=int, default=A_RES)
    a = ap.parse_args()
    out = a.out or os.path.join(os.path.dirname(a.root.rstrip("/\\")), "trace_ahead_" + os.path.basename(a.root.rstrip("/\\")).replace("wl_trace_", "") + ".csv")
    con = duckdb.connect()
    rows = []
    for cell, bench, d in run_dirs(a.root):
        viz = os.path.join(d, "octoviz.parquet").replace(os.sep, "/"); occ = occ_path(d)
        con.execute(f"CREATE OR REPLACE TABLE arr AS {ARRAY_SQL.format(viz=viz, occ=occ)}")
        con.execute(f"CREATE OR REPLACE TABLE rsp AS {RESP_SQL.format(viz=viz, occ=occ, ares=a.a_res)}")
        con.execute(f"COPY (SELECT * FROM arr) TO '{os.path.join(d, 'ahead_array.csv').replace(os.sep, '/')}' (HEADER)")
        con.execute(f"COPY (SELECT * FROM rsp) TO '{os.path.join(d, 'ahead_resp.csv').replace(os.sep, '/')}' (HEADER)")
        for grp, cond in (("oldest_hit", "oldest > 0 AND cls IN ('stable','transient')"), ("miss", "cls = 'miss'"), ("all", "TRUE")):
            s = con.execute(f"""SELECT count(*), max(n_ahead), avg(n_ahead),
                   max(read_own), max(read_other), max(wb_own), max(wb_other), max(fill), max(evict), max(cutin),
                   avg(wb_own + wb_other + fill + evict), sum(CASE WHEN array_ahead_w <> wb_own + wb_other + fill + evict THEN 1 ELSE 0 END),
                   sum(CASE WHEN array_ahead <> n_ahead THEN 1 ELSE 0 END)
                   FROM arr WHERE {cond}""").fetchone()
            t = con.execute(f"""SELECT count(*), max(n_ahead), avg(n_ahead),
                   max(resp_own_hit), max(resp_own_miss), max(resp_other_hit), max(resp_other_miss), max(supply), max(wb),
                   avg(resp_own_hit + resp_own_miss), avg(resp_other_hit + resp_other_miss + supply),
                   sum(CASE WHEN resp_ahead <> resp_own_hit + resp_own_miss THEN 1 ELSE 0 END),
                   sum(CASE WHEN resp_ahead_ref <> resp_own_miss THEN 1 ELSE 0 END)
                   FROM rsp WHERE {cond}""").fetchone()
            rows.append([cell, bench, grp, *s, *t])
        print(f"{cell} {bench}: array rows {con.execute('SELECT count(*) FROM arr').fetchone()[0]}, resp rows {con.execute('SELECT count(*) FROM rsp').fetchone()[0]}")
    hdr = ("cell,benchmark,group,arr_n,arr_wc_ahead,arr_avg_ahead,arr_wc_read_own,arr_wc_read_other,arr_wc_wb_own,arr_wc_wb_other,arr_wc_fill,arr_wc_evict,arr_wc_cutin,"
           "arr_avg_writes,arr_ctr_writes_mismatch,arr_ctr_ahead_mismatch,"
           "resp_n,resp_wc_ahead,resp_avg_ahead,resp_wc_own_hit,resp_wc_own_miss,resp_wc_other_hit,resp_wc_other_miss,resp_wc_supply,resp_wc_wb,"
           "resp_avg_own,resp_avg_other,resp_ctr_ahead_mismatch,resp_ctr_ref_mismatch")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(hdr + "\n")
        for r in rows: f.write(",".join("" if v is None else (f"{v:.2f}" if isinstance(v, float) else str(v)) for v in r) + "\n")
    print("->", out)

if __name__ == "__main__":
    main()
