#!/usr/bin/env bash
#
# on-create.sh -- everything slow or network-bound, run once when the container is
# created. Codespaces runs this during a PREBUILD too, so the result is baked into
# the prebuilt image and an attendee creating a codespace does not wait for it.
#
# Deliberately does not touch the source tree: post-create.sh builds.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

BMS_SPARSE="eembc-traces" ./get_benchmarks.sh

echo "on-create: done"
