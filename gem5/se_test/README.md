# SE-mode test for the Octopus/gem5 bridge

The SE counterpart of `llsc_wfe_test`: one self-checking aarch64 binary that
gem5 loads directly. No kernel, disk image or checkpoint is involved, so a run
takes about a minute; this is the quick check for bridge or protocol changes
before an FS run.

| | |
| --- | --- |
| P1 | private streaming: each thread fills, then sums, its own buffer (1 MiB by default, twice the Octopus L1). The sum has a closed form. |
| P2 | shared counter: every thread adds to one atomic counter; the line migrates between cores on every increment. |
| P3 | producer/consumer: each thread writes its 64 KiB slice of a shared array, a barrier, then reads and checks all other slices. Four rounds. |

Threads are capped at the number of online CPUs (gem5 SE does not preempt) and
the main thread is worker 0. The ROI (`m5_work_begin`..`m5_work_end`) covers
the three phases plus thread creation and joins; `se_arm.py` resets the stats
at the first marker and dumps them at the second. Last line is
`RESULT: PASS`/`FAIL` and the exit status matches.

```
se_test [threads] [KiB per thread] [iterations]      # defaults: 4 1024 2000
```

## Build

Needs the host packages `g++-aarch64-linux-gnu` (which pulls the gcc, libstdc++
and libc cross packages) and, for the smoke test, `qemu-user-static`:

```sh
cd /home/guotong/gem5_resource/se_test
make                                    # se_test-static, -dynamic, -noroi
make run-qemu                           # smoke test of the -noroi build
```

`make` assembles gem5's own `util/m5/src/abi/arm64/m5op.S` for the ROI
markers, so `libm5.a` does not have to be built. On an aarch64 host use
`make CROSS=`.

## Run under gem5

```sh
cd /home/guotong/gem5_arm
./build/ARM/gem5.opt -re -d se_test_o3_oct     /home/guotong/Octopus_stable/gem5/configs/se_arm.py
./build/ARM/gem5.opt -re -d se_test_o3_classic /home/guotong/Octopus_stable/gem5/configs/se_arm.py --classic
```

`se_arm.py` takes the same options as `fs_arm.py` (`--octopus-config`,
`--octopus-param`, `--cache-ports`, `--max-ticks`, `--classic`) plus
`--binary`, `--num-cores` (4, the preset has 8 L1 bridges) and `--cpu-type
{o3,timing}`. Arguments for the binary go after `--`:

```sh
./build/ARM/gem5.opt -re -d se_test_short /home/guotong/Octopus_stable/gem5/configs/se_arm.py -- 2 64 100
```

`se_test-dynamic` runs with `--workload-type dynamic`; the loader and libc are
taken from `--sysroot`, by default the cross toolchain's
`/usr/aarch64-linux-gnu`.

`stats.txt` holds two dumps: the ROI (at `m5_work_end`) and the final one at
exit. Compare `simTicks`, per-core `numCycles`/`ipc` and `lsq0.loadToUse`
between the Octopus and classic runs, as for the FS experiments. The bridge
prints its snoop / hold-back / port-refusal counters at exit; they are for
the whole run, not the ROI.
