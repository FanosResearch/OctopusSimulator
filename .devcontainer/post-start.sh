#!/usr/bin/env bash
#
# post-start.sh -- runs every time the codespace starts, including after a stop/resume,
# where postCreateCommand does NOT run again.
#
# Its whole job is to make the environment self-healing. A missing binary is the state
# an attendee lands in when a create was interrupted, when a build was cancelled, or
# after a `--clean-first` that did not finish -- and the failure they see is a
# confusing "No such file or directory" from a script that never mentions the build.
# Cheap when there is nothing to do: one test, no output.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

[ -x build/Octopus_Simulator ] && exit 0

echo "== simulator missing; building it (about 52 s)"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)" \
  || { echo "post-start: build FAILED -- see above" >&2; exit 1; }
echo "== simulator rebuilt: build/Octopus_Simulator"
