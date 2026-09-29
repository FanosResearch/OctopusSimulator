#!/usr/bin/env bash
# apply.sh -- apply the reference solution for one exercise, so you can carry on with
# the next one.   Usage:  bash tutorial/solutions/apply.sh <exercise>
# where <exercise> is the leaf folder name, e.g. 01-arbiter, 02-protocol, 03-three-levels.
#
# Patches are produced from the tutorial-solutions branch; each one is checked in CI
# by apply -> build -> the exercise's check.sh, so a patch that stops applying is
# caught before the session, not during it.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
cd "$ROOT"

if [ $# -ne 1 ]; then
  echo "usage: bash tutorial/solutions/apply.sh <exercise>"; echo "available:"
  ls "$HERE"/*.patch 2>/dev/null | sed 's#.*/##; s/\.patch$//; s/^/  /' || echo "  (no patches yet)"
  exit 2
fi
p="$HERE/$1.patch"
[ -f "$p" ] || { echo "no solution patch for '$1' (looked for $p)"; exit 1; }

if ! git apply --check "$p" 2>/dev/null; then
  echo "the patch does not apply cleanly on your tree."
  echo "if you have partial edits for this exercise, stash or discard them first:"
  echo "    git stash        # keep them"
  echo "    git checkout -- .   # discard them"
  exit 1
fi
git apply "$p" && echo "applied $1. Rebuild if it touched C++:  cmake --build build -j\$(nproc)"
