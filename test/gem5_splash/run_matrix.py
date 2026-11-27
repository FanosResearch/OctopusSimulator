#!/usr/bin/env python3
"""Runs SPLASH-3 under gem5 SE for the gem5 Ruby/Garnet versus gem5 + Octopus comparison
(docs/gem5_SPLASH_Garnet_vs_Octopus.md): any subset of benchmarks x configurations x input sizes,
one directory per run under the results root, at most --jobs gem5 processes at a time, all at
nice 19. Standard-library Python only.

  run_matrix.py --bench fft,lu_contig,radix --config garnet,simple,octopus,classic \\
      --size small --threads 16 --jobs 6 --out <results root>
  run_matrix.py --bench all --config all --size small,default --dry-run    # list the runs

Configurations (all: 16 O3 cores at 3 GHz, 32 KiB 8-way L1I/L1D, 8 MiB shared L2/LLC,
DRAM timed as MCsim's DDR4-2400U, see the doc):
  garnet   Ruby MESI_Two_Level, Garnet 4x4 Mesh_XY             (se_arm_ruby.py, gem5-ruby build)
  simple   Ruby MESI_Two_Level, SimpleNetwork 4x4 mesh, XY     (se_arm_ruby.py --network simple)
  octopus  Octopus directory MESI on its 4x4 mesh NoC          (se_arm.py, gem5 linked with Octopus)
  classic  gem5 classic caches, crossbar, no prefetchers       (se_arm.py --classic, reference)

A run directory holds gem5's outdir (stats.txt, simout.txt, ...), run.json (what was run),
time.txt (/usr/bin/time -v: wall time, peak RSS) and rc.txt. An Octopus run additionally
writes newLogger/ (LatencyReport_C*.csv, NetworkStats.csv); the LatencyReports are reduced to
octopus_requests.json right after the run and deleted unless --keep-reports (they reach
gigabytes on the default inputs). A run whose rc.txt says 0 is skipped unless --force.
collect.py turns a results root into one CSV.
"""
import argparse
import concurrent.futures as cf
import json
import os
import shlex
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
OCTOPUS = os.path.abspath(os.path.join(HERE, "..", ".."))
CONFIGS_DIR = os.path.join(OCTOPUS, "gem5", "configs")
BASE = "/home/mohassan/Octopus_transfer"

BENCHES = ["barnes", "fmm", "ocean_contig", "ocean_non_contig", "radiosity", "raytrace",
           "volrend", "water_nsquared", "water_spatial", "cholesky", "fft", "lu_contig",
           "lu_non_contig", "radix"]
CONFIGS = ["garnet", "simple", "octopus", "classic"]
OCTOPUS_PRESET = "MultiCoreSystem_gem5_mesh_ruby"

sys.path.insert(0, HERE)
import collect  # noqa: E402  (reduce_octopus_reports)


def bench_cmd(splash, bench, size, threads, selfcheck=False):
    out = subprocess.run([os.path.join(splash, "scripts", "bench_cmd.sh"), "m5", bench, size,
                          str(threads)] + (["test"] if selfcheck else []),
                         check=True, capture_output=True, text=True).stdout
    kv = dict(line.split("=", 1) for line in out.splitlines() if "=" in line)
    return kv["DIR"], kv["EXE"], kv["ARGS"].split(), kv["STDIN"] or None


def gem5_command(a, config, cores, binary, bargs, stdin, outdir):
    if config in ("garnet", "simple"):
        cmd = [a.gem5_ruby, "-re", "-d", outdir, os.path.join(CONFIGS_DIR, "se_arm_ruby.py"),
               "--network", config, "--num-cores", str(cores), "--dram", "mcsim_match"]
    elif config == "octopus":
        cmd = [a.gem5_octopus, "-re", "-d", outdir, os.path.join(CONFIGS_DIR, "se_arm.py"),
               "--num-cores", str(cores), "--octopus-config", OCTOPUS_PRESET,
               "--octopus-param", "cpu[*].log_requests(i)=1"]
    elif config == "classic":
        cmd = [a.gem5_octopus, "-re", "-d", outdir, os.path.join(CONFIGS_DIR, "se_arm.py"),
               "--classic", "--num-cores", str(cores), "--octopus-config", OCTOPUS_PRESET,
               "--classic-l1", "32KiB,8", "--classic-l2", "8MiB,8", "--classic-no-prefetch",
               "--dram", "mcsim_match"]
    else:
        raise ValueError(config)
    if stdin:
        cmd += ["--stdin", stdin]
    cmd += ["--binary", binary, "--"] + bargs
    return cmd


def run_one(a, bench, size, config):
    name = f"{bench}-{size}-p{a.threads}-{config}" + ("-t" if a.selfcheck else "")
    rundir = os.path.join(a.out, name)
    rcfile = os.path.join(rundir, "rc.txt")
    if not a.force and os.path.exists(rcfile) and open(rcfile).read().strip() == "0":
        return name, "skip (done)"
    cwd, exe, bargs, stdin = bench_cmd(a.splash, bench, size, a.threads, a.selfcheck)
    cmd = gem5_command(a, config, a.cores, os.path.join(cwd, exe), bargs, stdin, rundir)
    full = (["nice", "-n", "19", "/usr/bin/time", "-v", "-o", os.path.join(rundir, "time.txt")]
            + (["timeout", str(a.timeout)] if a.timeout else []) + cmd)
    if a.dry_run:
        return name, "cd " + shlex.quote(cwd) + " && " + " ".join(shlex.quote(c) for c in full)
    os.makedirs(rundir, exist_ok=True)
    meta = {"bench": bench, "size": size + ("-t" if a.selfcheck else ""), "threads": a.threads,
            "cores": a.cores,
            "config": config, "cwd": cwd, "cmd": full, "start": time.time()}
    with open(os.path.join(rundir, "run.json"), "w") as f:
        json.dump(meta, f, indent=1)
    with open(os.path.join(rundir, "console.txt"), "w") as log:
        rc = subprocess.run(full, cwd=cwd, stdout=log, stderr=subprocess.STDOUT).returncode
    if config == "octopus":
        try:
            collect.reduce_octopus_reports(rundir, delete=not a.keep_reports)
        except Exception as e:  # keep the reports for a look
            print(f"{name}: LatencyReport reduction failed: {e}", flush=True)
    with open(rcfile, "w") as f:
        f.write(f"{rc}\n")
    return name, f"rc={rc} {time.time() - meta['start']:.0f} s"


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bench", default="fft,lu_contig,radix", help="comma list or 'all'")
    p.add_argument("--config", default="all", help=f"comma list of {CONFIGS} or 'all'")
    p.add_argument("--size", default="small", help="small, default or small,default")
    p.add_argument("--threads", type=int, default=16, help="SPLASH P (main thread included)")
    p.add_argument("--cores", type=int, default=None, help="gem5 cores (default: --threads)")
    p.add_argument("--jobs", type=int, default=6)
    p.add_argument("--timeout", type=int, default=0, help="seconds per run, 0 = none")
    p.add_argument("--out", default=os.path.join(OCTOPUS, "test", "gem5_splash", "results"))
    p.add_argument("--splash", default=os.path.join(BASE, "splash-gem5"))
    p.add_argument("--gem5-ruby", default=os.path.join(BASE, "gem5-ruby", "build", "ARM_MESI2", "gem5.opt"))
    p.add_argument("--gem5-octopus", default=os.path.join(BASE, "gem5-noc", "build", "ARM", "gem5.opt"))
    p.add_argument("--keep-reports", action="store_true", help="keep Octopus's LatencyReport_C*.csv")
    p.add_argument("--force", action="store_true", help="rerun runs that already finished")
    p.add_argument("--selfcheck", action="store_true",
                   help="add the suite's self-check -t (FFT, LU, RADIX, CHOLESKY; FFT's check adds an "
                        "inverse FFT inside the ROI, so use it for validation, not for timing)")
    p.add_argument("--dry-run", action="store_true")
    a = p.parse_args()
    a.cores = a.cores or a.threads
    a.out = os.path.abspath(a.out)
    benches = BENCHES if a.bench == "all" else a.bench.split(",")
    configs = CONFIGS if a.config == "all" else a.config.split(",")
    sizes = a.size.split(",")
    for b in benches:
        assert b in BENCHES, b
    for c in configs:
        assert c in CONFIGS, c
    # longest first: Octopus, then Ruby, then classic
    order = {"octopus": 0, "garnet": 1, "simple": 2, "classic": 3}
    runs = [(b, s, c) for s in sizes for c in sorted(configs, key=order.get) for b in benches]
    print(f"{len(runs)} runs, {a.jobs} at a time, results in {a.out}", flush=True)
    with cf.ThreadPoolExecutor(max_workers=1 if a.dry_run else a.jobs) as ex:
        for fut in cf.as_completed([ex.submit(run_one, a, b, s, c) for b, s, c in runs]):
            name, status = fut.result()
            print(f"{name}: {status}", flush=True)


if __name__ == "__main__":
    main()
