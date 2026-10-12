# Install and verify

This page gets a fresh checkout to a known-good simulator build. Commands assume
Ubuntu or another Linux environment with Bash, a C++17 compiler, CMake, and Git.
Use `stable` for the released documentation baseline or `dev-stable` when
developing against the integration branch.

## Option 1: GitHub Codespaces

The stable repository includes a development container. On GitHub, choose
**Code -> Codespaces -> Create codespace** on the stable branch. The container
provides the compiler and Python tooling used by the repository.

## Option 2: Local build

Install the basic toolchain:

```bash
sudo apt update
sudo apt install build-essential cmake git python3
```

Clone and build:

```bash
git clone https://github.com/FanosResearch/OctopusSimulator.git
cd OctopusSimulator
# Optional for development work:
# git switch dev-stable
mkdir -p build
cmake -S . -B build
cmake --build build -j
```

The binary is `build/Octopus_Simulator`.

## Verify the environment

Run the repository check before starting an experiment:

```bash
bash scripts/check_environment.sh
```

A healthy stable checkout ends with **15,364 requests** and a worst-case DRAM
latency of **377 cycles**. These are equality checks. A mismatch usually means a
stale build, changed configuration, missing benchmark data, or a portability
problem such as CRLF line endings.

## Get benchmark traces

Benchmarks are not committed to the simulator repository. Fetch them only when
needed:

```bash
./get_benchmarks.sh
```

For a smaller EEMBC-only checkout:

```bash
BMS_SPARSE=eembc-traces ./get_benchmarks.sh
```

The full trace set needs substantial disk space after decompression. See
[Reproducibility](reproducibility.md) for the exact suites and resource notes.

!!! warning
    Do not run the simulator before the benchmark directory exists unless your
    selected configuration points at a workload already present on disk.