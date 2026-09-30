#!/usr/bin/env bash
# 00 -- compare the saved exercise output with the reference.
# Never runs the simulator or modifies output files.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
cd "$ROOT"

REPORT="$HERE/output/Summary.csv"
REFERENCE="$HERE/expected/Summary.csv"
if [ ! -f "$REPORT" ]; then
  echo "[FAIL] no saved run at $REPORT"
  echo "       Run the command in README.md step 2 first, then run this check again."
  exit 1
fi
[ -f "$REFERENCE" ] || { echo "[FAIL] reference missing: $REFERENCE"; exit 1; }

# Ignore Windows CRLF differences; preserve the reference's exact values and order.
if diff -u <(tr -d '\r' < "$REFERENCE") <(tr -d '\r' < "$REPORT"); then
  echo "[PASS] 00 first run: Summary.csv matches the reference"
  exit 0
fi
echo "[FAIL] 00 first run: output/Summary.csv differs from the reference"
echo "       Check that the run completed and used the shipped MESI/TDM baseline."
if ! git -C "$ROOT" diff HEAD --quiet -- configuration 2>/dev/null; then
  echo "       configuration/ has local changes; inspect them with git diff HEAD -- configuration/"
fi
exit 1
