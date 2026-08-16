#!/usr/bin/env python3
"""
Generate the R2 figures for the 2D-mesh NoC evaluation.

Reads per-(topology, cores) Summary.csv files emitted by the sweep harness
plus the analytical_wcl bound, and produces:

  FigA_scaling.{pdf,png}   - WCL vs core count, one panel per pairing
                             strategy. Lines for 2D-mesh observed-max,
                             2D-mesh analytical, fully-connected observed-max,
                             fully-connected analytical.
  FigB_percore_16.{pdf,png} - WCL bar per core at N=16 sorted by Manhattan
                              distance to LLC, three colored bars per core
                              (one per strategy), analytical bound overlaid.

Inputs (resolved relative to the simulator's BMs root):
  BMs/Synthetic/<N>Cores/PingPong-<strat>/MESI_<topology>_logs/Summary.csv

Outputs land in $RESULTS_DIR (default: ./results/).
"""

import argparse
import csv
import os
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

# Pull the analytical bound from the sibling script.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from analytical_wcl import wcl_table  # noqa: E402

CONFIGS = [
    # (cores, rows)
    (2,  2),
    (4,  2),
    (8,  4),
    (16, 4),
    (32, 8),
]
STRATEGIES = ["antipodal", "neighbor", "random"]
TOPOLOGIES = ["mesh2d", "fullyconnected"]
PROTOCOL = "MESI"


def load_summary(BMs_root: Path, cores: int, strat: str, topology: str):
    """Return list of per-core dicts from Summary.csv, or None if missing."""
    summary = (BMs_root / "Synthetic" / f"{cores}Cores"
               / f"PingPong-{strat}" / f"{PROTOCOL}_{topology}_logs"
               / "Summary.csv")
    if not summary.exists():
        return None
    rows = []
    with open(summary) as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append({
                "core":   int(r["Core Id"]),
                "wcl":    int(r["Worst-case Total Latency"]),
                "finish": int(r["Finish Cycle"]),
            })
    return rows


# -- Strategy-specific pairing for analytical hop count of cache-to-cache fwds --
# For per-core analytical bound at N=16 we need to know the partner of each core
# to compute fwd_hop. But the dominant term WCL_stall already uses diameter, so
# we don't need partner-specific hop counts.


def figA_scaling(BMs_root: Path, out_dir: Path):
    """Figure A: scaling sweep. One panel per strategy.

    X = number of cores. Y = WCL (log scale).
    Lines:
      - 2D-mesh analytical (worst-core WCL_combined)
      - 2D-mesh observed max (max across cores)
      - fully-connected analytical (worst-core WCL_combined)
      - fully-connected observed max
    """
    fig, axes = plt.subplots(1, len(STRATEGIES), figsize=(12, 3.5), sharey=True)

    # Same marker size everywhere; the observed lines for both topologies
    # nearly coincide (mesh2d > FC by ~0.25% across all N), so we use the
    # SAME size and a small horizontal jitter on mesh2d so both markers are
    # individually visible.
    MS = 8
    JITTER = 0.04   # log-space x-offset to nudge mesh2d markers right

    for ax, strat in zip(axes, STRATEGIES):
        for topology, marker, color, label_pfx, xshift, zorder in [
            ("fullyconnected", "s", "#0070C0", "Fully-conn", -JITTER, 2),
            ("mesh2d",         "o", "#C00000", "2D-mesh",     JITTER, 4),
        ]:
            xs = []
            obs_max = []
            ana_max = []
            for cores, rows in CONFIGS:
                cols = cores // rows
                ana = wcl_table(cores, rows, cols, topology)
                ana_max_c = max(r["WCL_combined"] for r in ana)
                summary = load_summary(BMs_root, cores, strat, topology)
                if summary is None:
                    continue
                obs_max_c = max(r["wcl"] for r in summary)
                xs.append(cores)
                obs_max.append(obs_max_c)
                ana_max.append(ana_max_c)

            # Apply log-space jitter so coincident markers are individually
            # visible. (np.power(2, log2(x) + shift) == x * 2^shift)
            xs_jit = [c * (2 ** xshift) for c in xs]

            # Analytical: dashed, hollow marker.
            ax.plot(xs_jit, ana_max, marker=marker, linestyle="--", color=color,
                    markersize=MS, markerfacecolor="none", markeredgewidth=1.4,
                    linewidth=1.2, zorder=zorder,
                    label=f"{label_pfx} analytical")
            # Observed: solid line, filled marker.
            ax.plot(xs_jit, obs_max, marker=marker, linestyle="-", color=color,
                    markersize=MS, linewidth=1.8, zorder=zorder + 1,
                    label=f"{label_pfx} observed")

        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        # X-axis label sits at the bottom of the panel and incorporates the
        # subfigure letter (a)/(b)/(c), per the paper convention.
        letter = chr(ord("a") + STRATEGIES.index(strat))
        ax.set_xlabel(f"Cores\n({letter}) {strat}")
        ax.set_xticks([2, 4, 8, 16, 32])
        ax.set_xticklabels([str(c) for c in [2, 4, 8, 16, 32]])
        ax.grid(True, which="both", alpha=0.3)

    axes[0].set_ylabel("WCL (cycles)")
    axes[-1].legend(loc="lower right", fontsize=8, framealpha=0.9)
    fig.tight_layout()

    for ext in ("pdf", "png"):
        out = out_dir / f"FigA_scaling.{ext}"
        fig.savefig(out, dpi=300, bbox_inches="tight")
        print(f"  wrote {out}")
    plt.close(fig)


def figB_percore_16(BMs_root: Path, out_dir: Path):
    """Figure B: consolidated per-core WCL at N=16 (4x4 mesh), by hop distance.

    X = Manhattan hop distance from LLC corner R(0,0), h in {0..6}.
    Y = WCL (cycles).
    Shaded band: min/max envelope of observed WCL across all (core, pairing)
                 combinations at each h.
    Solid line: per-h analytical bound (max across cores at that h).
    Scatter:   individual (core, pairing) observed WCLs, color-coded by pairing
               (very small markers to emphasize the tight band).
    """
    cores = 16
    rows  = 4
    cols  = 4

    # Per-core analytical bound and hop distance.
    ana = wcl_table(cores, rows, cols, "mesh2d")
    h_by_core      = {r["core"]: r["h_c"] for r in ana}
    ana_by_core    = {r["core"]: r["WCL_demand"] for r in ana}

    # Collect observed WCL per (strategy, core).
    obs = {}  # strat -> {core_id: wcl}
    for strat in STRATEGIES:
        summary = load_summary(BMs_root, cores, strat, "mesh2d")
        if summary is None:
            print(f"  [WARN] missing 2D-mesh data for strategy {strat}")
            obs[strat] = {}
        else:
            obs[strat] = {r["core"]: r["wcl"] for r in summary}

    # Build per-h observed and analytical aggregates.
    hop_values = sorted({h_by_core[c] for c in range(cores)})
    obs_min_by_h = []
    obs_max_by_h = []
    ana_by_h     = []
    scatter_xs   = {strat: [] for strat in STRATEGIES}
    scatter_ys   = {strat: [] for strat in STRATEGIES}
    for h in hop_values:
        cores_at_h = [c for c in range(cores) if h_by_core[c] == h]
        all_obs = []
        for strat in STRATEGIES:
            for c in cores_at_h:
                v = obs[strat].get(c)
                if v is None:
                    continue
                all_obs.append(v)
                scatter_xs[strat].append(h)
                scatter_ys[strat].append(v)
        if not all_obs:
            obs_min_by_h.append(np.nan)
            obs_max_by_h.append(np.nan)
        else:
            obs_min_by_h.append(min(all_obs))
            obs_max_by_h.append(max(all_obs))
        ana_by_h.append(max(ana_by_core[c] for c in cores_at_h))

    fig, ax = plt.subplots(figsize=(3.5, 2.4))

    # Min/max envelope (shaded band) of observed WCL across all (core, pairing).
    # The band's tightness is what shows "all pairings agree" — no need for
    # 48 individual scatter dots to make that point.
    ax.fill_between(hop_values, obs_min_by_h, obs_max_by_h,
                    color="#4472C4", alpha=0.3, linewidth=0,
                    label="observed (min--max)")
    ax.plot(hop_values, obs_max_by_h, "-", color="#4472C4", linewidth=1.4,
            marker="o", markersize=4)

    # Per-h analytical bound (max across cores at that h).
    ax.plot(hop_values, ana_by_h, "--", color="#C00000", linewidth=1.4,
            marker="D", markersize=4, label="analytical bound")

    # Annotate the spread from corner to antipode.
    if obs_max_by_h[0] and obs_max_by_h[-1]:
        spread = obs_max_by_h[-1] / max(obs_min_by_h[0], 1)
        ax.annotate(
            f"{spread:.2f}$\\times$ spread",
            xy=(hop_values[-1], obs_max_by_h[-1]),
            xytext=(hop_values[-1] - 0.4, obs_max_by_h[-1] * 1.4),
            fontsize=7, ha="right",
            arrowprops=dict(arrowstyle="->", color="#444", lw=0.7),
        )

    ax.set_xlabel("Manhattan distance to LLC, $h$", fontsize=8)
    ax.set_ylabel("WCL (cycles)", fontsize=8)
    ax.set_xticks(hop_values)
    ax.tick_params(axis="both", labelsize=7)
    ax.grid(axis="y", alpha=0.3)
    # Legend below the plot, out of the data area entirely.
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.28),
              ncol=2, fontsize=7, framealpha=0.9, frameon=False)
    fig.tight_layout()

    for ext in ("pdf", "png"):
        out = out_dir / f"FigB_percore_16.{ext}"
        fig.savefig(out, dpi=300, bbox_inches="tight")
        print(f"  wrote {out}")
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bms-root", type=Path,
                    default=Path(__file__).resolve().parent / "BMs",
                    help="Path to BMs/ root (default: ./BMs/)")
    ap.add_argument("--out-dir", type=Path,
                    default=Path(__file__).resolve().parent / "results",
                    help="Output dir for figures (default: ./results/)")
    args = ap.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    print(f"BMs root: {args.bms_root}")
    print(f"Out dir:  {args.out_dir}")

    print("Generating Figure A (scaling)...")
    figA_scaling(args.bms_root, args.out_dir)

    print("Generating Figure B (per-core at N=16)...")
    figB_percore_16(args.bms_root, args.out_dir)

    print("Done.")


if __name__ == "__main__":
    main()
