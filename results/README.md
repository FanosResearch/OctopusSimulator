# Results

Pre-generated CSV outputs and figures from the configuration-axis sweeps.
Everything here is regenerable — see [`../REPRODUCIBILITY.md`](../REPRODUCIBILITY.md)
and [`../sweeps/README.md`](../sweeps/README.md):

```bash
bash sweeps/run_overnight.sh        # (re)run the sweep matrix
python sweeps/plot_results.py       # (re)draw figures/ from the CSVs
```

## Layout

Each per-axis directory holds one row per `(config value, benchmark)`:

| Directory | Axis varied | Files |
|---|---|---|
| `arbiter/` | bus arbiter (FCFS / RR / TDM) | `eembc.csv`, `splash.csv` |
| `cache/` | LLC capacity (16 / 32 / 64 KB) | `eembc.csv`, `splash.csv` |
| `replacement/` | replacement (LRU / RANDOM) | `eembc.csv`, `splash.csv` |
| `memory/` | main memory (fixed-latency / MCsim DDR4) | `eembc.csv` (EEMBC-only) |
| `directory-eembc/` | directory-based protocols | `summary.csv`, `rows/`, `progress.log` |
| `giants/` | SPLASH-2 giants at baseline | `splash.csv` |
| `splash_baseline/` | SPLASH-2 baseline (snoop MESI) | `baseline.csv` |
| `figures/` | generated plots | `fig1`–`fig7` (`.png` + `.pdf`) |

`overnight.log` is the driver's progress + final status summary.

## CSV columns

```
<axis>, benchmark, status, avg, wcTotal, wcEff,
wcL1stall, wcReqBus, wcL2stall, wcL2access, wcRespBus, wcDramBus, wcDRAM, finish
```

- **status** — `OK` / `TIMEOUT` / `INCOMPLETE`
- **avg** — mean effective latency (cycles)
- **wcTotal** — worst-case end-to-end latency; **wcEff** — worst-case effective latency
- **wcL1stall … wcDRAM** — worst-case per-stage latency (L1 stall, request bus,
  L2 stall, L2 access, response bus, DRAM bus, DRAM)
- **finish** — last-retire cycle (≈ runtime)

## Figures

| File | Shows | In paper |
|---|---|---|
| `fig1_stage_breakdown` | per-request worst-case latency by stage, across axes | **Fig. 3** |
| `fig4_wcet_vs_avg` | mean vs. worst-case latency by bus arbiter | **Fig. 4** |
| `fig2_cache_capacity` | LLC-capacity effect on latency | — |
| `fig3_memory_dram` | DRAM latency (fixed-latency vs. MCsim) | — |
| `fig5_normalized` | normalized latency across axes | — |
| `fig6_config_heatmap` | axis × metric heatmap | — |
| `fig7_status_matrix` | completion status per `(config, benchmark)` | — |

Figures are emitted as both `.png` (for the paper) and `.pdf` (vector).
