#!/usr/bin/env python3
"""live_server.py -- watch an Octopus run while it is going and feed the demo page.

Octopus appends one row to `<run>/newLogger/JobReport_C<n>.csv` the moment a job finishes and
flushes it (docs/Tasks.md), so the whole live path is a tail of one text file. This server
serves demo/ over HTTP and a single JSON endpoint the page polls:

  GET /api/live?since=<job index>   ->  {"running": bool, "period": int, "label": str,
                                         "total": int, "jobs": [ <records after `since`> ],
                                         "sim_cycle": int, "watching": "<dir>"}

Each record has the same shape the page replays from the canned JSONL (release_cycle,
finish_cycle, deadline_cycle, deadline_miss, skipped_periods, n_accesses, exec_cycles,
mean_lat_ns, max_lat_ns) so live and replay use one code path.

`running` is false once the core has written its Summary.csv row (it finished) and the report
has stopped growing; the page then loops the act instead of waiting for more jobs.

Usage:
    bash demo/run_acts.sh act2 &                 # or any Octopus run of a periodic task
    python demo/live_server.py --watch demo/workloads/contention
    # open http://localhost:8770/octopus-tracking-demo.html

Options: --core (default 0), --port (8770), --label (shown on the page), --root (served dir).
"""
import argparse, csv, json, os, time, urllib.parse
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler

WATCH = None; CORE = 0; LABEL = ""; ROOT = None
_cache = {"rows": [], "size": -1, "mtime": 0.0, "last_growth": 0.0}


def report_path():
    return os.path.join(WATCH, "newLogger", f"JobReport_C{CORE}.csv")


def period_from_task():
    """attr,period from the core's task program; the page needs it before any job arrives."""
    p = os.path.join(WATCH, f"task_C{CORE}.task.csv")
    try:
        for line in open(p, encoding="utf-8"):
            c = [x.strip() for x in line.split(",")]
            if len(c) > 2 and c[0] == "attr" and c[1] == "period":
                return int(c[2], 0)
    except OSError:
        pass
    return 0


def read_rows():
    """Re-read the report when it has grown (cheap: these files are a few hundred rows)."""
    p = report_path()
    try:
        st = os.stat(p)
    except OSError:
        return []
    if st.st_size == _cache["size"] and st.st_mtime == _cache["mtime"]:
        return _cache["rows"]
    if st.st_size != _cache["size"]:
        _cache["last_growth"] = time.time()
    rows = []
    try:
        with open(p, newline="", encoding="utf-8") as f:
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
        return _cache["rows"]
    _cache.update(rows=rows, size=st.st_size, mtime=st.st_mtime)
    return rows


def finished():
    """The core wrote its Summary.csv row (it ran out of jobs) and nothing has been appended since."""
    if not _cache["rows"]:
        return False                       # nothing reported yet: the run has not got going
    s = os.path.join(WATCH, "newLogger", "Summary.csv")
    if not os.path.exists(s):
        return False
    return time.time() - _cache["last_growth"] > 1.0


def live(q):
    since = int(q.get("since", -1))
    rows = read_rows()
    new = [r for r in rows if r["job"] > since]
    return {"running": not finished(), "period": period_from_task(), "label": LABEL or os.path.basename(os.path.abspath(WATCH)),
            "watching": os.path.abspath(WATCH), "total": len(rows), "jobs": new,
            "sim_cycle": rows[-1]["finish_cycle"] if rows else 0}


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
    ap.add_argument("--watch", required=True, help="workload dir of the running Octopus simulation")
    ap.add_argument("--core", type=int, default=0); ap.add_argument("--port", type=int, default=8770)
    ap.add_argument("--label", default=""); ap.add_argument("--root", default=os.path.dirname(os.path.abspath(__file__)))
    a = ap.parse_args()
    WATCH, CORE, LABEL, ROOT = a.watch, a.core, a.label, a.root
    _cache["last_growth"] = time.time()
    print(f"octopus live: watching {report_path()} (period {period_from_task()} cycles)")
    print(f"  ->  http://localhost:{a.port}/octopus-tracking-demo.html")
    ThreadingHTTPServer(("127.0.0.1", a.port), H).serve_forever()
