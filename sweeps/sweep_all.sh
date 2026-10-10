#!/usr/bin/env bash
# Run every single-axis configuration sweep. Coherence is covered by the
# repo-root sweep_protocols.sh (kept separate; it rewrites protocol FSMs).
#
# Env: SUITE=eembc|splash  SAFETY=<sec>  BENCH=<name> (smoke one benchmark)
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
for s in arbiter replacement cache memory; do
  echo "############################## $s ##############################"
  bash "$here/sweep_$s.sh"
done
echo
echo "Coherence axis: run  ./sweep_protocols.sh --suite ${SUITE:-eembc}  (repo root)"
