#!/usr/bin/env python3
"""plot_trajectory.py -- the paper-style figure of the demo (cf. Bechtel & Yun, TC 2024, Fig. 8):
(a) the robot's actual trajectory in the X-Y plane for each act, over the planned (ground-truth)
path; (b) x and y position over time; (c) position error over time; plus the absolute trajectory
error (ATE: RMSE and median of the position error) per act, printed and written to ate.csv.

The trajectory is a pure function of the Octopus job timings (demo/data/act<n>.jsonl): the
localization job senses the pose at its release and the robot steers toward the goal point of
the last finished job's estimate at a speed limit -- the same kinematics the demo page animates.

Usage: python demo/plot_trajectory.py [--data demo/data] [--out demo/figures] [--path fig8]
                                      [--lap-periods 200] [--vgain 1.55] [--cm 600]
"""
import argparse, json, math, os, csv

def make_path(name):
    tp = 2 * math.pi
    return {"fig8":   lambda u: (math.sin(u*tp), math.sin(2*u*tp)*0.56),
            "circle": lambda u: (math.cos(u*tp), math.sin(u*tp)),
            "square": lambda u: (math.copysign(abs(math.cos(u*tp))**0.4, math.cos(u*tp)), math.copysign(abs(math.sin(u*tp))**0.4, math.sin(u*tp))),
            "rose":   lambda u: ((0.64+0.36*math.cos(5*u*tp))*math.cos(u*tp), (0.64+0.36*math.cos(5*u*tp))*math.sin(u*tp))}[name]

class Lut:
    def __init__(self, f, n=3000):
        self.pts = [f(i/n) for i in range(n+1)]; self.cum = [0.0]
        for i in range(1, n+1):
            a, b = self.pts[i-1], self.pts[i]; self.cum.append(self.cum[-1] + math.hypot(b[0]-a[0], b[1]-a[1]))
        self.total = self.cum[-1]; self.n = n
    def at(self, s):
        s = s % self.total; lo, hi = 0, self.n
        while lo < hi-1:
            m = (lo+hi)//2
            if self.cum[m] <= s: lo = m
            else: hi = m
        seg = self.cum[hi]-self.cum[lo] or 1e-9; f = (s-self.cum[lo])/seg
        return (self.pts[lo][0]+(self.pts[hi][0]-self.pts[lo][0])*f, self.pts[lo][1]+(self.pts[hi][1]-self.pts[lo][1])*f)

def simulate(recs, lut, T, lap_periods, vgain, cm, dt=5.0):
    """Replay one act: returns lists t, x, y, err(cm), and the end time."""
    lap = T * lap_periods; vmax = vgain * lut.total / lap
    t_end = max(r["deadline_cycle"] for r in recs) if recs else lap
    t_end = max(t_end, recs[-1]["finish_cycle"]) if recs else lap
    jobs = sorted(recs, key=lambda r: r["release_cycle"]); k = 0
    eff = list(lut.at(0)); tgt = lut.at(0); t = 0.0
    T_, X, Y, E = [], [], [], []
    while t <= t_end:
        while k < len(jobs) and t >= jobs[k]["finish_cycle"]:
            tgt = lut.at(((jobs[k]["release_cycle"] + T)/lap) * lut.total); k += 1   # pose sensed at release, plans the waypoint for release + T, applied at finish
        dx, dy = tgt[0]-eff[0], tgt[1]-eff[1]; d = math.hypot(dx, dy); s = vmax*dt
        if d <= s: eff = [tgt[0], tgt[1]]
        else: eff = [eff[0]+dx/d*s, eff[1]+dy/d*s]
        ref = lut.at((t/lap) * lut.total)
        T_.append(t); X.append(eff[0]); Y.append(eff[1]); E.append(math.hypot(eff[0]-ref[0], eff[1]-ref[1]) * cm)
        t += dt
    return T_, X, Y, E

def main():
    ap = argparse.ArgumentParser()
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--data", default=os.path.join(here, "data")); ap.add_argument("--out", default=os.path.join(here, "figures"))
    ap.add_argument("--path", default="fig8"); ap.add_argument("--lap-periods", type=int, default=200)
    ap.add_argument("--vgain", type=float, default=1.55); ap.add_argument("--cm", type=float, default=600)
    a = ap.parse_args()
    import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
    os.makedirs(a.out, exist_ok=True)
    acts = [("act1", "Alone (FCFS)", "#1B8A5F", 3.2, .45), ("act2", "Contention (FCFS)", "#BE3F24", 1.4, .95), ("act3", "Protected (RR + reserved way)", "#6244C7", 1.2, 1.0)]
    data = {}; T = 5000
    for act, *_ in acts:
        with open(os.path.join(a.data, act + ".jsonl"), encoding="utf-8") as f: data[act] = [json.loads(l) for l in f if l.strip()]
        mp = os.path.join(a.data, act + ".manifest.json")
        if os.path.exists(mp):
            m = json.load(open(mp, encoding="utf-8")); T = m.get("period_cycles") or T
    lut = Lut(make_path(a.path)); lap = T * a.lap_periods
    fig = plt.figure(figsize=(12, 7.2)); gs = fig.add_gridspec(3, 2, width_ratios=[1.05, 1], hspace=0.45, wspace=0.28)
    axy = fig.add_subplot(gs[:, 0]); ax_x = fig.add_subplot(gs[0, 1]); ax_y = fig.add_subplot(gs[1, 1], sharex=ax_x); ax_e = fig.add_subplot(gs[2, 1], sharex=ax_x)
    gt = [lut.at(i/600*lut.total) for i in range(601)]
    axy.plot([p[0]*a.cm/100 for p in gt], [p[1]*a.cm/100 for p in gt], "--", color="#555", lw=1.2, label="Ground truth (planned path)")
    tt = [i/600*lap for i in range(601)]
    ax_x.plot([t/1e3 for t in tt], [p[0]*a.cm/100 for p in gt], "--", color="#555", lw=1, label="Ground")
    ax_y.plot([t/1e3 for t in tt], [p[1]*a.cm/100 for p in gt], "--", color="#555", lw=1)
    rows = []
    for act, label, col, lw, al in acts:
        t, x, y, e = simulate(data[act], lut, T, a.lap_periods, a.vgain, a.cm)
        axy.plot([v*a.cm/100 for v in x], [v*a.cm/100 for v in y], color=col, lw=lw, label=label, alpha=al)
        ax_x.plot([v/1e3 for v in t], [v*a.cm/100 for v in x], color=col, lw=lw*0.7, label=label, alpha=al)
        ax_y.plot([v/1e3 for v in t], [v*a.cm/100 for v in y], color=col, lw=lw*0.7, alpha=al)
        ax_e.plot([v/1e3 for v in t], [v/100 for v in e], color=col, lw=lw*0.7, alpha=al)
        es = sorted(e); rmse = math.sqrt(sum(v*v for v in e)/len(e))
        m = json.load(open(os.path.join(a.data, act + ".manifest.json"), encoding="utf-8")) if os.path.exists(os.path.join(a.data, act + ".manifest.json")) else {}
        rows.append([act, label, len(data[act]), m.get("deadline_misses", ""), m.get("exec_mean", ""), m.get("exec_max", ""), round(rmse/100, 3), round(es[len(es)//2]/100, 3), round(es[-1]/100, 3)])
    s0 = lut.at(0); axy.plot([s0[0]*a.cm/100], [s0[1]*a.cm/100], "ks", ms=6); axy.annotate("start / goal", (s0[0]*a.cm/100, s0[1]*a.cm/100), xytext=(6, 6), textcoords="offset points", fontsize=8)
    axy.set_xlabel("x (m)"); axy.set_ylabel("y (m)"); axy.set_aspect("equal"); axy.grid(alpha=.3); axy.legend(fontsize=8, loc="lower right")
    axy.set_title("(a) Trajectory in the X-Y plane", fontsize=10)
    ax_x.set_ylabel("x (m)"); ax_y.set_ylabel("y (m)"); ax_e.set_ylabel("position error (m)"); ax_e.set_xlabel("time (thousand cycles)")
    ax_x.set_title("(b) X, Y position and error over time", fontsize=10); ax_x.legend(fontsize=7, ncol=2)
    for ax in (ax_x, ax_y, ax_e): ax.grid(alpha=.3)
    fig.suptitle(f"Robot trajectory vs. the localization task's timing under shared-memory contention (Octopus, period {T} cycles, {len(data['act1'])} periods)", fontsize=10)
    for ext in ("pdf", "png"): fig.savefig(os.path.join(a.out, f"trajectory.{ext}"), dpi=160, bbox_inches="tight")
    with open(os.path.join(a.data, "ate.csv"), "w", newline="\n", encoding="utf-8") as f:
        w = csv.writer(f); w.writerow(["act", "label", "jobs", "deadline_misses", "exec_mean_cycles", "exec_max_cycles", "ate_rmse_m", "ate_median_m", "ate_max_m"]); w.writerows(rows)
    print(f"{'act':5s} {'label':32s} {'jobs':>5s} {'misses':>6s} {'exec_mean':>9s} {'exec_max':>8s} {'ATE rmse':>8s} {'median':>7s} {'max':>6s}")
    for r in rows: print(f"{r[0]:5s} {r[1]:32s} {r[2]:5d} {str(r[3]):>6s} {str(r[4]):>9s} {str(r[5]):>8s} {r[6]:8.3f} {r[7]:7.3f} {r[8]:6.3f}")
    print("->", os.path.join(a.out, "trajectory.pdf"), "and", os.path.join(a.data, "ate.csv"))

if __name__ == "__main__":
    main()
