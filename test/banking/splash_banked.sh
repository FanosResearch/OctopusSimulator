#!/usr/bin/env bash
# splash_banked.sh -- the SPLASH-2 subset of the design doc s8.1: snooping MOESI on a 2-bank LLC,
# line and page interleave, with the banking properties of s8.3 checked on every run.
#
#   test/banking/splash_banked.sh <outdir> [splash_dir]
#
# splash_dir defaults to BMs/splash (./get_benchmarks.sh inflates it). The subset is the six
# smaller SPLASH traces (2 to 7 M accesses per core, about 1 to 3 minutes each); BENCHES overrides
# it. Each trace is about 3 GB while it is checked, then deleted. Exits non-zero on any failure.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$1"; SPLASH="${2:-$ROOT/BMs/splash}"
[ -d "$SPLASH/ocean" ] || { echo "no SPLASH traces in $SPLASH (run ./get_benchmarks.sh)" >&2; exit 2; }
BMDIR="$SPLASH" BENCHES="${BENCHES:-ocean water_nsquared water_spatial fmm lu_non_contig cholesky}" \
  ONLY=snoop-moesi PROPS="${PROPS-1}" CAP="${CAP:-7200}" JOBS="${JOBS:-12}" "$ROOT/test/banking/banked.sh" "$OUT" 2
