#!/usr/bin/env bash
#
# octoviz.sh -- one entry point for the Octopus timeline visualizer (docs/Visualizer.md):
# simulate a workload with the raw event trace on, convert the run for the viewer, and
# serve it in the browser. Everything runs locally.
#
# Usage:
#   ./octoviz.sh run     <workload_dir> [-- <extra simulator args>]   # simulate with OCTOPUS_TRACE
#   ./octoviz.sh convert <root_dir>                                    # recursively find latency CSVs -> Parquet
#   ./octoviz.sh serve   <root_dir>                                     # convert missing runs, then serve
#   ./octoviz.sh view    <workload_dir> [-- <extra simulator args>]    # run + convert + serve + open browser
#
# <workload_dir> holds the core traces (trace_C0..C3.trc.shared); the simulator writes
# newLogger/ inside it (created here), the trace goes to <workload_dir>/trace.bin, and the
# Parquet files go next to the reports. Extra simulator args after "--" are passed verbatim,
# e.g.  -- -p "cpu[*].m_number_of_OoO_requests(i)=1" -p "llc_controller.perfect_llc(i)=1"
#
# Environment overrides:
#   SYSTEM=<name>     system configuration (default MultiCoreSystem)
#   TRACE=0           run without the raw trace (request rows only; no all-traffic lanes,
#                     no coherence table)
#   WINDOW=t0:t1      trace only this cycle range (OCTOPUS_TRACE_WINDOW) -- for giant runs
#   KEEP_TRACE=1      keep trace.bin after conversion (default: deleted; the Parquet has it all)
#   PORT=<n>          server port (default 8765)
#   TIMEOUT=<sec>     wall-clock cap for the simulation (default 0 = run to completion)
#   BIN=<path>        simulator executable (default build/Octopus_Simulator[.exe])
#   PY=<python>       interpreter (default: the first of python3/python/py that runs;
#                     needs: pip install duckdb numpy)
#
# Examples:
#   ./octoviz.sh view BMs/eembc-traces/a2time01-trace
#   WINDOW=20000000:20100000 ./octoviz.sh view BMs/splash/water_nsquared
#   ./octoviz.sh serve results/pcc_par            # all runs converted by sweeps/trace_pcc.sh
#
set -u
OCTOPUS_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SYSTEM="${SYSTEM:-MultiCoreSystem}"; TRACE="${TRACE:-1}"; WINDOW="${WINDOW:-}"; KEEP_TRACE="${KEEP_TRACE:-0}"
PORT="${PORT:-8765}"; TIMEOUT="${TIMEOUT:-0}"
# Python: many Linux distributions ship only python3, not python.
if [ -z "${PY:-}" ]; then
  # first candidate that actually RUNS: many Linux distributions have no `python`,
  # and on Windows `python3` can be the Store stub that exits without running anything.
  for _p in python3 python py; do
    if "$_p" -c "import sys" >/dev/null 2>&1; then PY="$_p"; break; fi
  done
  PY="${PY:-python3}"
fi
BIN="${BIN:-$OCTOPUS_ROOT/build/Octopus_Simulator.exe}"; [ -f "$BIN" ] || BIN="$OCTOPUS_ROOT/build/Octopus_Simulator"
TOOLS="$OCTOPUS_ROOT/tools/octoviz"
# MinGW runtime DLLs next to the simulator (Windows builds); harmless elsewhere
MINGW="/c/Users/moham/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
[ -d "$MINGW" ] && export PATH="$MINGW:$OCTOPUS_ROOT/build:$PATH"

die(){ echo "octoviz.sh: $*" >&2; exit 1; }
need_py(){ "$PY" -c "import duckdb, numpy" 2>/dev/null || die "python needs duckdb and numpy:  $PY -m pip install duckdb numpy"; }
winpath(){ cygpath -m "$1" 2>/dev/null || echo "$1"; }
open_url(){ local u="$1"; (command -v start >/dev/null && start "" "$u") 2>/dev/null || cmd.exe /c start "" "$u" 2>/dev/null || xdg-open "$u" 2>/dev/null || open "$u" 2>/dev/null || echo "open $u in a browser"; }

# The address to give a human. In a Codespace the server's own localhost URL is useless
# -- the browser is on another machine -- and the forwarded one has to be built from the
# environment. Auto-opening cannot be relied on there either: the editor's popup is
# blocked as often as not, so print the URL and let the terminal make it clickable.
viewer_url(){
  if [ -n "${CODESPACE_NAME:-}" ] && [ -n "${GITHUB_CODESPACES_PORT_FORWARDING_DOMAIN:-}" ]; then
    printf 'https://%s-%s.%s/' "$CODESPACE_NAME" "$PORT" "$GITHUB_CODESPACES_PORT_FORWARDING_DOMAIN"
  else
    printf 'http://localhost:%s/' "$PORT"
  fi
}
in_codespace(){ [ -n "${CODESPACE_NAME:-}" ]; }

cmd_run(){
  local d="$1"; shift; [ -d "$d" ] || die "no such workload dir: $d"
  [ -f "$BIN" ] || die "simulator not built ($BIN); see README.md 'Building Octopus'"
  ls "$d"/trace_C*.trc.shared >/dev/null 2>&1 || die "$d has no trace_C*.trc.shared core traces (run get_benchmarks.sh?)"
  mkdir -p "$d/newLogger"; rm -f "$d/newLogger"/*.csv "$d/newLogger"/*.parquet "$d/trace.bin" "$d/trace.bin.names"
  local envs=() cap=()
  [ "$TRACE" = "1" ] && envs+=("OCTOPUS_TRACE=$(winpath "$d")/trace.bin") && [ -n "$WINDOW" ] && envs+=("OCTOPUS_TRACE_WINDOW=$WINDOW")
  [ "$TIMEOUT" != "0" ] && cap=(timeout "${TIMEOUT}s")
  echo "== simulating $d (system $SYSTEM${TRACE:+, trace on}${WINDOW:+, window $WINDOW}) $*"
  local s=$(date +%s)
  env "${envs[@]}" "${cap[@]}" "$BIN" -s "$SYSTEM" -p "workload_path(s)=$(winpath "$d")/" "$@" > "$d/.octoviz.out" 2>&1
  local rc=$?
  [ "$rc" -eq 124 ] && echo "   (wall-clock cap hit: the reports so far are still usable)"
  [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ] && { tail -5 "$d/.octoviz.out"; die "simulator exited with $rc (see $d/.octoviz.out)"; }
  grep -q "Average Latency" "$d/newLogger/LatencyReport_C0.csv" 2>/dev/null || [ "$rc" -eq 124 ] || die "no report written (see $d/.octoviz.out)"
  echo "   done in $(( $(date +%s) - s )) s; trace $( [ -f "$d/trace.bin" ] && stat -c %s "$d/trace.bin" || echo 0 ) B"
}

cmd_convert(){
  local root="$1"; [ -d "$root" ] || die "no such dir: $root"
  local mode="${2:-all}"
  need_py
  local report d trace count=0
  local -A seen=()
  local args=()
  while IFS= read -r -d '' report; do
    d="$(dirname "$report")"
    [ -n "${seen[$d]:-}" ] && continue
    seen["$d"]=1
    [ "$mode" = "missing" ] && [ -f "$d/octoviz.parquet" ] && continue
    if [ "$mode" = "missing" ] && [ "$count" -eq 0 ]; then
      echo
      echo "Converting data to visualizer format, please wait..."
      echo
    fi
    args=(); trace=""
    # Custom output directories keep traces beside the CSVs. Legacy runs
    # put trace.bin one level above newLogger/.
    if [ -f "$d/trace.bin" ]; then
      trace="$d/trace.bin"
    elif [ "$(basename "$d")" = "newLogger" ] && [ -f "$d/../trace.bin" ]; then
      trace="$d/../trace.bin"
    fi
    [ -n "$trace" ] && args+=(--trace "$trace")
    [ -n "$WINDOW" ] && [ -n "$trace" ] && args+=(--t0 "${WINDOW%%:*}" --t1 "${WINDOW##*:}")
    echo "== converting $d ${args[*]:-}"
    "$PY" "$TOOLS/convert.py" "$d" "${args[@]}" || die "conversion failed: $d"
    if [ "$mode" = "all" ] && [ "$KEEP_TRACE" != "1" ] && [ -n "$trace" ]; then rm -f "$trace"; fi
    count=$((count+1))
  done < <(find "$root" -type f -name 'LatencyReport_C*.csv' -print0 | sort -z)
  if [ "$count" -eq 0 ]; then
    [ "$mode" = "missing" ] && return 0
    die "no LatencyReport_C*.csv found under $root"
  fi
  echo "== converted $count run(s)"
}

cmd_serve(){
  local root="$1"; [ -d "$root" ] || die "no such dir: $root"
  need_py
  cmd_convert "$root" missing
  find "$root" -name octoviz.parquet 2>/dev/null | grep -q . || die "no converted run (octoviz.parquet) under $root"
  local url; url="$(viewer_url)"
  echo "== serving $root  (Ctrl-C to stop)"
  echo "==   $url"
  if in_codespace; then
    # Ctrl-click (or Cmd-click) the line above. The first load redirects through
    # github.com to authenticate the forwarded port, which is why the editor's
    # embedded preview pane cannot show it -- use a real browser tab.
    echo "==   ctrl-click the URL, or use the Ports panel (port $PORT, globe icon)"
  else
    open_url "$url" &
  fi
  exec "$PY" "$TOOLS/server.py" --root "$root" --port "$PORT"
}

case "${1:-}" in
  run)     shift; d="${1:?workload_dir}"; shift; [ "${1:-}" = "--" ] && shift; cmd_run "$d" "$@";;
  convert) shift; cmd_convert "${1:?root_dir}";;
  serve)   shift; cmd_serve "${1:?root_dir}";;
  view)    shift; d="${1:?workload_dir}"; shift; [ "${1:-}" = "--" ] && shift; cmd_run "$d" "$@"; cmd_convert "$d"; cmd_serve "$d";;
  *) sed -n '2,36p' "$0" | sed 's/^# \{0,1\}//'; exit 1;;
esac
