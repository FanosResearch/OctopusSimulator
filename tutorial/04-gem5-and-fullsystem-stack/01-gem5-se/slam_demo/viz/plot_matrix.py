#!/usr/bin/env python3
"""plot_matrix.py -- paper-style figures for a set of slam_demo gem5 runs.

Each run directory must hold a simout.txt from slam_demo --dump (SCAN and KF
lines after the report); run_matrix.sh writes them to ../../runs/<name>. Figures, after Bechtel & Yun 2024:

  1_trajectories   estimated paths against the ground truth (their Fig. 3/6)
  2_error_boxplot  per-scan position error per configuration (their Fig. 2)
  3_error_over_time  position error per scan, dropped and lost scans marked
  4_exec_times     front-end per scan and mapper per keyframe (their Fig. 4)
  5_memory_breakdown  where the SLAM cores' memory time goes, per stage, from
                   Octopus's own reports (runs made with LOG=1, reduced by
                   reduce_reports.sh into breakdown.csv)

usage: python plot_matrix.py [--runs-dir DIR] [--out DIR] [--period-us US]
       (needs numpy and matplotlib; the walls come from steps.json, made by
       `make steps` in ../)
"""

import argparse
import json
import math
import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))

# (run directory, label, colour, group)
RUNS = [
    ("A_solo", "Solo", "#17202b", "both"),
    ("L_fcfs", "Light aggressor", "#d97a12", "light"),
    ("L_rr", "Light + RR", "#11807e", "light"),
    ("H_fcfs", "Heavy aggressor", "#c73e5a", "heavy"),
    ("H_rr", "Heavy + RR", "#8e44ad", "heavy"),
    ("H_part", "Heavy + partition", "#2f7ed8", "heavy"),
    ("H_rr_part", "Heavy + RR + partition", "#5a9e2f", "heavy"),
]


def load_run(path):
    scans, kfs = [], []
    with open(os.path.join(path, "simout.txt")) as f:
        for line in f:
            if line.startswith("SCAN,"):
                v = line.strip().split(",")[1:]
                scans.append(dict(k=int(v[0]), tracked=int(v[1]), lost=int(v[2]),
                                  gt=(float(v[3]), float(v[4])),
                                  est=(float(v[5]), float(v[6])),
                                  err=float(v[7]), fe=float(v[8])))
            elif line.startswith("KF,"):
                v = line.strip().split(",")[1:]
                kfs.append(dict(j=int(v[0]), scan=int(v[1]), map_us=float(v[2])))
    if not scans:
        raise SystemExit(f"{path}: no SCAN lines (run slam_demo with --dump)")
    return scans, kfs


def summary(scans):
    err = np.array([s["err"] for s in scans[1:]])
    return dict(rmse=float(np.sqrt(np.mean(err ** 2))), median=float(np.median(err)),
                dropped=sum(1 for s in scans[1:] if not s["tracked"]),
                lost=sum(s["lost"] for s in scans[1:]))


def draw_walls(ax, walls):
    from matplotlib.collections import LineCollection
    ax.add_collection(LineCollection([((a, b), (c, d)) for a, b, c, d in walls],
                                     colors="#9aa5b1", linewidths=0.8))


def fig_trajectories(data, walls, out):
    fig, axes = plt.subplots(1, 2, figsize=(15, 7.2))
    gt = np.array([s["gt"] for s in data["A_solo"][0]])
    cx, cy = gt[:, 0].mean(), gt[:, 1].mean()
    for ax, group, title in ((axes[0], "light", "(a) light aggressor: bus/LLC queueing"),
                             (axes[1], "heavy", "(b) heavy aggressor: LLC capacity")):
        draw_walls(ax, walls)
        ax.plot(gt[:, 0], gt[:, 1], "--", color="black", lw=2, label="ground truth", zorder=5)
        for name, label, col, g in RUNS:
            if g not in (group, "both") or name not in data:
                continue
            scans = data[name][0]
            est = np.array([s["est"] for s in scans])
            sm = summary(scans)
            ax.plot(est[:, 0], est[:, 1], "-", color=col, lw=1.8, zorder=6,
                    label=f"{label}  (RMSE {sm['rmse']:.2f} m, {sm['dropped']} dropped, {sm['lost']} lost)")
            d = [s["est"] for s in scans if not s["tracked"]]
            lo = [s["est"] for s in scans if s["lost"]]
            if d:
                ax.plot(*zip(*d), "x", color=col, ms=8, mew=2, zorder=7)
            if lo:
                ax.plot(*zip(*lo), "o", mfc="none", mec=col, ms=7, mew=1.5, zorder=7)
        ax.set_xlim(cx - 6, cx + 6)
        ax.set_ylim(cy - 6, cy + 6)
        ax.set_aspect("equal")
        ax.set_xlabel("x (m)")
        ax.set_ylabel("y (m)")
        ax.set_title(title)
        ax.legend(loc="lower left", fontsize=8.5, framealpha=0.9)
    fig.suptitle("Estimated trajectories vs ground truth  (x: scan dropped, o: tracking lost; "
                 "paths that leave the view continue off-plot)", fontsize=12)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "1_trajectories.png"), dpi=140)
    plt.close(fig)


def fig_error_boxplot(data, out):
    fig, ax = plt.subplots(figsize=(12, 6))
    names = [r for r in RUNS if r[0] in data]
    errs = [[s["err"] for s in data[n][0][1:]] for n, *_ in names]
    bp = ax.boxplot(errs, patch_artist=True, widths=0.6, showfliers=True,
                    medianprops=dict(color="black"))
    for patch, (_, _, col, _) in zip(bp["boxes"], names):
        patch.set_facecolor(col)
        patch.set_alpha(0.55)
    ax.set_yscale("log")
    ax.set_ylabel("position error per scan (m, log scale)")
    ax.set_xticks(range(1, len(names) + 1))
    ax.set_xticklabels([lab for _, lab, _, _ in names], rotation=15)
    top = max(max(e) for e in errs) * 1.8
    for i, (n, *_) in enumerate(names, start=1):
        sm = summary(data[n][0])
        ax.text(i, top, f"RMSE {sm['rmse']:.2f} m\n{sm['dropped']} dropped\n{sm['lost']} lost",
                ha="center", va="bottom", fontsize=8.5)
    ax.set_ylim(top=top * 4)
    ax.grid(axis="y", alpha=0.3, which="both")
    ax.set_title("Position error per scan, per configuration (cf. the paper's Fig. 2)")
    fig.tight_layout()
    fig.savefig(os.path.join(out, "2_error_boxplot.png"), dpi=140)
    plt.close(fig)


def fig_error_over_time(data, out):
    fig, axes = plt.subplots(2, 1, figsize=(13, 8), sharex=True)
    for ax, group, title in ((axes[0], "light", "light aggressor"), (axes[1], "heavy", "heavy aggressor")):
        for name, label, col, g in RUNS:
            if g not in (group, "both") or name not in data:
                continue
            scans = data[name][0]
            k = [s["k"] for s in scans[1:]]
            e = [max(s["err"], 1e-3) for s in scans[1:]]
            ax.semilogy(k, e, "-", color=col, lw=1.6, label=label)
            for s in scans[1:]:
                if not s["tracked"]:
                    ax.plot(s["k"], max(s["err"], 1e-3), "x", color=col, ms=8, mew=2)
                elif s["lost"]:
                    ax.plot(s["k"], max(s["err"], 1e-3), "o", mfc="none", mec=col, ms=6)
        ax.set_ylabel("position error (m)")
        ax.set_title(title)
        ax.grid(alpha=0.3, which="both")
        ax.legend(fontsize=8.5, loc="upper left")
    axes[1].set_xlabel("scan")
    fig.suptitle("Position error over the run  (x: dropped, o: tracking lost)", fontsize=12)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "3_error_over_time.png"), dpi=140)
    plt.close(fig)


def fig_exec_times(data, out, period_us, kf_every):
    fig, axes = plt.subplots(1, 2, figsize=(15, 6))
    names = [r for r in RUNS if r[0] in data]
    fe = [[s["fe"] for s in data[n][0][1:] if s["tracked"]] for n, *_ in names]
    mp = [[k["map_us"] for k in data[n][1]] for n, *_ in names]
    for ax, vals, title, budget, blabel in (
            (axes[0], fe, "(a) front-end, per scan", period_us, f"period {period_us:g} us"),
            (axes[1], mp, "(b) mapper, per keyframe", period_us * kf_every,
             f"{kf_every} periods = {period_us * kf_every:g} us")):
        bp = ax.boxplot(vals, patch_artist=True, widths=0.6, medianprops=dict(color="black"))
        for patch, (_, _, col, _) in zip(bp["boxes"], names):
            patch.set_facecolor(col)
            patch.set_alpha(0.55)
        ax.axhline(budget, color="#c73e5a", ls="--", lw=1.2, label=blabel)
        if vals is mp:   # keyframes the mapper finished before the end, of those queued
            for i, (n, *_) in enumerate(names, start=1):
                queued = sum(1 for s in data[n][0][1:] if s["tracked"]) // kf_every
                ax.annotate(f"{len(vals[i - 1])} of ~{queued}\nkeyframes", (i, 0), xycoords=("data", "axes fraction"),
                            xytext=(0, 4), textcoords="offset points", ha="center", va="bottom", fontsize=7.5)
        ax.set_yscale("log")
        ax.set_ylabel("execution time (us, log scale)")
        ax.set_xticks(range(1, len(names) + 1))
        ax.set_xticklabels([lab for _, lab, _, _ in names], rotation=20, fontsize=8.5)
        ax.set_title(title)
        ax.grid(axis="y", alpha=0.3, which="both")
        ax.legend(fontsize=9)
    fig.suptitle("Execution-time distributions of the SLAM threads (cf. the paper's Fig. 4)", fontsize=12)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "4_exec_times.png"), dpi=140)
    plt.close(fig)


STAGES = [("mean_l1_stall", "L1 stall"), ("mean_req_bus", "request bus"),
          ("mean_l2_stall", "LLC queue (L2 stall)"), ("mean_l2_access", "LLC array"),
          ("mean_resp_bus", "response bus"), ("mean_l2_dram_bus", "LLC-DRAM bus"),
          ("mean_dram", "DRAM"), ("mean_l1_access", "L1 access")]
STAGE_COL = ["#b4bec9", "#d97a12", "#c73e5a", "#8e44ad", "#f0a03c", "#5a9e2f", "#17202b", "#2f7ed8"]


def load_breakdown(path):
    f = os.path.join(path, "breakdown.csv")
    if not os.path.isfile(f):
        return None
    rows = {}
    with open(f) as fh:
        head = fh.readline().strip().split(",")
        for line in fh:
            v = dict(zip(head, line.strip().split(",")))
            rows[int(v["core"])] = {k: float(x) for k, x in v.items()}
    return rows


def fig_memory_breakdown(breakdowns, out):
    """Stacked mean latency per stage of the requests that left the L1, for
    the front-end and the mapper core; one row per aggressor, each with its
    own scale (the heavy aggressor's DRAM waits would flatten the rest)."""
    if "A_solo" not in breakdowns:
        return False

    def core_of(b, role):
        # the aggressor thread is created first: with one, the mapper runs on
        # core 2 and the front-end on core 3, otherwise on cores 1 and 2
        if 3 in b:
            return 3 if role == "front-end" else 2
        return 2 if role == "front-end" else 1

    groups = [("light aggressor: bus and LLC queueing", "light"),
              ("heavy aggressor: LLC capacity", "heavy")]
    fig, axes = plt.subplots(2, 2, figsize=(15, 10))
    for row, (gtitle, g) in enumerate(groups):
        names = [r for r in RUNS if r[0] in breakdowns and r[3] in (g, "both")]
        for col, role in enumerate(("front-end", "mapper")):
            ax = axes[row][col]
            x = np.arange(len(names))
            bottom = np.zeros(len(names))
            for (key, lab), color in zip(STAGES, STAGE_COL):
                vals = [breakdowns[n].get(core_of(breakdowns[n], role), {}).get(key, 0.0)
                        for n, *_ in names]
                ax.bar(x, vals, bottom=bottom, color=color, label=lab, width=0.6)
                bottom += np.array(vals)
            for i, (n, *_) in enumerate(names):
                r = breakdowns[n].get(core_of(breakdowns[n], role), {})
                if r:
                    ax.text(i, bottom[i], f"{bottom[i]:.0f} cy\n{r['beyond_l1'] / 1000:.1f}k left L1\n"
                            f"{int(r['dram'])} to DRAM", ha="center", va="bottom", fontsize=7.5)
            ax.set_ylim(0, max(bottom) * 1.3)
            ax.set_xticks(x)
            ax.set_xticklabels([lab for _, lab, _, _ in names], rotation=12, fontsize=8.5)
            ax.set_title(f"{role} core, {gtitle}", fontsize=10.5)
            ax.set_ylabel("mean cycles per request that left the L1")
            ax.grid(axis="y", alpha=0.3)
    axes[0][0].legend(fontsize=8, loc="upper right")
    fig.suptitle("Where the SLAM cores' memory time goes  (Octopus LatencyReport stages, "
                 "requests that left the L1)", fontsize=12)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "5_memory_breakdown.png"), dpi=140)
    plt.close(fig)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--runs-dir", default=os.path.join(HERE, "..", "..", "runs"),
                    help="directory holding one run directory per configuration (run_matrix.sh)")
    ap.add_argument("--steps", default=os.path.join(HERE, "steps.json"),
                    help="walls of the scenario (make steps)")
    ap.add_argument("--out", default=os.path.join(HERE, "..", "..", "figures"))
    ap.add_argument("--period-us", type=float, default=80)
    ap.add_argument("--kf-every", type=int, default=2)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    data = {}
    for name, *_ in RUNS:
        p = os.path.join(a.runs_dir, name)
        if os.path.isfile(os.path.join(p, "simout.txt")):
            data[name] = load_run(p)
    if "A_solo" not in data:
        raise SystemExit(f"the Solo run (A_solo) is required in {a.runs_dir}")
    with open(a.steps) as f:
        walls = json.load(f)["walls"]
    for name, label, *_ in RUNS:
        if name in data:
            sm = summary(data[name][0])
            print(f"{label:24s} RMSE {sm['rmse']:7.3f} m  median {sm['median']:7.3f} m  "
                  f"dropped {sm['dropped']:2d}  lost {sm['lost']:2d}")
    fig_trajectories(data, walls, a.out)
    fig_error_boxplot(data, a.out)
    fig_error_over_time(data, a.out)
    fig_exec_times(data, a.out, a.period_us, a.kf_every)
    breakdowns = {}
    for name, *_ in RUNS:
        b = load_breakdown(os.path.join(a.runs_dir, name))
        if b:
            breakdowns[name] = b
    if fig_memory_breakdown(breakdowns, a.out):
        print("memory breakdown from", len(breakdowns), "runs with breakdown.csv")
    print("figures in", a.out)


if __name__ == "__main__":
    main()
