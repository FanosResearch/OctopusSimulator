# Step 8 -- the BookSim comparison (docs/NoC_Test_Plan.md §11). BookSim 2 is built under WSL into
# test/network/out/booksim2 (clone of github.com/booksim/booksim2) with bison/flex from
# $HOME/local/bin when the distribution has none; the configuration is test/network/booksim/mesh44_dor.cfg.
# Saturation on both sides: the first offered rate whose accepted rate is below 0.9 of it (the
# S4-SAT-DEF criterion), in flits per cycle per node.
step8() {
  local O="$OUT/s8" t0; mkdir -p "$O"
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
  local lr; lr="/mnt/$(echo "$ROOT" | sed 's#^/\([a-zA-Z]\)/#\L\1/#')"
  local bs="$OUT/booksim2/src/booksim"
  if command -v wsl.exe > /dev/null 2>&1 && [ ! -f "$bs" ]; then
    wsl.exe -e bash -lc "export PATH=\$HOME/local/bin:\$PATH; cd '$lr/test/network/out' && (test -d booksim2 || git clone -q --depth 1 https://github.com/booksim/booksim2.git) && cd booksim2/src && make -j8 > build.log 2>&1; ls -la booksim" > "$O/.build" 2>&1
  fi
  if command -v wsl.exe > /dev/null 2>&1 && [ -f "$bs" ]; then
    : > "$O/booksim.txt"; local bsat="" bbase=""
    local ir; for ir in 0.05 0.10 0.15 0.20 0.25 0.30 0.35 0.40 0.45 0.50 0.55 0.60 0.65 0.70 0.80 0.90; do
      wsl.exe -e bash -lc "cd '$lr/test/network/out/booksim2/src' && ./booksim '$lr/test/network/booksim/mesh44_dor.cfg' injection_rate=$ir" > "$O/bs-$ir.log" 2>&1
      local acc lat; acc=$(grep "Accepted flit rate average" "$O/bs-$ir.log" | tail -1 | sed 's/.*= *\([0-9.]*\).*/\1/'); lat=$(grep "Packet latency average" "$O/bs-$ir.log" | tail -1 | sed 's/.*= *\([0-9.]*\).*/\1/')
      local unstable=0; grep -q -i "unstable\|aborting" "$O/bs-$ir.log" && unstable=1
      [ -z "$bbase" ] && [ -n "$lat" ] && bbase=$lat
      echo "offered $ir accepted ${acc:-?} latency ${lat:-?} unstable $unstable" >> "$O/booksim.txt"
      if [ $unstable = 1 ] || { [ -n "$acc" ] && $PY -c "import sys; sys.exit(0 if $acc < 0.9*$ir else 1)"; }; then bsat=$ir; break; fi
    done
    local bacc; bacc=$(grep "offered $bsat " "$O/booksim.txt" | sed 's/.*accepted \([0-9.]*\).*/\1/')
    if [ -n "$bsat" ] && [ -n "$oacc" ] && [ "$osat" != exit ]; then
      local rel; rel=$($PY -c "a=$oacc; b=$bacc; print(round(abs(a-b)/max(a,b), 3))")
      if $PY -c "import sys; sys.exit(0 if $rel <= 0.10 else 1)"; then pass S8-BOOKSIM "saturation throughput Octopus $oacc (offered $oflits) vs BookSim $bacc (offered $bsat) flits/cycle/node, rel. diff $rel" "within 10%" "$(elapsed "$t0")"
      else fail S8-BOOKSIM "saturation throughput Octopus $oacc (offered $oflits) vs BookSim $bacc (offered $bsat) flits/cycle/node, rel. diff $rel" "within 10%" "$(elapsed "$t0")"; fi
    else fail S8-BOOKSIM "Octopus ${osat} BookSim ${bsat:-no saturation found}" "both saturation rates" "$(elapsed "$t0")"; fi
  else
    xfail S8-BOOKSIM "BookSim is not built on this machine (see $O/.build); Octopus saturates at offered $osat packets/1000 ticks = $oflits flits/cycle/node (unloaded mean $obase); the reference for a 4x4 mesh, DOR, 1 VC, 4-flit buffers, 5-flit packets under uniform traffic is about 0.4-0.5 flits/cycle/node"
  fi
}
