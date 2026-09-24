#!/usr/bin/env python3
"""extract_jobs.py -- per-job records for the demo page from an Octopus run of a periodic task
(docs/Tasks.md): joins JobReport_C<n>.csv (job boundaries from the simulator) with
LatencyReport_C<n>.csv (per-access stages) and writes one JSON line per job:

  {"job": j, "release_cycle": .., "finish_cycle": .., "deadline_cycle": .., "deadline_miss": bool,
   "skipped_periods": k, "n_accesses": n, "exec_cycles": finish - release,
   "mean_lat_ns": .., "max_lat_ns": .., "llc_misses": <accesses with DRAM latency > 0>,
   "l1_hits": .., "mean_dram_ns": ..}

plus a manifest with the run's summary (period, jobs, misses, mean/max exec, worst access).
CYCLE_NS scales simulator cycles to nanoseconds for the readouts (relative numbers are what matter).

Usage: python demo/extract_jobs.py <run_dir>/newLogger [--core 0] [--cycle-ns 1.0] [-o act.jsonl]
"""
import argparse, csv, json, os, statistics

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("logdir"); ap.add_argument("--core", type=int, default=0)
    ap.add_argument("--cycle-ns", type=float, default=1.0); ap.add_argument("-o", "--out")
    ap.add_argument("--label", default="")
    a = ap.parse_args()
    jobs = list(csv.DictReader(open(os.path.join(a.logdir, f"JobReport_C{a.core}.csv"), newline="")))
    acc = []
    with open(os.path.join(a.logdir, f"LatencyReport_C{a.core}.csv"), newline="") as f:
        r = csv.reader(f); hdr = next(r)
        i_ready, i_tot, i_dram, i_state = hdr.index("Ready Cycle"), hdr.index("Total Latency"), hdr.index("DRAM latency"), hdr.index("LLC Arrival State")
        for row in r:
            if len(row) <= i_state or not row[i_tot].isdigit(): continue
            acc.append((int(row[i_ready]), int(row[i_tot]), int(row[i_dram]), int(row[i_state])))
    acc.sort()
    out = a.out or os.path.join(a.logdir, f"jobs_C{a.core}.jsonl")
    k = 0; recs = []
    for j in jobs:
        rel, fin = int(j["release_cycle"]), int(j["finish_cycle"])
        while k < len(acc) and acc[k][0] < rel: k += 1
        rows = []
        while k < len(acc) and acc[k][0] <= fin: rows.append(acc[k]); k += 1
        tot = [x[1] for x in rows] or [0]; dram = [x[2] for x in rows if x[2] > 0]
        recs.append({"job": int(j["job"]), "release_cycle": rel, "finish_cycle": fin, "deadline_cycle": int(j["deadline_cycle"]),
                     "deadline_miss": j["deadline_miss"] == "1", "skipped_periods": int(j["skipped_periods"]),
                     "n_accesses": len(rows), "exec_cycles": fin - rel,
                     "mean_lat_ns": round(statistics.fmean(tot) * a.cycle_ns, 2), "max_lat_ns": round(max(tot) * a.cycle_ns, 2),
                     "llc_misses": len(dram), "l1_hits": sum(1 for x in rows if x[3] == -2),
                     "mean_dram_ns": round(statistics.fmean(dram) * a.cycle_ns, 2) if dram else 0.0})
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        for rec in recs: f.write(json.dumps(rec) + "\n")
    ex = [r["exec_cycles"] for r in recs]
    man = {"label": a.label, "core": a.core, "cycle_ns": a.cycle_ns, "jobs": len(recs),
           "period_cycles": (int(jobs[1]["deadline_cycle"]) - int(jobs[0]["deadline_cycle"])) if len(jobs) > 1 else None,
           "deadline_misses": sum(r["deadline_miss"] for r in recs) + sum(r["skipped_periods"] for r in recs),
           "exec_mean": round(statistics.fmean(ex), 1), "exec_max": max(ex), "exec_p99": sorted(ex)[int(0.99 * (len(ex) - 1))],
           "access_mean_ns": round(statistics.fmean(r["mean_lat_ns"] for r in recs), 2), "access_max_ns": max(r["max_lat_ns"] for r in recs),
           "llc_miss_share": round(sum(r["llc_misses"] for r in recs) / max(1, sum(r["n_accesses"] for r in recs)), 3)}
    with open(out.replace(".jsonl", ".manifest.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(man, f, indent=1)
    print(json.dumps(man))

if __name__ == "__main__":
    main()
