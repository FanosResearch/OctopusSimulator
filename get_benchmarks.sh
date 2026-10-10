#!/usr/bin/env bash
#
# get_benchmarks.sh -- make sure the benchmark traces are present and inflated.
#
# The benchmarks live in their OWN repository (FanosResearch/OctopusBMs) so this
# simulator repository stays lean. On first use this clones it into BMs/
# (skipped when BMs/ is already a clone), then inflates the compressed SPLASH-2
# traces through BMs/prepare_traces.sh -- which is idempotent, so calling this
# before every run costs nothing once the traces exist. Every driver script
# (run_octopus.sh, run_splash.sh, sweep_protocols.sh, sweeps/*) calls it first,
# so a fresh clone of the simulator needs no manual step.
#
# Usage:  ./get_benchmarks.sh [--force] [DIR ...]
#         (arguments pass straight through to BMs/prepare_traces.sh)
#   BMS_REPO=<url>      override the benchmark repository URL
#   BMS_SPARSE="a b"    clone only these suite directories (e.g. "eembc-traces"),
#                       skipping the SPLASH archives. Only affects a FIRST clone.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BMS_DIR="$ROOT/BMs"
BMS_REPO="${BMS_REPO:-https://github.com/FanosResearch/OctopusBMs.git}"

if [ ! -d "$BMS_DIR/.git" ]; then
  if [ -d "$BMS_DIR" ] && [ -n "$(ls -A "$BMS_DIR" 2>/dev/null)" ]; then
    echo "ERROR: $BMS_DIR exists but is not a clone of the benchmark repository." >&2
    echo "       Move it aside, then re-run (this script clones $BMS_REPO into BMs/)." >&2
    exit 1
  fi
  echo "Benchmarks not present -- cloning $BMS_REPO into BMs/ ..."
  if [ -n "${BMS_SPARSE:-}" ]; then
    # Only the listed suite directories, space-separated. The SPLASH archives are
    # ~651 MB of the ~710 MB repository, so an environment that only runs EEMBC --
    # the tutorial container, CI -- downloads a tenth of the data with
    # BMS_SPARSE="eembc-traces". prepare_traces.sh finds no archives to inflate in
    # a sparse clone and reports zero work, which is correct and not an error.
    git clone --depth 1 --filter=blob:none --sparse "$BMS_REPO" "$BMS_DIR" \
      || { echo "ERROR: could not clone $BMS_REPO" >&2; exit 1; }
    ( cd "$BMS_DIR" && git sparse-checkout set $BMS_SPARSE ) \
      || { echo "ERROR: could not restrict the clone to: $BMS_SPARSE" >&2; exit 1; }
  else
    git clone --depth 1 "$BMS_REPO" "$BMS_DIR" \
      || { echo "ERROR: could not clone $BMS_REPO" >&2; exit 1; }
  fi
fi

exec bash "$BMS_DIR/prepare_traces.sh" "$@"
