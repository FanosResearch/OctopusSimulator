#!/usr/bin/env bash
#
# post-create.sh -- build the simulator and prove the environment works. Cheap by
# design: a clean build is about 11 s on 8 cores, and the smoke test one short run.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

echo "== building Octopus"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j"$(nproc)" >/dev/null

bash scripts/check_environment.sh
