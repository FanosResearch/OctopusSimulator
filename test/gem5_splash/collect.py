#!/usr/bin/env python3
"""Turns a run_matrix.py results root into one CSV (one row per run). Standard-library Python.

  collect.py <results root> [-o results.csv]

Columns (cycles are 3 GHz core cycles, 1 cycle = 333 ticks):
  roi_cycles       ROI length: simTicks of the ROI section / 333
  insts            committed instructions in the ROI, all cores
  l1_requests      core requests seen by the L1s in the ROI (I and D). Octopus: LatencyReport rows
                   (every request); Ruby: Sequencer latency samples (requests the Sequencer coalesced
                   with an outstanding one to the same line are not sampled); classic: demand accesses
  l1_misses        primary misses (the count compares across simulators; the rate's
                   denominator does not, see l1_requests)
  l1_miss_rate     primary misses / l1_requests. Octopus: requests that reached the network, the
                   LLC or DRAM; Ruby: Sequencer misses (served outside the L1); classic: MSHR misses
  l1_avg_latency   mean core-request latency in cycles (hits and misses)
  l1_miss_latency  mean latency of the primary misses in cycles
  net_*_roi        network totals over the ROI (Ruby only: Octopus's NetworkStats.csv covers the run)
  net_*_run        network totals over the whole run (Ruby: the three stats sections summed)
  net_avg_latency  mean packet latency in cycles. Garnet: average_packet_network_latency
                   (flitisation at the source NI to the tail at the destination NI, minus one
                   cycle; it includes the injection and ejection links); net_avg_queueing is the
                   wait before flitisation. Octopus: per-class mean weighted by packets, head
                   entering the source router to tail ejection (local links cost nothing), so it
                   reads about 4 cycles lower than Garnet's for the same path
                   (docs/NoC_Garnet_Validation.md s2)
  net_msgs_*        Ruby only: messages delivered to the controllers' network input buffers
                   (SimpleNetwork has no packet or latency statistics of its own)
  noc_hops_per_miss, noc_wait_per_miss  Octopus only, ROI: LatencyReport NoC Hops / NoC Wait per miss
  wall_s, max_rss_mb   host wall time and peak resident set of the gem5 process
"""
import argparse
import csv
import glob
import json
import os
import re

TICKS_PER_CYCLE = 333.0
SECTION = "---------- Begin Simulation Statistics ----------"


def stats_sections(path):
    secs, cur = [], None
    with open(path) as f:
        for line in f:
            if line.startswith(SECTION):
                cur = {}
                secs.append(cur)
                continue
            if cur is None or not line.strip() or line.startswith("-"):
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            try:
                cur[parts[0]] = float(parts[1])
            except ValueError:
                pass
    return secs


def roi_section_index(rundir):
    """se_arm_ruby.py dumps once before the ROI; se_arm.py does not."""
    with open(os.path.join(rundir, "simout.txt"), errors="replace") as f:
        text = f.read()
    if "Dump stats at the end of the ROI" not in text:
        return None
    return 1 if "Dump stats before the ROI" in text else 0


def ssum(sec, pattern):
    rx = re.compile(pattern)
    return sum(v for k, v in sec.items() if rx.fullmatch(k))


def time_info(rundir):
    wall = rss = None
    p = os.path.join(rundir, "time.txt")
    if os.path.exists(p):
        for line in open(p):
            if "Elapsed (wall clock)" in line:
                t = line.rsplit(" ", 1)[1].strip().split(":")
                wall = 0.0
                for x in t:
                    wall = wall * 60 + float(x)
            elif "Maximum resident set size" in line:
                rss = int(line.rsplit(":", 1)[1]) / 1024.0
    return wall, rss


def reduce_octopus_reports(rundir, delete=False):
    """LatencyReport_C*.csv -> octopus_requests.json (ROI: the Logger follows gem5's stats reset
    and dump). A miss is a request with any latency beyond the L1."""
    files = sorted(glob.glob(os.path.join(rundir, "newLogger", "LatencyReport_C*.csv")))
    n = misses = 0
    lat_all = lat_miss = hops = wait = 0
    for fn in files:
        with open(fn) as f:
            for r in csv.DictReader(f):
                if not (r["RequstID"] or "").isdigit():
                    break             # the per-core worst-case summary appended after the requests
                total = int(r["Total Latency"])
                n += 1
                lat_all += total
                beyond = (int(r["Requst Bus Latency"]) or int(r["L2 Access Latency"])
                          or int(r["DRAM latency"]) or int(r["NoC Hops"]) or int(r["L2 Stall Latency"])
                          or int(r["Response Bus Latency"]))
                if beyond:
                    misses += 1
                    lat_miss += total
                    hops += int(r["NoC Hops"])
                    wait += int(r["NoC Wait"])
    out = {"requests": n, "misses": misses,
           "avg_latency": lat_all / n if n else None,
           "miss_latency": lat_miss / misses if misses else None,
           "hops_per_miss": hops / misses if misses else None,
           "wait_per_miss": wait / misses if misses else None}
    with open(os.path.join(rundir, "octopus_requests.json"), "w") as f:
        json.dump(out, f, indent=1)
    if delete:
        for fn in files:
            os.remove(fn)
    return out


def octopus_network(rundir):
    p = os.path.join(rundir, "newLogger", "NetworkStats.csv")
    if not os.path.exists(p):
        return {}
    pk = lat = 0.0
    out = {}
    for row in csv.reader(open(p)):
        if row[0] == "class":
            pk += float(row[4])
            lat += float(row[4]) * float(row[5])
        elif row[0] == "totals" and row[1] in ("flits", "packets"):
            out[f"net_{row[1]}_run"] = float(row[4])
    out["net_avg_latency_run"] = lat / pk if pk else None
    return out


def ruby_network(secs, roi):
    out = {}
    for tag, chosen in (("roi", [secs[roi]]), ("run", secs)):
        key = "board.cache_hierarchy.ruby_system.network."
        pk = sum(s.get(key + "packets_received::total", 0) for s in chosen)
        fl = sum(s.get(key + "flits_received::total", 0) for s in chosen)
        # average_packet_network_latency: flitisation at the source NI (the packet holds an NI VC)
        # to the tail at the destination NI, minus one cycle; *_queueing_latency: waiting in the
        # controller's buffer for an NI VC (docs/NoC_Garnet_Validation.md s3, "Latency")
        lat = sum(s.get(key + "average_packet_network_latency", 0) * s.get(key + "packets_received::total", 0)
                  for s in chosen)
        que = sum(s.get(key + "average_packet_queueing_latency", 0) * s.get(key + "packets_received::total", 0)
                  for s in chosen)
        hops = sum(s.get(key + "average_hops", 0) * s.get(key + "packets_received::total", 0) for s in chosen)
        if pk:
            out[f"net_packets_{tag}"] = pk
            out[f"net_flits_{tag}"] = fl
            out[f"net_avg_latency_{tag}"] = lat / pk / TICKS_PER_CYCLE
            out[f"net_avg_queueing_{tag}"] = que / pk / TICKS_PER_CYCLE
            out[f"net_avg_hops_{tag}"] = hops / pk
        # both Ruby networks: messages delivered into the controllers' network input buffers
        out[f"net_msgs_{tag}"] = sum(ssum(s, r".*ruby_system\.\w+_controllers\d+\.(requestToL1Cache|"
                                         r"responseToL1Cache|unblockToL2Cache|L1RequestToL2Cache|"
                                         r"responseToL2Cache|requestToDir|responseToDir)\.m_msg_count")
                                     for s in chosen)
    return out


def one_run(rundir):
    meta = json.load(open(os.path.join(rundir, "run.json")))
    row = {k: meta[k] for k in ("bench", "size", "threads", "config")}
    rc = open(os.path.join(rundir, "rc.txt")).read().strip() if os.path.exists(os.path.join(rundir, "rc.txt")) else "running"
    row["rc"] = rc
    row["wall_s"], row["max_rss_mb"] = time_info(rundir)
    st = os.path.join(rundir, "stats.txt")
    roi = roi_section_index(rundir) if os.path.exists(os.path.join(rundir, "simout.txt")) else None
    if roi is None or not os.path.exists(st):
        row["status"] = "no ROI dump"
        return row
    secs = stats_sections(st)
    s = secs[roi]
    row["status"] = "ok" if rc == "0" else "rc!=0"
    row["roi_cycles"] = s.get("simTicks", 0) / TICKS_PER_CYCLE
    row["insts"] = s.get("simInsts")
    cfg = meta["config"]
    if cfg in ("garnet", "simple"):
        req = ssum(s, r".*ruby_system\.m_latencyHistSeqr::samples")
        miss = ssum(s, r".*ruby_system\.m_missLatencyHistSeqr::samples")
        row["l1_requests"] = req
        row["l1_miss_rate"] = miss / req if req else None
        row["l1_avg_latency"] = s.get("board.cache_hierarchy.ruby_system.m_latencyHistSeqr::mean")
        row["l1_miss_latency"] = s.get("board.cache_hierarchy.ruby_system.m_missLatencyHistSeqr::mean")
        row.update(ruby_network(secs, roi))
    elif cfg == "classic":
        acc = ssum(s, r".*l1[di]_caches\d+\.demandAccesses::total")
        mm = ssum(s, r".*l1[di]_caches\d+\.demandMshrMisses::total")
        mlat = ssum(s, r".*l1[di]_caches\d+\.demandMshrMissLatency::total")
        row["l1_requests"] = acc
        row["l1_miss_rate"] = mm / acc if acc else None
        row["l1_miss_latency"] = mlat / mm / TICKS_PER_CYCLE if mm else None
    elif cfg == "octopus":
        p = os.path.join(rundir, "octopus_requests.json")
        o = json.load(open(p)) if os.path.exists(p) else reduce_octopus_reports(rundir)
        row["l1_requests"] = o["requests"]
        row["l1_miss_rate"] = o["misses"] / o["requests"] if o["requests"] else None
        row["l1_avg_latency"] = o["avg_latency"]
        row["l1_miss_latency"] = o["miss_latency"]
        row["noc_hops_per_miss"] = o["hops_per_miss"]
        row["noc_wait_per_miss"] = o["wait_per_miss"]
        lat = ssum(s, r".*l1[di]_caches\d+\.latencyCycles")
        resp = ssum(s, r".*l1[di]_caches\d+\.responses")
        row["bridge_avg_latency"] = lat / resp if resp else None
        row.update(octopus_network(rundir))
    if row.get("l1_requests") and row.get("l1_miss_rate") is not None:
        row["l1_misses"] = row["l1_requests"] * row["l1_miss_rate"]
    return row


COLUMNS = ["bench", "size", "threads", "config", "rc", "status", "roi_cycles", "insts",
           "l1_requests", "l1_misses", "l1_miss_rate", "l1_avg_latency", "l1_miss_latency", "bridge_avg_latency",
           "net_packets_roi", "net_flits_roi", "net_avg_latency_roi", "net_avg_queueing_roi",
           "net_avg_hops_roi", "net_msgs_roi",
           "net_packets_run", "net_flits_run", "net_avg_latency_run", "net_avg_queueing_run",
           "net_avg_hops_run", "net_msgs_run",
           "noc_hops_per_miss", "noc_wait_per_miss", "wall_s", "max_rss_mb"]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("root")
    p.add_argument("-o", "--output", default=None, help="CSV path (default <root>/results.csv)")
    a = p.parse_args()
    rows = []
    for d in sorted(glob.glob(os.path.join(a.root, "*", "run.json"))):
        rows.append(one_run(os.path.dirname(d)))
    out = a.output or os.path.join(a.root, "results.csv")
    with open(out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow({k: (f"{v:.4g}" if isinstance(v, float) else v) for k, v in r.items()})
    print(out, len(rows), "runs")


if __name__ == "__main__":
    main()
