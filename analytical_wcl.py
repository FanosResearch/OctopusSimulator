#!/usr/bin/env python3
"""
Compute the analytical worst-case latency (WCL) bound from the PHiSCo paper's
equations, parametrized by interconnect topology (fully-connected vs 2D mesh).

For 2D-mesh NoC, the bound is per-core: a core's WCL_demand depends on its
Manhattan distance to the LLC corner (since msg.req traverses h hops to reach
the directory). For fully-connected mesh every endpoint is 1 hop from every
other endpoint, so h = 1 for every core.

The dependency-chain terms (WCL_fwd, WCL_data on the cache-to-cache path) are
worst-cased at the network diameter, since the chain can involve any core.

References (paper section / equation):
  - L_longest: Lemma 2 / Eq. 1 (5.LatencyAnalysis.tex)
  - WCL_stall: Lemma 4 / Eq. 2
  - WCL_demand = L_longest + WCL_stall                  (Theorem 1)
  - L_longest_L1repl: Lemma 6 / Eq. 4
  - WCL_L1repl = WCL_demand + L_longest_L1repl          (Theorem 2)
  - L_LLCrepl: Lemma 8 / Eq. 6
  - WCL_LLCrepl = (N+1)(2L_sBus + L_mem) - 3 + WCL_stall + L_LLCrepl  (Theorem 3)
  - WCL_combined = WCL_demand + WCL_L1repl + WCL_LLCrepl              (Eq. 7)

Usage:
    python3 analytical_wcl.py --cores 16 --rows 4 --topology mesh2d
    python3 analytical_wcl.py --cores 16 --rows 4 --topology fullyconnected
"""

import argparse
import sys
from dataclasses import dataclass


# Latency parameters from Table II in 6.Evaluation.tex.
@dataclass
class LatencyParams:
    L_msg:   int = 2     # per-link interconnect message latency
    L_data:  int = 5     # per-link interconnect data latency
    L_sBus:  int = 5     # per-link sBus latency
    L_mem:   int = 250   # main memory access latency
    L_L1:    int = 1     # L1 processing latency
    L_dir:   int = 1     # Directory processing latency
    L_LLC:   int = 20    # LLC data array access latency


def manhattan(a: int, b: int, cols: int) -> int:
    ar, ac = divmod(a, cols)
    br, bc = divmod(b, cols)
    return abs(ar - br) + abs(ac - bc)


def hop_count_to_llc(core_id: int, rows: int, cols: int, topology: str) -> int:
    """How many router-to-router hops a request from core_id takes to reach
    the directory at the LLC corner R(0,0).

    For fully-connected: always 1 (one arbitration stage between endpoints).
    For 2D mesh: Manhattan distance from this core's local router to R(0,0).
                 LLC sits at R(0,0), so this equals manhattan(core_id, 0).
    """
    if topology == "fullyconnected":
        return 1
    elif topology == "mesh2d":
        return manhattan(core_id, 0, cols)
    else:
        raise ValueError(f"unknown topology: {topology}")


def diameter(rows: int, cols: int, topology: str) -> int:
    """Max hop count between any two endpoints."""
    if topology == "fullyconnected":
        return 1
    elif topology == "mesh2d":
        return (rows - 1) + (cols - 1)
    else:
        raise ValueError(f"unknown topology: {topology}")


def wcl_per_core(core_id: int, N: int, rows: int, cols: int,
                 topology: str, p: LatencyParams = LatencyParams()):
    """Return a dict of per-core WCL components and the combined bound.

    Per-core: WCL_req uses this core's hop count to LLC.
    Worst-case (whole-system): WCL_fwd and WCL_data on the dependency chain
    use the diameter (the chain may involve any core).
    """
    h_c = hop_count_to_llc(core_id, rows, cols, topology)
    h_diam = diameter(rows, cols, topology)

    # Per-hop WCL at a single RROF-style arbiter.
    WCL_hop_msg  = (N + 1) * p.L_msg  - 1
    WCL_hop_data = (N + 1) * p.L_data - 1

    # Per-core interconnect terms.
    WCL_req       = h_c    * WCL_hop_msg
    WCL_data_LLC  = h_c    * WCL_hop_data       # data path LLC -> this core
    WCL_fwd_diam  = h_diam * WCL_hop_msg        # worst-case fwd in chain
    WCL_data_diam = h_diam * WCL_hop_data       # worst-case data in chain

    # ---- Eq. 1: L_longest (demand request, four cases dominated by case (iv)) ----
    # (N+1)(L_L1 + L_dir + L_sBus + L_mem + L_sBus + L_dir) - 6
    # = (N+1)(3 + 2 L_sBus + L_mem)   (L_L1 = L_dir = 1)
    L_longest = (
        (N + 1) * (3 + 2 * p.L_sBus + p.L_mem) + WCL_req
        + max((N + 1) * p.L_LLC - 1, WCL_data_LLC + N)
        - 6
    )

    # ---- Eq. 2: WCL_stall (dependency-chain stall at the directory) ----
    # Two parallel paths in Fig. 4, take max of:
    #   N^2 + N + WCL_fwd + WCL_data
    #   N + N*(WCL_data + 2N)
    WCL_stall = max(
        N * N + N + WCL_fwd_diam + WCL_data_diam,
        N + N * (WCL_data_diam + 2 * N),
    )

    # ---- Theorem 1: WCL_demand ----
    WCL_demand = L_longest + WCL_stall

    # ---- Eq. 4: L_longest_L1repl ----
    # 2N + WCL_data + max((N+1) L_LLC - 1, WCL_fwd + N)
    L_longest_L1repl = (
        2 * N + WCL_data_diam
        + max((N + 1) * p.L_LLC - 1, WCL_fwd_diam + N)
    )

    # ---- Theorem 2: WCL_L1repl ----
    WCL_L1repl = WCL_demand + L_longest_L1repl

    # ---- Eq. 6: L_LLCrepl ----
    # 3N + WCL_fwd + WCL_data - 2 + (N+1)(L_sBus + L_mem)
    L_LLCrepl = (
        3 * N + WCL_fwd_diam + WCL_data_diam - 2
        + (N + 1) * (p.L_sBus + p.L_mem)
    )

    # ---- Theorem 3: WCL_LLCrepl ----
    WCL_LLCrepl = (
        (N + 1) * (2 * p.L_sBus + p.L_mem) - 3
        + WCL_stall + L_LLCrepl
    )

    # ---- Eq. 7: WCL_combined ----
    WCL_combined = WCL_demand + WCL_L1repl + WCL_LLCrepl

    return {
        "core":         core_id,
        "h_c":          h_c,
        "h_diam":       h_diam,
        "WCL_req":      WCL_req,
        "WCL_data_LLC": WCL_data_LLC,
        "WCL_fwd_diam": WCL_fwd_diam,
        "WCL_data_diam":WCL_data_diam,
        "L_longest":    L_longest,
        "WCL_stall":    WCL_stall,
        "WCL_demand":   WCL_demand,
        "WCL_L1repl":   WCL_L1repl,
        "WCL_LLCrepl":  WCL_LLCrepl,
        "WCL_combined": WCL_combined,
    }


def wcl_table(N: int, rows: int, cols: int, topology: str,
              p: LatencyParams = LatencyParams()):
    """Compute WCL for every core. Returns list of per-core dicts."""
    return [wcl_per_core(c, N, rows, cols, topology, p) for c in range(N)]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cores", type=int, required=True)
    ap.add_argument("--rows",  type=int, required=True,
                    help="mesh rows (cols = cores/rows)")
    ap.add_argument("--topology", choices=["mesh2d", "fullyconnected"],
                    default="mesh2d")
    args = ap.parse_args()

    if args.cores % args.rows != 0:
        sys.exit(f"cores ({args.cores}) must be divisible by rows ({args.rows})")
    cols = args.cores // args.rows

    table = wcl_table(args.cores, args.rows, cols, args.topology)

    print(f"# Analytical WCL bound: {args.cores}-core {args.rows}x{cols} {args.topology}")
    print(f"# Diameter = {diameter(args.rows, cols, args.topology)} hops")
    print()
    cols_print = ["core", "h_c", "WCL_req", "WCL_data_LLC",
                  "L_longest", "WCL_stall", "WCL_demand",
                  "WCL_L1repl", "WCL_LLCrepl", "WCL_combined"]
    print(",".join(cols_print))
    for row in table:
        print(",".join(str(row[k]) for k in cols_print))

    # Summary across cores.
    wcl_demand_max = max(r["WCL_demand"] for r in table)
    wcl_demand_min = min(r["WCL_demand"] for r in table)
    wcl_combined_max = max(r["WCL_combined"] for r in table)
    print()
    print(f"# WCL_demand   range: {wcl_demand_min} .. {wcl_demand_max}")
    print(f"# WCL_combined max:   {wcl_combined_max}")


if __name__ == "__main__":
    main()
