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
#   BMS_REPO=<url>   override the benchmark repository URL
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
  git clone --depth 1 "$BMS_REPO" "$BMS_DIR" \
    || { echo "ERROR: could not clone $BMS_REPO" >&2; exit 1; }
fi

exec bash "$BMS_DIR/prepare_traces.sh" "$@"
