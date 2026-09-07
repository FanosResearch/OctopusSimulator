#!/usr/bin/env python3
"""
Generate publication figures from the Octopus configuration-sweep CSVs.

Reads results/<axis>/<suite>.csv (columns identified by HEADER NAME, so the wide
EEMBC schema and the narrow SPLASH schema both work) and writes PNG+PDF figures
to results/figures/. See sweeps/README.md for the sweep design.
"""
import csv, os, base64
from collections import defaultdict
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES  = os.environ.get("OCTOPUS_RES", os.path.join(ROOT, "results"))          # data source
FIG  = os.environ.get("OCTOPUS_FIG", os.path.join(RES, "figures")); os.makedirs(FIG, exist_ok=True)

AXES  = ["arbiter", "replacement", "cache", "memory"]
ORDER = {"arbiter":["FCFSArbiter","RRArbiter","TDMArbiter"],
         "replacement":["LRU","RANDOM"],
         "cache":["16384","32768","65536"],
         "memory":["MainMemoryController","MCsim"]}
BASE  = {"arbiter":"FCFSArbiter","replacement":"LRU","cache":"32768","memory":"MainMemoryController"}
PRETTY= {"FCFSArbiter":"FCFS","RRArbiter":"RR","TDMArbiter":"TDM","LRU":"LRU","RANDOM":"RANDOM",
         "16384":"16 KB","32768":"32 KB","65536":"64 KB",
         "MainMemoryController":"MainMem","MCsim":"MCsim (DDR4)"}
AXLABEL = {"arbiter":"Bus arbiter","replacement":"Replacement","cache":"LLC size","memory":"Main memory"}
# per-stage worst-case columns (wide EEMBC schema), in pipeline order
STAGES = [("wcL1stall","L1 stall"),("wcReqBus","Req bus"),("wcL2stall","L2 stall"),
          ("wcL2access","L2 access"),("wcRespBus","Resp bus"),("wcDramBus","DRAM bus"),
          ("wcDRAM","DRAM")]
STAGE_COLORS = plt.get_cmap("viridis")(np.linspace(0.05, 0.9, len(STAGES)))
STATUS_COLOR = {"OK":"#2e7d32","TIMEOUT":"#ef6c00","INCOMPLETE":"#c62828","SKIP":"#9e9e9e","":"#bdbdbd"}

def load(axis, suite):
    p = os.path.join(RES, axis, f"{suite}.csv")
    if not os.path.isfile(p): return []
    with open(p, newline="") as fh:
        rows = list(csv.DictReader(fh))
    for r in rows:                       # numeric coercion; blank/NA -> None
        for k, v in list(r.items()):
            if k in ("benchmark","status") or k == list(r)[0]: continue
            try: r[k] = float(v)
            except (TypeError, ValueError): r[k] = None
    return rows

def valcol(rows):                        # name of the config-value column (1st)
    return list(rows[0].keys())[0] if rows else None

def ok(rows):  return [r for r in rows if r.get("status") == "OK"]

def save(fig, name):
    for ext in ("png","pdf"):
        fig.savefig(os.path.join(FIG, f"{name}.{ext}"), dpi=150, bbox_inches="tight")
    plt.close(fig)
    print("wrote", name)

# ---------------------------------------------------------------- fig 1: stacked
def fig_stacked():
    fig, axs = plt.subplots(2, 2, figsize=(11, 8)); axs = axs.ravel()
    have = False
    for i, axis in enumerate(AXES):
        ax = axs[i]; rows = ok(load(axis, "eembc")); vc = valcol(rows)
        if not rows: ax.set_visible(False); continue
        have = True
        vals = [v for v in ORDER[axis] if any(r[vc]==v for r in rows)]
        bottoms = np.zeros(len(vals))
        for (col, lab), color in zip(STAGES, STAGE_COLORS):
            hv = [np.mean([r[col] for r in rows if r[vc]==v and r.get(col) is not None] or [0]) for v in vals]
            ax.bar(range(len(vals)), hv, bottom=bottoms, color=color, label=lab, edgecolor="white", linewidth=.5)
            bottoms += np.array(hv)
        ax.set_xticks(range(len(vals))); ax.set_xticklabels([PRETTY.get(v,v) for v in vals])
        ax.set_title(AXLABEL[axis]); ax.set_ylabel("Worst-case latency (cycles)")
    if not have: return
    h,l = axs[0].get_legend_handles_labels()
    fig.legend(h, l, ncol=len(STAGES), loc="lower center", bbox_to_anchor=(.5,-.02), frameon=False)
    fig.suptitle("Worst-case latency, decomposed by pipeline stage (EEMBC, mean over benchmarks)", fontsize=13)
    fig.tight_layout(rect=(0,.04,1,.98)); save(fig, "fig1_stage_breakdown")

# ------------------------------------------------------------ fig 2: cache lines
def fig_cache():
    fig, axs = plt.subplots(1, 2, figsize=(11, 4.2), sharey=False)
    for ax, suite in zip(axs, ("eembc","splash")):
        rows = ok(load("cache", suite)); vc = valcol(rows)
        if not rows: ax.set_visible(False); continue
        sizes = [s for s in ORDER["cache"] if any(r[vc]==s for r in rows)]
        benches = sorted({r["benchmark"] for r in rows})
        for b in benches:
            ys = [next((r["avg"] for r in rows if r[vc]==s and r["benchmark"]==b), None) for s in sizes]
            if all(y is not None for y in ys):
                ax.plot([PRETTY[s] for s in sizes], ys, marker="o", label=b, linewidth=1.4)
        ax.set_title(f"{suite.upper()}"); ax.set_xlabel("LLC size"); ax.set_ylabel("Mean effective latency (cycles)")
        ax.grid(True, alpha=.3)
        if suite=="splash": ax.legend(fontsize=7, ncol=2, loc="upper right")
    fig.suptitle("Cache-capacity sweep: larger LLC lowers latency (monotonic on SPLASH)", fontsize=13)
    fig.tight_layout(); save(fig, "fig2_cache_capacity")

# ----------------------------------------------------------- fig 3: memory model
def fig_memory():
    rows = ok(load("memory","eembc")); vc = valcol(rows)
    if not rows: return
    benches = sorted({r["benchmark"] for r in rows})
    mm = [next((r["wcDRAM"] for r in rows if r[vc]=="MainMemoryController" and r["benchmark"]==b), None) for b in benches]
    mc = [next((r["wcDRAM"] for r in rows if r[vc]=="MCsim" and r["benchmark"]==b), None) for b in benches]
    x = np.arange(len(benches)); w=.38
    fig, ax = plt.subplots(figsize=(10,4.2))
    ax.bar(x-w/2, [v or 0 for v in mm], w, label="MainMem (fixed)", color="#5b8def")
    ax.bar(x+w/2, [v or 0 for v in mc], w, label="MCsim (DDR4)",   color="#ef6c00")
    for i,v in enumerate(mc):
        if v is None: ax.text(i+w/2, 2, "timeout", rotation=90, fontsize=7, ha="center", va="bottom", color="#c62828")
    ax.set_xticks(x); ax.set_xticklabels([b.replace("-trace","") for b in benches], rotation=30, ha="right")
    ax.set_ylabel("Worst-case DRAM latency (cycles)"); ax.legend()
    ax.set_title("Memory-model axis: cycle-accurate MCsim DDR4 vs fixed-latency main memory (EEMBC)")
    fig.tight_layout(); save(fig, "fig3_memory_dram")

# -------------------------------------------------- fig 4: WCET vs avg (arbiter)
def fig_wcet():
    rows = ok(load("arbiter","eembc")); vc = valcol(rows)
    if not rows: return
    vals = [v for v in ORDER["arbiter"] if any(r[vc]==v for r in rows)]
    avg  = [np.mean([r["avg"]     for r in rows if r[vc]==v]) for v in vals]
    wct  = [np.mean([r["wcTotal"] for r in rows if r[vc]==v]) for v in vals]
    x=np.arange(len(vals)); w=.38
    fig, ax = plt.subplots(figsize=(7,4.4))
    ax.bar(x-w/2, avg, w, label="Mean effective latency", color="#5b8def")
    ax.bar(x+w/2, wct, w, label="Worst-case total latency", color="#c62828")
    ax.set_yscale("log"); ax.set_ylim(1, max(wct)*2.2)   # avg ~9 vs WC ~1000: log keeps both legible
    for i,(a,t) in enumerate(zip(avg,wct)):
        ax.text(i+w/2, t*1.05, f"{t/a:.0f}x", ha="center", va="bottom", fontsize=9, fontweight="bold")
    ax.set_xticks(x); ax.set_xticklabels([PRETTY[v] for v in vals])
    ax.set_ylabel("Latency (cycles, log scale)"); ax.legend(loc="upper left")
    ax.set_title("Real-time pessimism by arbiter (EEMBC): WC/avg ratio annotated")
    fig.tight_layout(); save(fig, "fig4_wcet_vs_avg")

# --------------------------------------------- fig 5: normalized-to-baseline WCET
def fig_normalized():
    fig, ax = plt.subplots(figsize=(9,4.4)); pos=0; ticks=[]; lab=[]
    for axis in AXES:
        rows = ok(load(axis,"eembc")); vc = valcol(rows)
        if not rows: continue
        vals=[v for v in ORDER[axis] if any(r[vc]==v for r in rows)]
        base=np.mean([r["wcTotal"] for r in rows if r[vc]==BASE[axis]] or [1]) or 1
        for v in vals:
            h=np.mean([r["wcTotal"] for r in rows if r[vc]==v])/base
            c = "#9e9e9e" if v==BASE[axis] else ("#c62828" if h>1 else "#2e7d32")
            ax.bar(pos, h, .8, color=c); ticks.append(pos); lab.append(PRETTY[v]); pos+=1
        ax.axvline(pos-.5, color="#ccc", lw=.8); pos+=.6
    ax.axhline(1, color="k", lw=.8, ls="--")
    ax.set_xticks(ticks); ax.set_xticklabels(lab, rotation=30, ha="right")
    ax.set_ylabel("Worst-case total latency\n(normalised to each axis baseline)")
    ax.set_title("Every axis moves the needle — WCET relative to baseline (EEMBC)")
    fig.tight_layout(); save(fig, "fig5_normalized")

# --------------------------------------------------- fig 6: configurability heat
def fig_heatmap():
    labels=[]; mat=[]; benches=None
    for axis in AXES:
        rows = load(axis,"eembc"); vc = valcol(rows)
        if not rows: continue
        if benches is None: benches = sorted({r["benchmark"] for r in rows})
        for v in ORDER[axis]:
            if not any(r[vc]==v for r in rows): continue
            row=[next((r["wcTotal"] for r in rows if r[vc]==v and r["benchmark"]==b and r["status"]=="OK"), np.nan) for b in benches]
            mat.append(row); labels.append(f"{AXLABEL[axis][:4]}: {PRETTY[v]}")
    if not mat: return
    mat=np.array(mat, float)
    fig, ax = plt.subplots(figsize=(10, .5*len(labels)+1.5))
    im=ax.imshow(mat, aspect="auto", cmap="magma")
    ax.set_xticks(range(len(benches))); ax.set_xticklabels([b.replace("-trace","") for b in benches], rotation=35, ha="right")
    ax.set_yticks(range(len(labels))); ax.set_yticklabels(labels, fontsize=8)
    fig.colorbar(im, ax=ax, label="Worst-case total latency (cycles)")
    ax.set_title("Configuration x benchmark response surface (EEMBC wcTotal)")
    fig.tight_layout(); save(fig, "fig6_config_heatmap")

# ------------------------------------------------------ fig 7: completion status
def fig_status():
    suites=["eembc","splash"]
    fig, axs = plt.subplots(1, 2, figsize=(13,5.5))
    for ax, suite in zip(axs, suites):
        labels=[]; benches=set(); grid=[]
        data={}
        for axis in AXES:
            rows=load(axis,suite); vc=valcol(rows)
            if not rows: continue
            for v in ORDER[axis]:
                if not any(r[vc]==v for r in rows): continue
                key=f"{AXLABEL[axis][:4]}: {PRETTY[v]}"; labels.append(key); data[key]={}
                for r in rows:
                    if r[vc]==v: data[key][r["benchmark"]]=r["status"]; benches.add(r["benchmark"])
        benches=sorted(benches)
        for key in labels:
            grid.append([{"OK":0,"TIMEOUT":1,"INCOMPLETE":2}.get(data[key].get(b,""),3) for b in benches])
        if not grid: ax.set_visible(False); continue
        from matplotlib.colors import ListedColormap
        cmap=ListedColormap([STATUS_COLOR["OK"],STATUS_COLOR["TIMEOUT"],STATUS_COLOR["INCOMPLETE"],STATUS_COLOR[""]])
        ax.imshow(np.array(grid), aspect="auto", cmap=cmap, vmin=0, vmax=3)
        ax.set_xticks(range(len(benches))); ax.set_xticklabels([b.replace("-trace","") for b in benches], rotation=40, ha="right", fontsize=8)
        ax.set_yticks(range(len(labels))); ax.set_yticklabels(labels, fontsize=8)
        ax.set_title(f"{suite.upper()} completion")
    fig.legend(handles=[Patch(color=STATUS_COLOR[s], label=s or "missing") for s in ["OK","TIMEOUT","INCOMPLETE",""]],
               ncol=4, loc="lower center", frameon=False)
    fig.suptitle("Completion status across configuration x benchmark", fontsize=13)
    fig.tight_layout(rect=(0,.05,1,.97)); save(fig, "fig7_status_matrix")

if __name__ == "__main__":
    fig_stacked(); fig_cache(); fig_memory(); fig_wcet(); fig_normalized(); fig_heatmap(); fig_status()
    print("figures ->", FIG)
