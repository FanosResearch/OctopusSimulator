#!/usr/bin/env bash
# Self-test of run.sh's completion handling (docs/Shared_Controller_Defects_Plan.md, runner follow-up): the
# runner is copied into a scratch tree whose step files stub the costly parts (build, the reference generators,
# the regression), and each mode's exit status and abort marker are checked. No simulator runs.
#   bash test/network/selftest.sh
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "${T:?}"' EXIT
mkdir -p "$T/test/network/steps"
cp "$HERE/run.sh" "$T/test/network/run.sh"
cat > "$T/test/network/steps/stepZ_stub.sh" <<'STUB'
PY=python3
build() { [ "${STUB_MODE:-}" = buildfail ] && { echo "BUILD FAILED (stub)"; exit 2; }; echo "build ok (stub)"; }
make_refs() { echo "make_refs (stub)"; }
tilelink_refs() { echo "tilelink_refs (stub)"; }
regression() {
  case "${STUB_MODE:-ok}" in
    ok) pass STUB-OK;;
    fail) fail STUB-FAIL x y;;
    unbound) echo "$STUB_UNDEFINED_VARIABLE";;
    sleep) sleep 30;;
  esac
}
STUB
fails=0
check() { # name mode args... ; expected status (0 / nonzero / exact), abort marker expected (yes/no)
  local name="$1" want_st="$2" want_abort="$3"; shift 3
  local out st ab
  out=$(cd "$T" && "$@" 2>&1); st=$?
  echo "$out" | grep -q "RUN ABORTED" && ab=yes || ab=no
  local ok=1
  case "$want_st" in nonzero) [ "$st" != 0 ] || ok=0;; *) [ "$st" = "$want_st" ] || ok=0;; esac
  [ "$ab" = "$want_abort" ] || ok=0
  if [ $ok = 1 ]; then echo "PASS RUNNER-$name (status $st, abort marker $ab)"
  else echo "FAIL RUNNER-$name: status $st (expected $want_st), abort marker $ab (expected $want_abort)"; echo "$out" | sed 's/^/    /'; fails=$((fails+1)); fi
}
check REFS 0 no env STUB_MODE=ok bash test/network/run.sh refs
grep -q "tilelink-refs)" "$T/test/network/run.sh" && check TILELINK-REFS 0 no env STUB_MODE=ok bash test/network/run.sh tilelink-refs
check REG-OK 0 no env STUB_MODE=ok bash test/network/run.sh reg
check REG-FAIL nonzero no env STUB_MODE=fail bash test/network/run.sh reg          # complete, with a FAIL
check SCRIPT-ERROR nonzero yes env STUB_MODE=unbound bash test/network/run.sh reg  # stops under set -u
check BUILD-FAIL 2 yes env STUB_MODE=buildfail bash test/network/run.sh reg
check USAGE 2 no env STUB_MODE=ok bash test/network/run.sh no-such-mode
# interrupted: SIGTERM while the regression runs
( cd "$T" && STUB_MODE=sleep bash test/network/run.sh reg > "$T/term.out" 2>&1 ) & pid=$!
sleep 2; pkill -TERM -P "$pid" -f "test/network/run.sh" 2>/dev/null; kill -TERM "$pid" 2>/dev/null; wait "$pid"; st=$?
if [ "$st" != 0 ] && grep -q "RUN ABORTED" "$T/term.out"; then echo "PASS RUNNER-INTERRUPTED (status $st, abort marker yes)"
else echo "FAIL RUNNER-INTERRUPTED: status $st, $(grep -c 'RUN ABORTED' "$T/term.out") abort marker(s)"; sed 's/^/    /' "$T/term.out"; fails=$((fails+1)); fi
echo "----"; echo "$fails failure(s)"
[ "$fails" = 0 ]
