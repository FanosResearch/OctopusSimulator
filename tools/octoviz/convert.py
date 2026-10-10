#!/usr/bin/env python3
"""convert.py -- turn a run's newLogger/LatencyReport_C*.csv into one Parquet file with the
per-request stage INTERVALS (absolute cycles) that the viewer queries.

Every LatencyReport row tiles a request's life exactly (docs/Logger.md S5):
  issue = Ready + CPU, then L1-Stall, Req-Bus, L2-Stall, [Mem-Bus, DRAM], L2-Access, Resp-Bus,
  L1-Access, whose sum is Total.  From that we derive start/end of each stage, plus the
  service intervals of the shared resources a request OCCUPIED (as opposed to waited for):
    req_bus   : last A_REQ cycles of the Req-Bus stage      (the broadcast slot)
    array     : A_LLC cycles from the array grant (the LLC stamps SERVICE and EXIT in the same
                cycle, so the read itself overlaps the start of the Resp-Bus stage)
    mem_bus   : the L2-DRAM-Bus stage (two legs, not separable from the row -> drawn as one)
    dram      : the DRAM stage
    resp_bus  : last A_RES cycles of the Resp-Bus stage      (the transfer slot)
Rows are sorted by issue so DuckDB's zone maps make cycle-window queries cheap.

Usage: python tools/octoviz/convert.py <newLogger dir> [-o out.parquet] [--a-req 2] [--a-res 5] [--a-llc 10]
                                       [--trace <trace.bin>]
With --trace (a raw event trace written by OCTOPUS_TRACE, docs/Trace.md) it also builds
occupancy.parquet next to the output -- every resource's busy intervals for ALL traffic, including
the write-backs, invalidations, supplies and fills the request rows cannot show -- and checks it
against the request rows (see occupancy.py).
"""
import argparse, glob, os, sys
import duckdb
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

STAGES = ["l1s", "reqb", "l2s", "membus", "dram", "l2a", "resp", "l1a"]   # temporal order

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("logdir")
    ap.add_argument("-o", "--out")
    ap.add_argument("--a-req", type=int, default=2)
    ap.add_argument("--a-res", type=int, default=5)
    ap.add_argument("--a-llc", type=int, default=10)
    ap.add_argument("--trace", help="raw event trace (OCTOPUS_TRACE) of the same run -> occupancy/fsm parquet (streamed, bounded memory)")
    ap.add_argument("--t0", type=int); ap.add_argument("--t1", type=int, help="convert only this cycle window of the trace")
    ap.add_argument("--part-rows", type=int, default=2_000_000, help="rows per Parquet part for giant traces")
    a = ap.parse_args()
    files = sorted(glob.glob(os.path.join(a.logdir, "LatencyReport_C*.csv")))
    if not files:
        sys.exit(f"no LatencyReport_C*.csv under {a.logdir}")
    out = a.out or os.path.join(a.logdir, "octoviz.parquet")
    con = duckdb.connect()
    # read every core file; footer rows (fewer columns / text) are dropped by the numeric filter
    cols = ("id,addr,ready,cpu,l1s,reqb,l2s,l2a,resp,membus,dram,l1a,total,eff,oldest,"
            "llc_state,llc_gate,resp_ahead,resp_ahead_ref,array_ahead,array_ahead_w,llc_stall")
    names = cols.split(",")
    con.execute("CREATE TABLE raw AS SELECT * FROM (SELECT 1) WHERE 0")  # placeholder, replaced below
    parts = []
    for f in files:
        core = int(os.path.basename(f).split("_C")[1].split(".")[0])
        parts.append(f"""
          SELECT {core} AS core, * FROM read_csv('{f.replace(os.sep, '/')}', header=false, skip=1,
                 columns={{{', '.join(f"'{n}': 'VARCHAR'" for n in names)}}}, null_padding=true,
                 ignore_errors=true, all_varchar=true)
          WHERE regexp_matches(total, '^[0-9]+$') AND llc_stall IS NOT NULL""")
    con.execute("DROP TABLE raw")
    con.execute("CREATE TABLE raw AS " + " UNION ALL ".join(parts))
    # cast + derive absolute intervals
    con.execute(f"""
      CREATE TABLE req AS
      SELECT core, CAST(id AS BIGINT) AS id, CAST(addr AS VARCHAR) AS addr_hex,
             CAST(('0x' || addr) AS UBIGINT) AS addr,
             CAST(ready AS BIGINT) AS ready, CAST(cpu AS BIGINT) AS cpu,
             CAST(l1s AS BIGINT) l1s, CAST(reqb AS BIGINT) reqb, CAST(l2s AS BIGINT) l2s,
             CAST(membus AS BIGINT) membus, CAST(dram AS BIGINT) dram, CAST(l2a AS BIGINT) l2a,
             CAST(resp AS BIGINT) resp, CAST(l1a AS BIGINT) l1a,
             CAST(total AS BIGINT) total, CAST(eff AS BIGINT) eff, CAST(oldest AS BIGINT) oldest,
             CAST(llc_state AS INTEGER) llc_state, CAST(llc_gate AS INTEGER) llc_gate,
             CAST(resp_ahead AS INTEGER) resp_ahead, CAST(resp_ahead_ref AS INTEGER) resp_ahead_ref,
             CAST(array_ahead AS INTEGER) array_ahead, CAST(array_ahead_w AS INTEGER) array_ahead_w,
             CAST(llc_stall AS INTEGER) llc_stall
      FROM raw""")
    con.execute(f"""
      CREATE TABLE viz AS
      SELECT *,
        ready + cpu                                   AS issue,
        ready + cpu + total                           AS retire,
        ready + cpu                                   AS s_l1s,   ready + cpu + l1s                          AS e_l1s,
        ready + cpu + l1s                             AS s_reqb,  ready + cpu + l1s + reqb                   AS e_reqb,
        ready + cpu + l1s + reqb                      AS s_l2s,   ready + cpu + l1s + reqb + l2s             AS e_l2s,
        ready + cpu + l1s + reqb + l2s                AS s_membus, ready + cpu + l1s + reqb + l2s + membus   AS e_membus,
        ready + cpu + l1s + reqb + l2s + membus       AS s_dram,  ready + cpu + l1s + reqb + l2s + membus + dram AS e_dram,
        ready + cpu + l1s + reqb + l2s + membus + dram AS s_l2a,  ready + cpu + l1s + reqb + l2s + membus + dram + l2a AS e_l2a,
        ready + cpu + l1s + reqb + l2s + membus + dram + l2a AS s_resp,
        ready + cpu + l1s + reqb + l2s + membus + dram + l2a + resp AS e_resp,
        ready + cpu + l1s + reqb + l2s + membus + dram + l2a + resp AS s_l1a,
        ready + cpu + total                           AS e_l1a,
        CASE WHEN oldest > 0 THEN ready + cpu + total - oldest ELSE NULL END AS t_oldest,
        CASE WHEN dram > 0 OR membus > 0 THEN 'miss'
             WHEN llc_state = -2 THEN 'l1hit'
             WHEN llc_state IN (1,2) THEN 'coalesced'
             WHEN llc_state BETWEEN 6 AND 10 THEN 'transient'
             ELSE 'stable' END AS cls,
        -- occupancy (service) intervals of shared resources; NULL when the resource was not used
        CASE WHEN reqb > 0 THEN GREATEST(ready+cpu+l1s, ready+cpu+l1s+reqb-{a.a_req}) END AS o_reqbus_s,
        CASE WHEN reqb > 0 THEN ready+cpu+l1s+reqb END AS o_reqbus_e,
        CASE WHEN reqb > 0 AND llc_state <> -2 THEN ready+cpu+l1s+reqb+l2s+membus+dram+l2a END AS o_array_s,
        CASE WHEN reqb > 0 AND llc_state <> -2 THEN ready+cpu+l1s+reqb+l2s+membus+dram+l2a+{a.a_llc} END AS o_array_e,
        CASE WHEN membus > 0 THEN ready+cpu+l1s+reqb+l2s END AS o_membus_s,
        CASE WHEN membus > 0 THEN ready+cpu+l1s+reqb+l2s+membus END AS o_membus_e,
        CASE WHEN dram > 0 THEN ready+cpu+l1s+reqb+l2s+membus END AS o_dram_s,
        CASE WHEN dram > 0 THEN ready+cpu+l1s+reqb+l2s+membus+dram END AS o_dram_e,
        CASE WHEN resp > 0 THEN GREATEST(ready+cpu+l1s+reqb+l2s+membus+dram+l2a, ready+cpu+l1s+reqb+l2s+membus+dram+l2a+resp-{a.a_res}) END AS o_respbus_s,
        CASE WHEN resp > 0 THEN ready+cpu+l1s+reqb+l2s+membus+dram+l2a+resp END AS o_respbus_e
      FROM req ORDER BY issue, core, id""")
    con.execute(f"COPY viz TO '{out.replace(os.sep, '/')}' (FORMAT PARQUET, COMPRESSION ZSTD)")
    n, tmin, tmax = con.execute("SELECT count(*), min(issue), max(retire) FROM viz").fetchone()
    print(f"{out}: {n} requests, cycles {tmin}..{tmax}")
    if a.trace:
        import occupancy
        out_dir = os.path.dirname(out)
        st, n, pending = occupancy.stream(a.trace, out_dir, a.t0, a.t1, a.part_rows, a_req=a.a_req, a_res=a.a_res, a_llc=a.a_llc)
        occupancy.report(st, n, pending, occupancy.occ_glob(out_dir), occupancy.fsm_glob(out_dir), out.replace(os.sep, "/"), a.t0, a.t1)


if __name__ == "__main__":
    main()
