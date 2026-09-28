#!/usr/bin/env bash
#
# on-create.sh -- everything slow or network-bound, run once when the container is
# created. Codespaces runs this during a PREBUILD too, so the result is baked into
# the prebuilt image and an attendee creating a codespace does not wait for it.
#
# Deliberately does not touch the source tree: post-create.sh builds.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

echo "== system packages"
sudo apt-get update -qq
# xz-utils: BMs/prepare_traces.sh inflates the compressed SPLASH archives with it.
# python3-pip: the analysis and visualizer scripts.
sudo apt-get install -y -qq --no-install-recommends xz-utils python3-pip >/dev/null

echo "== python packages"
# Ubuntu 24.04 marks the system interpreter externally managed (PEP 668). This is a
# throwaway container, so installing into it is fine and keeps plain `python3` working
# for every script in the repo; fall back for images without that restriction.
python3 -m pip install --quiet --break-system-packages duckdb numpy matplotlib \
  || python3 -m pip install --quiet duckdb numpy matplotlib

echo "== benchmark traces (EEMBC only)"
# The SPLASH archives are ~651 MB of the ~710 MB benchmark repository and no tutorial
# exercise runs them, so the container fetches EEMBC alone. To add SPLASH later:
#   rm -rf BMs && ./get_benchmarks.sh
BMS_SPARSE="eembc-traces" ./get_benchmarks.sh

echo "on-create: done"
