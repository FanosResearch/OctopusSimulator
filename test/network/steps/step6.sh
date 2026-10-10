# Step 6 -- degenerate equivalence with the legacy NoC (docs/NoC_Test_Plan.md §9, spec §17).
NETNOC="-s MultiCoreSystem_Mesh -c MultiCoreSystem_NetNoC"
step6() {
  local O="$OUT/s6" t0 t; mkdir -p "$O"
  t0=$(now)
  for t in $EEMBC; do rm -rf "${O:?}/netnoc/${t:?}"; mkdir -p "$O/netnoc/$t"; done
  for t in $EEMBC; do
    ( OCTOPUS_HANG_DUMP=200000 timeout 1800 "$BIN" $NETNOC -p "workload_path(s)=$(W $t)/" -o "$(wpath "$O/netnoc/$t")" > "$O/netnoc/$t/.stdout" 2> "$O/netnoc/$t/.stderr"; echo $? > "$O/netnoc/$t/.rc" ) &
  done; wait
  local ok=1 detail="" ident=""
  for t in $EEMBC; do
    local rc; rc=$(cat "$O/netnoc/$t/.rc")
    local got want; got=$(sumdir "$O/netnoc/$t"); want=$(cat "$REF/legacy-mesh/$t.md5" 2>/dev/null || echo missing)
    if [ "$rc" != 0 ]; then ok=0; detail="$detail $t(rc=$rc)"; elif [ "$got" != "$want" ]; then ok=0; detail="$detail $t"; else ident="$ident $t"; fi
  done
  # the user accepted the documented divergence (test/network/ref/s6-accepted.txt): report it as XFAIL
  local accepted=0; [ -f "$REF/s6-accepted.txt" ] && accepted=1
  if [ $ok = 1 ]; then pass S6-IDENTITY "10/10 byte-identical to the legacy NoC" "identical" "$(elapsed "$t0")"
  else
    if [ $accepted = 1 ]; then xfail S6-IDENTITY "differs on$detail; divergence accepted by the user (ref/s6-accepted.txt)"
    else fail S6-IDENTITY "differs:$detail${ident:+ (identical:$ident)}" "identical" "$(elapsed "$t0")"; fi
    # ---- S6-FALLBACK: per-core Summary deltas against the legacy run of the regression suite
    local fb="$O/fallback.csv"; echo "trace,core,column,legacy,network,delta" > "$fb"
    local worst_ok=1 finish_ok=1
    for t in $EEMBC; do
      [ -f "$OUT/reg-mesh/$t/Summary.csv" ] || sim "$OUT/reg-mesh/$t" -s MultiCoreSystem_Mesh -p "workload_path(s)=$(W $t)/"
      $PY - "$t" "$OUT/reg-mesh/$t/Summary.csv" "$O/netnoc/$t/Summary.csv" "$fb" <<'EOF'
import csv, sys
t, a, b, out = sys.argv[1:]
def load(p):
    rows = list(csv.reader(open(p, newline='')))
    hdr = rows[0]; d = {}
    for r in rows[1:]:
        if len(r) == len(hdr) and r[0].isdigit(): d[r[0]] = dict(zip(hdr, r))
    return hdr, d
hdr, A = load(a); _, B = load(b)
worst = 0; finish = 0
with open(out, 'a', newline='') as f:
    w = csv.writer(f)
    for core in sorted(A, key=int):
        for col in hdr[1:]:
            try: x = float(A[core][col]); y = float(B.get(core, {}).get(col, 'nan'))
            except ValueError: continue
            if x != y: w.writerow([t, core, col, A[core][col], B.get(core, {}).get(col, '')[:12], y - x])
            if col.startswith('Worst-case') and x != y: worst = 1
            if col == 'Finish Cycle' and abs(y - x) > 1: finish = 1
print(worst, finish)
EOF
    done > "$O/.fb"
    grep -q "^1 " "$O/.fb" && worst_ok=0; grep -q " 1$" "$O/.fb" && finish_ok=0
    local nd; nd=$(( $(wc -l < "$fb") - 1 ))
    if [ $worst_ok = 1 ] && [ $finish_ok = 1 ]; then pass S6-FALLBACK "worst cases identical, finish within 1 tick ($nd column deltas)" "see $fb"
    elif [ $accepted = 1 ]; then xfail S6-FALLBACK "$nd column deltas, accepted by the user (ref/s6-accepted.txt); deltas in $fb"
    else fail S6-FALLBACK "$nd column deltas (worst cases identical: $worst_ok, finish within 1: $finish_ok)" "the user's decision; deltas in $fb"; fi
  fi
  xfail S6-NOC-ALIAS "the NoC alias is introduced only after the user accepts S6-IDENTITY or S6-FALLBACK (spec s17.1)"
}
