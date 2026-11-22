# Step 8 -- the BookSim comparison (docs/NoC_Test_Plan.md §11, docs/NoC_BookSim_Validation.md).
# BookSim 2 (github.com/booksim/booksim2, pinned to BS_COMMIT) is cloned and built into
# test/network/out/booksim2 (gitignored): natively on Linux, under WSL on Windows. bison and flex come
# from PATH, then $HOME/local/bin; when neither has them the BookSim tests are XFAIL.
#   S8-BOOKSIM      4x4 mesh, DOR, 1 VC, uniform: saturation throughput (test/network/booksim/mesh44_dor.cfg)
#   S8-PATTERN-MAP  8x8: every permutation source reaches the same destination on both simulators
#   S8-88-<routing> 8x8 mesh, 2 VCs, six patterns: zero-load latency, saturation throughput, knee and
#                   latency curve against the criteria C1-C4 (test/network/booksim/compare.py, mesh88.cfg)
# Saturation in S8-BOOKSIM on both sides: the first offered rate whose accepted rate is below 0.9 of it
# (the S4-SAT-DEF criterion), in flits per cycle per node.
BS_COMMIT=28f43299f1706a3160ffac721ca461d74eb6e618

# bs_mode: "native", "wsl" or "" (no BookSim). Builds it when missing; the build log is $1/.build.
bs_mode() {
  local log="$1/.build" bs="$OUT/booksim2/src/booksim"
  if command -v wsl.exe > /dev/null 2>&1; then
    local lr; lr="/mnt/$(echo "$ROOT" | sed 's#^/\([a-zA-Z]\)/#\L\1/#')"
    if [ ! -f "$bs" ]; then
      wsl.exe -e bash -lc "export PATH=\$HOME/local/bin:\$PATH; cd '$lr/test/network/out' && (test -d booksim2 || git clone -q https://github.com/booksim/booksim2.git) && cd booksim2 && git checkout -q $BS_COMMIT && cd src && make -j8 > build.log 2>&1; ls -la booksim" > "$log" 2>&1
    fi
    [ -f "$bs" ] && echo wsl
    return 0
  fi
  if [ ! -x "$bs" ]; then
    (
      export PATH="$PATH:$HOME/local/bin"
      for t in bison flex git make g++; do command -v "$t" > /dev/null 2>&1 || { echo "missing $t"; exit 1; }; done
      cd "$OUT" && { [ -d booksim2 ] || git clone -q https://github.com/booksim/booksim2.git; } && cd booksim2 && git checkout -q "$BS_COMMIT" && cd src && make -j8
    ) > "$log" 2>&1
  fi
  # a second copy with the one-line Valiant fix (docs/NoC_BookSim_Validation.md s6.1): stock valiant_mesh
  # lets a phase-0 packet that passes its destination router take a phase-1 VC, and deadlocks under load
  local fx="$OUT/booksim2-valfix/src"
  if [ -x "$bs" ] && [ ! -x "$fx/booksim" ]; then
    (
      rm -rf "$OUT/booksim2-valfix" && cp -r "$OUT/booksim2" "$OUT/booksim2-valfix" && cd "$fx" &&
      sed -i '1314s/if(r->GetID() != f->dest) {/if((r->GetID() != f->dest) || (f->ph == 0)) {/' routefunc.cpp &&
      grep -q 'if((r->GetID() != f->dest) || (f->ph == 0)) {' routefunc.cpp && make -j8
    ) >> "$log" 2>&1 || rm -f "$fx/booksim"
  fi
  [ -x "$bs" ] && echo native
  return 0
}
# bs_run <mode> <cfg path relative to ROOT> <args...>: BookSim's output on stdout
bs_run() {
  local mode="$1" cfg="$2"; shift 2
  if [ "$mode" = wsl ]; then
    local lr; lr="/mnt/$(echo "$ROOT" | sed 's#^/\([a-zA-Z]\)/#\L\1/#')"
    wsl.exe -e bash -lc "cd '$lr/test/network/out/booksim2/src' && ./booksim '$lr/$cfg' $*"
  else
    "$OUT/booksim2/src/booksim" "$ROOT/$cfg" "$@"
  fi
}

step8() {
  local O="$OUT/s8" t0; mkdir -p "$O"
  local mode; mode=$(bs_mode "$O")
  s8_mesh44 "$O" "$mode"
  s8_mesh88 "$O" "$mode"
}

s8_mesh44() {
  local O="$1" mode="$2" t0
  # ---- Octopus: uniform 4x4, 5-flit packets, offered rates 10..200 packets per 1000 ticks, with the
  # pipeline mapped onto BookSim's (mesh44_dor.cfg): router_latency 3 = VA + SA + ST, credit_latency 4
  # = SA + ST + credit_delay, because a BookSim buffer slot is held until the flit has traversed the
  # switch while an Octopus slot is released when the flit is forwarded. The saturation throughput is
  # the accepted rate at the first offered rate past saturation, in flits per cycle per node.
  t0=$(now); local G44="-p interconnect.topology(s)=grid2d -p interconnect.dims(vi)=4,4 -p interconnect.router_latency(i)=3 -p interconnect.credit_latency(i)=4"
  local r osat="" obase="" oacc=""; : > "$O/octopus.txt"
  for r in 10 20 30 40 50 60 70 80 90 100 110 120 130 140 160 180 200; do
    local d="$O/oct-$r"; rm -rf "${d:?}"; mkdir -p "$d"
    "$BIN" -s NetworkOnly $G44 -p "generator[*].pattern(s)=uniform" -p "generator[*].packets(i)=3125" -p "generator[*].rate(i)=$r" -o "$(wpath "$d")" > "$d/.stdout" 2> "$d/.stderr" || { osat=exit; break; }
    local fi le acc mean; fi=$($PY "$HERE/netstat.py" "$d/NetworkStats.csv" classtime 2 1); le=$($PY "$HERE/netstat.py" "$d/NetworkStats.csv" classtime 2 2); mean=$($PY "$HERE/netstat.py" "$d/NetworkStats.csv" class 2 2)
    acc=$($PY -c "print(round(3125*1000/($le-$fi),1))"); [ -z "$obase" ] && obase=$mean
    echo "offered $r accepted $acc mean $mean" >> "$O/octopus.txt"
    if $PY -c "import sys; sys.exit(0 if $acc < 0.9*$r else 1)"; then osat=$r; oacc=$($PY -c "print(round($acc*5/1000, 3))"); break; fi
  done
  local oflits; oflits=$($PY -c "print(round(${osat:-0}*5/1000, 3))")
  # ---- BookSim
  if [ -n "$mode" ]; then
    : > "$O/booksim.txt"; local bsat="" bbase=""
    local ir; for ir in 0.05 0.10 0.15 0.20 0.25 0.30 0.35 0.40 0.45 0.50 0.55 0.60 0.65 0.70 0.80 0.90; do
      bs_run "$mode" test/network/booksim/mesh44_dor.cfg "injection_rate=$ir" > "$O/bs-$ir.log" 2>&1
      local acc lat; acc=$(grep "Accepted flit rate average" "$O/bs-$ir.log" | tail -1 | sed 's/.*= *\([0-9.]*\).*/\1/'); lat=$(grep "Packet latency average" "$O/bs-$ir.log" | tail -1 | sed 's/.*= *\([0-9.]*\).*/\1/')
      local unstable=0; grep -q -i "unstable\|aborting" "$O/bs-$ir.log" && unstable=1
      [ -z "$bbase" ] && [ -n "$lat" ] && bbase=$lat
      echo "offered $ir accepted ${acc:-?} latency ${lat:-?} unstable $unstable" >> "$O/booksim.txt"
      if [ $unstable = 1 ] || { [ -n "$acc" ] && $PY -c "import sys; sys.exit(0 if $acc < 0.9*$ir else 1)"; }; then bsat=$ir; break; fi
    done
    local bacc; bacc=$(grep "offered $bsat " "$O/booksim.txt" | sed 's/.*accepted \([0-9.]*\).*/\1/')
    if [ -n "$bsat" ] && [ -n "$oacc" ] && [ "$osat" != exit ]; then
      local rel; rel=$($PY -c "a=$oacc; b=$bacc; print(round(abs(a-b)/max(a,b), 3))")
      if $PY -c "import sys; sys.exit(0 if $rel <= 0.10 else 1)"; then pass S8-BOOKSIM "saturation throughput Octopus $oacc (offered $oflits) vs BookSim $bacc (offered $bsat) flits/cycle/node; rel. diff $rel" "within 10%" "$(elapsed "$t0")"
      else fail S8-BOOKSIM "saturation throughput Octopus $oacc (offered $oflits) vs BookSim $bacc (offered $bsat) flits/cycle/node; rel. diff $rel" "within 10%" "$(elapsed "$t0")"; fi
    else fail S8-BOOKSIM "Octopus ${osat} BookSim ${bsat:-no saturation found}" "both saturation rates" "$(elapsed "$t0")"; fi
  else
    xfail S8-BOOKSIM "BookSim is not built on this machine (see $O/.build); Octopus saturates at offered $osat packets/1000 ticks = $oflits flits/cycle/node (unloaded mean $obase); the reference for a 4x4 mesh, DOR, 1 VC, 4-flit buffers, 5-flit packets under uniform traffic is about 0.4-0.5 flits/cycle/node"
  fi
}

# The 8x8 matrix: five routings x six patterns, driven by compare.py (standard-library Python). It
# calls BookSim directly, so it needs the native build; under WSL it is XFAIL.
s8_mesh88() {
  local O="$1/mesh88" mode="$2" t0; t0=$(now)
  local ids="S8-PATTERN-MAP S8-88-XY S8-88-O1TURN S8-88-ROMM S8-88-VALIANT S8-88-ADAPTIVE" id
  if [ "$mode" != native ]; then
    for id in $ids; do xfail "$id" "needs BookSim built natively (mode '${mode:-none}', see $1/.build)"; done; return
  fi
  rm -rf "${O:?}"; mkdir -p "$O"
  local fx="$OUT/booksim2-valfix/src/booksim" vb=""
  local routings="XY,O1TURN,ROMM,Valiant,AdaptiveMinimal"
  if [ -x "$fx" ]; then vb="--valiant-booksim $fx"; else routings="XY,O1TURN,ROMM,AdaptiveMinimal"; fi
  "$PY" "$HERE/booksim/compare.py" --octopus "$BIN" --booksim "$OUT/booksim2/src/booksim" $vb --routings "$routings" --cfg "$HERE/booksim/mesh88.cfg" \
        --out "$O" > "$O/compare.txt" 2> "$O/compare.err"
  local line
  line=$(grep '^PATTERNMAP ' "$O/compare.txt")
  case "$line" in
    "PATTERNMAP PASS"*) local pm="${line#PATTERNMAP PASS }"; pass S8-PATTERN-MAP "${pm//,/;}" "same destinations" "$(elapsed "$t0")";;
    *) fail S8-PATTERN-MAP "${line:-no result (see $O/compare.err)}" "same destinations" "$(elapsed "$t0")";;
  esac
  local r tid
  for r in XY O1TURN ROMM Valiant AdaptiveMinimal; do
    case $r in Valiant) tid=S8-88-VALIANT;; AdaptiveMinimal) tid=S8-88-ADAPTIVE;; *) tid=S8-88-$r;; esac
    if [ $r = Valiant ] && [ -z "$vb" ]; then xfail "$tid" "the patched BookSim copy for Valiant did not build (see $1/.build); stock valiant_mesh deadlocks at saturation"; continue; fi
    line=$(grep "^ROUTING $r " "$O/compare.txt"); local detail="${line#ROUTING $r * }"; detail="${detail//,/;}"
    case "$line" in
      "ROUTING $r PASS"*) pass "$tid" "$detail" "C1-C4 on every pattern" "$(elapsed "$t0")";;
      "ROUTING $r KNOWN"*) xfail "$tid" "only the misses documented in docs/NoC_BookSim_Validation.md s7: $detail";;
      "ROUTING $r REPORT"*)
        if echo "$detail" | grep -q "not:"; then xfail "$tid" "report only (BookSim min_adapt and Octopus AdaptiveMinimal choose outputs differently, docs/NoC_BookSim_Validation.md): $detail"
        else pass "$tid" "$detail" "C1-C4 on every pattern" "$(elapsed "$t0")"; fi;;
      *) fail "$tid" "${line:-no result (see $O/compare.err)}" "C1-C4 on every pattern" "$(elapsed "$t0")";;
    esac
  done
}
