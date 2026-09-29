#!/usr/bin/env bash
#
# post-create.sh -- build the simulator and prove the environment works. Runs once,
# when the codespace is created or the container rebuilt. About 52 s on the 4-core
# machine attendees get, plus one short simulation.
#
# Output is NOT hidden: when this fails, the reason has to be in the creation log,
# not swallowed by >/dev/null. The one-line summary at the end is what a person
# reads; everything above it is what we read when it goes wrong.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

echo "== building Octopus"
if ! cmake -S . -B build -DCMAKE_BUILD_TYPE=Release; then
  echo "post-create: cmake configure FAILED -- run 'bash .devcontainer/post-create.sh' to retry" >&2
  exit 1
fi
if ! cmake --build build -j"$(nproc)"; then
  echo "post-create: build FAILED -- run 'bash .devcontainer/post-create.sh' to retry" >&2
  exit 1
fi

bash scripts/check_environment.sh
