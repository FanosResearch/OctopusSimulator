#!/usr/bin/env python3
"""live_server.py -- watch Octopus runs while they are going and feed the demo page.

Octopus appends one row to `<run>/newLogger/JobReport_C<n>.csv` the moment a job finishes and
flushes it (docs/Tasks.md), so the whole live path is a tail of one text file per run. This
server serves demo/ over HTTP and one JSON endpoint the page polls:

  GET /api/live?act=<name>&since=<job index>
      -> {"act": .., "running": bool, "period": int, "label": str, "total": int,
          "sim_cycle": int, "jobs": [ <records after `since`> ], "acts": [names]}

  GET /api/live?since=act1:12,act2:8,act3:0        (compare view: every watched act at once)
      -> {"acts": [names], "all": {act: {running, period, label, total, sim_cycle, jobs}}}

Each record has the same shape the page replays from the canned JSONL (release_cycle,
finish_cycle, deadline_cycle, deadline_miss, skipped_periods, n_accesses, exec_cycles,
mean_lat_ns, max_lat_ns) so live and replay use one code path.

`running` is false once the core has written its Summary.csv row (it finished) and the report
has stopped growing; the page then loops that act instead of waiting for more jobs.

Usage:
    bash demo/live.sh compare                       # starts the runs and this server
    # or by hand, against runs started elsewhere:
    python demo/live_server.py --watch act2=demo/workloads/act2
    python demo/live_server.py --watch act1=... --watch act2=... --watch act3=...

Options: --core (default 0), --port (8770), --root (served directory).
"""
import argparse, csv, json, os, time, urllib.parse
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler

CORE = 0; ROOT = None
WATCH = {}      # act -> {"dir":, "label":, "rows": [], "size": -1, "mtime": 0.0, "last_growth": t}

LABELS = {"act1": "act 1 - task alone, FCFS bus",
          "act2": "act 2 - 3 streaming cores, FCFS bus",
          "act3": "act 3 - RR bus + LLC way reserved for core 0"}


def report_path(w):
    return os.path.join(w["dir"], "newLogger", f"JobReport_C{CORE}.csv")


def period_from_task(w):
    """attr,period from the core's task program; the page needs it before any job arrives."""
    try:
        for line in open(os.path.join(w["dir"], f"task_C{CORE}.task.csv"), encoding="utf-8"):
            c = [x.strip() for x in line.split(",")]
            if len(c) > 2 and c[0] == "attr" and c[1] == "period":
                return int(c[2], 0)
    except OSError:
        pass
    return 0


def read_rows(w):
    """Re-read the report when it has grown (cheap: these files are a few hundred rows)."""
    try:
        st = os.stat(report_path(w))
    except OSError:
        return w["rows"]
    if st.st_size == w["size"] and st.st_mtime == w["mtime"]:
        return w["rows"]
    if st.st_size != w["size"]:
        w["last_growth"] = time.time()
    rows = []
    try:
        with open(report_path(w), newline="", encoding="utf-8") as f:
            for r in csv.DictReader(f):
                try:
                    rel, fin = int(r["release_cycle"]), int(r["finish_cycle"])
                    rows.append({"job": int(r["job"]), "release_cycle": rel, "finish_cycle": fin,
                                 "deadline_cycle": int(r["deadline_cycle"]),
                                 "deadline_miss": r["deadline_miss"] == "1",
                                 "skipped_periods": int(r["skipped_periods"]),
                                 "n_accesses": int(r["n_accesses"]), "exec_cycles": fin - rel,
                                 "mean_lat_ns": float(r.get("mean_access_lat", 0) or 0),
                                 "max_lat_ns": float(r.get("max_access_lat", 0) or 0)})
                except (KeyError, ValueError):
                    continue        # a row half-written while we read it: it will be there next poll
    except OSError:
        return w["rows"]
    w.update(rows=rows, size=st.st_size, mtime=st.st_mtime)
    return rows


def finished(w):
    """The core wrote its Summary.csv row (it ran out of jobs) and nothing has been appended since."""
    if not w["rows"]:
        return False                       # nothing reported yet: the run has not got going
    if not os.path.exists(os.path.join(w["dir"], "newLogger", "Summary.csv")):
        return False
    return time.time() - w["last_growth"] > 1.0


def act_state(act, since):
    w = WATCH[act]
    rows = read_rows(w)
    return {"act": act, "running": not finished(w), "period": period_from_task(w),
            "label": w["label"], "total": len(rows), "watching": os.path.abspath(w["dir"]),
            "sim_cycle": rows[-1]["finish_cycle"] if rows else 0,
            "jobs": [r for r in rows if r["job"] > since]}


def live(q):
    acts = sorted(WATCH)
    since = q.get("since", "-1")
    if "act" in q:                                   # one act
        a = q["act"]
        if a not in WATCH:
            return {"error": f"not watching {a}", "acts": acts}
        st = act_state(a, int(since)); st["acts"] = acts
        return st
    if ":" in since or not acts:                     # compare view: "act1:12,act2:8,..."
        cur = {}
        for part in since.split(","):
            if ":" in part:
                k, _, v = part.partition(":")
                try: cur[k] = int(v)
                except ValueError: pass
        return {"acts": acts, "all": {a: act_state(a, cur.get(a, -1)) for a in acts}}
    st = act_state(acts[0], int(since)); st["acts"] = acts   # single watched act, no name given
    return st


class H(SimpleHTTPRequestHandler):
    def __init__(self, *a, **k): super().__init__(*a, directory=ROOT, **k)
    def log_message(self, *a): pass
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()
    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        if not u.path.startswith("/api/"):
            return super().do_GET()
        q = {k: v[0] for k, v in urllib.parse.parse_qs(u.query).items()}
        try:
            body = live(q) if u.path == "/api/live" else None
            if body is None: self.send_error(404); return
            data = json.dumps(body).encode()
            self.send_response(200); self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)
        except Exception as e:
            data = json.dumps({"error": str(e)}).encode()
            self.send_response(500); self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--watch", action="append", required=True, metavar="ACT=DIR",
                    help="a running simulation to follow, e.g. act2=demo/workloads/act2 (repeatable)")
    ap.add_argument("--core", type=int, default=0); ap.add_argument("--port", type=int, default=8770)
    ap.add_argument("--root", default=os.path.dirname(os.path.abspath(__file__)))
    a = ap.parse_args()
    CORE, ROOT = a.core, a.root
    for spec in a.watch:
        act, _, d = spec.partition("=")
        if not d: act, d = os.path.basename(os.path.abspath(spec)), spec
        WATCH[act] = {"dir": d, "label": LABELS.get(act, act), "rows": [], "size": -1,
                      "mtime": 0.0, "last_growth": time.time()}
        print(f"octopus live: {act} -> {report_path(WATCH[act])} (period {period_from_task(WATCH[act])} cycles)")
    print(f"  ->  http://localhost:{a.port}/octopus-tracking-demo.html")
    ThreadingHTTPServer(("127.0.0.1", a.port), H).serve_forever()
