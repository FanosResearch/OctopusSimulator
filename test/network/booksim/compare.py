#!/usr/bin/env python3
"""BookSim 2 versus Octopus on a k x k mesh (docs/NoC_BookSim_Validation.md). Standard library only.

  compare.py --octopus <bin> --booksim <bin> --cfg <booksim cfg> --out <dir> [--k 8] [--jobs N]
             [--routings XY,O1TURN,...] [--patterns uniform,transpose,...]

For every (routing, pattern) case it measures, on both simulators:
  zero-load latency  at 0.005 flits/cycle/node;
  saturation throughput (every source backlogged);
  the knee: the lowest offered load, on a 0.005 grid, at which the run is not stable;
  the latency curve at 0.25, 0.5, 0.7, 0.8 and 0.9 of the lower knee.
and checks the agreement criteria C1..C4 of the document. Writes:
  <out>/points.csv   every run (side, case, offered load, latency, stability, ...)
  <out>/cases.csv    one row per case (zero-load, saturation, knee, worst curve difference, verdicts)
  <out>/patternmap.csv  the destination check of the permutation patterns
and prints one line per case and per routing: "CASE <routing> <pattern> PASS|FAIL <detail>",
"ROUTING <routing> PASS|FAIL <detail>", "PATTERNMAP PASS|FAIL <detail>".

Latency quantity: Octopus's class mean (head injection into the source router to tail ejection)
plus 3 cycles (BookSim's injection channel, ejection channel and retire step) against BookSim's
network latency (head injection into the injection channel to tail retire), both over packets
whose source and destination differ.
"""
import argparse, concurrent.futures as cf, csv, math, os, re, subprocess, threading

GRID = 0.005          # flits/cycle/node per Octopus rate step (1 packet per 1000 ticks, 5 flits)
FLITS = 5
OFFSET = 3            # BookSim terminal channels, cycles
T_CURVE = 20000       # Octopus injection cycles per curve or knee point
T_ZERO = 50000        # ... at the zero-load point
T_SAT = (10000, 30000)  # two backlogged runs; the difference removes the start-up fill
RETRY_LIMIT = 0.01    # Octopus point is unstable when a generator was refused on > 1 % of the cycles
QUEUE_PACKETS = 16    # BookSim point is unstable when a source's mean queueing delay exceeds the time to
                      # inject this many packets at its offered rate (Octopus's inject_queue_depth), or when
                      # BookSim itself calls the run unstable
BS_TIMEOUT = 900      # seconds; a BookSim run that has not ended is stopped (a network deadlock)

ROUTINGS = {          # Octopus name -> (BookSim routing_function, extra Octopus overrides)
    'XY': ('dor', []),
    'O1TURN': ('xy_yx', []),
    'ROMM': ('romm', []),
    'Valiant': ('valiant', []),
    'AdaptiveMinimal': ('min_adapt', ['interconnect.escape_vc(i)=0']),
}
REPORT_ONLY = {'AdaptiveMinimal'}   # selection functions differ (see the document): reported, not gated
# Misses measured at the pinned commits and explained in the document (s7). A routing whose misses are
# a subset of these reports KNOWN (XFAIL in the suite); any other miss is FAIL. Both simulators are
# deterministic, so the set does not move between runs.
KNOWN_MISSES = {
    ('O1TURN', 'bitrev'): {'c3'},                   # Octopus's knee 0.140, BookSim's 0.160
    ('Valiant', 'uniform'): {'c1'},                 # zero-load residual up to 1.7 cycles on 12-router paths
    ('Valiant', 'bitcomp'): {'c1'},
    ('Valiant', 'tornado'): {'c1'},
    ('Valiant', 'transpose'): {'c1', 'c2'},         # and BookSim routes its 8 self packets through the network
    ('Valiant', 'bitrev'): {'c1', 'c2'},
}

# criteria (docs/NoC_BookSim_Validation.md s4)
C1_ZERO_ABS = 1.5     # cycles
C2_SAT_REL = 0.10
C3_KNEE_REL = 0.10
C4_ABS, C4_REL, C4_MAXFRAC = 2.0, 0.10, 0.8


def booksim_dest(pattern, s, k):
    """The destination of node s, transcribed from BookSim's traffic.cpp (used for fixed points only;
    the actual mapping is read back from BookSim's pair matrix)."""
    n = k * k; bits = n.bit_length() - 1
    if pattern == 'transpose':
        h = bits // 2; lo = (1 << h) - 1
        return ((s >> h) & lo) | ((s << h) & (lo << h))
    if pattern == 'bitcomp':
        return ~s & (n - 1)
    if pattern == 'bitrev':
        r = 0; x = s
        for _ in range(bits): r = (r << 1) | (x & 1); x >>= 1
        return r
    if pattern == 'shuffle':
        sh = s << 1
        return (sh & (n - 1)) | (1 if sh & n else 0)
    if pattern == 'tornado':
        x, y = s % k, s // k; o = (k + 1) // 2 - 1
        return (x + o) % k + k * ((y + o) % k)
    return None


class Runner:
    def __init__(self, a):
        self.a = a; self.k = a.k; self.N = a.k * a.k
        self.sem = threading.Semaphore(a.jobs)
        self.lock = threading.Lock()
        self.points = []
        ids = ','.join(str(i) for i in range(self.N))
        self.G = ['-p', 'interconnect.topology(s)=grid2d', '-p', f'interconnect.dims(vi)={a.k},{a.k}',
                  '-p', f'interconnect.component_ids(vi)={ids}']
        for i in range(self.N): self.G += ['-p', f'interconnect.node_map[{i}](i)={i}']
        self.G += ['-p', 'interconnect.router_latency(i)=3', '-p', 'interconnect.credit_latency(i)=4',
                   '-p', 'interconnect.vc_depth(i)=4', '-p', 'interconnect.sa_allocator(s)=input_first',
                   '-p', 'interconnect.arbiter_type(s)=FCFSArbiter', '-p', f'interconnect.vcs_per_class(i)={a.vcs}']

    def fixed(self, pattern):
        if pattern == 'uniform': return set()
        return {s for s in range(self.N) if booksim_dest(pattern, s, self.k) == s}

    def active(self, pattern):
        return self.N - len(self.fixed(pattern))

    # ---------------------------------------------------------------- Octopus
    def octopus(self, routing, pattern, rate, cycles, tag):
        d = os.path.join(self.a.out, 'oct', f'{routing}-{pattern}', f'{tag}-r{rate}-t{cycles}')
        stats = os.path.join(d, 'NetworkStats.csv')
        if not (self.a.reuse and os.path.exists(stats)):
            os.makedirs(d, exist_ok=True)
            cmd = [self.a.octopus, '-s', 'NetworkOnly'] + self.G + ['-p', f'interconnect.routing(s)={routing}']
            for o in ROUTINGS[routing][1]: cmd += ['-p', o]
            cmd += ['-p', f'generator[*].pattern(s)={pattern}', '-p', f'generator[*].rate(i)={rate}',
                    '-p', 'generator[*].packets(i)=-1', '-p', f'generator[*].cycles(i)={cycles}']
            if pattern == 'transpose':   # Octopus sends a diagonal node's packets to the first other endpoint; BookSim to itself
                for s in sorted(self.fixed(pattern)): cmd += ['-p', f'generator[{s}].pattern(s)=none']
            cmd += ['-o', d]
            with self.sem:
                with open(os.path.join(d, '.stdout'), 'w') as fo, open(os.path.join(d, '.stderr'), 'w') as fe:
                    rc = subprocess.call(cmd, stdout=fo, stderr=fe)
            if rc != 0 or not os.path.exists(stats):
                raise RuntimeError(f'Octopus failed ({rc}) in {d}')
        R = list(csv.reader(open(stats)))
        cls = [r for r in R if r[0] == 'class' and r[1] == '2'][0]
        gens = {int(r[1]): (int(r[4]), int(r[6])) for r in R if r[0] == 'generator'}
        recv = {int(r[1]): int(r[4]) for r in R if r[0] == 'received'}
        tot = [r for r in R if r[0] == 'totals' and r[1] == 'packets'][0]
        return {'packets': int(cls[4]), 'lat': float(cls[5]), 'max_retries': max(v[1] for v in gens.values()),
                'sent': {g: v[0] for g, v in gens.items()}, 'recv': recv, 'conserved': tot[4] == tot[5] and tot[6] == '0'}

    def oct_point(self, routing, pattern, rate, cycles=T_CURVE):
        r = self.octopus(routing, pattern, rate, cycles, 'pt')
        stable = r['max_retries'] <= RETRY_LIMIT * cycles
        acc = r['packets'] * FLITS / (self.active(pattern) * cycles)
        self.record('octopus', routing, pattern, rate * GRID, r['lat'] + OFFSET, r['lat'], '', acc, stable, r['max_retries'])
        return stable, r['lat'] + OFFSET

    def oct_sat(self, routing, pattern):
        a = self.octopus(routing, pattern, 1000, T_SAT[0], 'sat'); b = self.octopus(routing, pattern, 1000, T_SAT[1], 'sat')
        return (b['packets'] - a['packets']) * FLITS / (self.active(pattern) * (T_SAT[1] - T_SAT[0]))

    # ---------------------------------------------------------------- BookSim
    def booksim(self, routing, pattern, inj, mode, tag):
        d = os.path.join(self.a.out, 'bs', f'{routing}-{pattern}'); os.makedirs(d, exist_ok=True)
        base = os.path.join(d, f'{tag}-{inj:.6f}-{mode}')
        log, st = base + '.log', base + '.m'
        if not (self.a.reuse and os.path.exists(log)):
            if os.path.exists(st): os.remove(st)
            cmd = [self.a.booksim, self.a.cfg, f'routing_function={ROUTINGS[routing][0]}', f'num_vcs={self.a.vcs}',
                   f'k={self.k}', f'traffic={pattern}', f'injection_rate={inj:.6f}', f'sim_type={mode}',
                   'pair_stats=1', f'stats_out={st}']
            if routing == 'Valiant' and self.a.valiant_booksim: cmd[0] = self.a.valiant_booksim
            with self.sem:
                with open(log, 'w') as fo:
                    try: subprocess.call(cmd, stdout=fo, stderr=subprocess.STDOUT, timeout=BS_TIMEOUT)
                    except subprocess.TimeoutExpired: fo.write('\nSTOPPED after the time limit\n')
        txt = open(log).read()
        deadlock = 'Possible network deadlock' in txt
        stable = 'Simulation unstable' not in txt and 'Overall Traffic Statistics' in txt and not deadlock
        out = {'stable': stable, 'deadlock': deadlock}
        if stable and os.path.exists(st):
            m = {}
            for line in open(st):
                mm = re.match(r'(\w+)\(1,:\) = \[(.*)\];', line.strip())
                if mm: m[mm.group(1)] = [float(x) for x in mm.group(2).split()]
            plat = re.findall(r'Packet latency average = ([0-9.]+)', txt)
            cnt, nl, pl = m['pair_sent'], m['pair_nlat'], m['pair_plat']
            n = self.N; s = w = 0.0; frac = []; qmax = 0.0
            for i in range(n):
                row = sum(cnt[i * n:(i + 1) * n]); other = row - cnt[i * n + i]
                frac.append(other / row if row else 0.0)
                qs = qc = 0.0
                for j in range(n):
                    c = cnt[i * n + j]
                    if i != j and c > 0: s += c; w += c * nl[i * n + j]; qs += c * (pl[i * n + j] - nl[i * n + j]); qc += c
                if qc: qmax = max(qmax, qs / qc)
            out['max_queueing'] = qmax
            out['nlat'] = w / s if s else float('nan')
            out['plat'] = float(plat[-1]) if plat else float('nan')
            out['acc'] = sum(m['sent_packets'][i] * frac[i] for i in range(n)) * FLITS / self.active(pattern)
            out['pairs'] = {(i, j): cnt[i * n + j] for i in range(n) for j in range(n) if cnt[i * n + j] > 0}
        return out

    def bs_inj(self, pattern, load):
        # the offered load is per node to other nodes: BookSim's uniform also sends 1/N of its packets to itself
        return load * self.N / (self.N - 1) if pattern == 'uniform' else load

    def bs_point(self, routing, pattern, rate):
        load = rate * GRID
        r = self.booksim(routing, pattern, self.bs_inj(pattern, load), 'latency', 'pt')
        if r['stable']:
            # per source, as Octopus's test is: BookSim's own test averages over all sources and misses a
            # single saturated flow of a permutation
            flow_ok = r['max_queueing'] <= QUEUE_PACKETS * FLITS / load
            self.record('booksim', routing, pattern, load, r['nlat'], r['nlat'] - OFFSET, r['plat'], r['acc'], flow_ok, f"q{r['max_queueing']:.0f}")
            return flow_ok, r['nlat']
        self.record('booksim', routing, pattern, load, '', '', '', '', False, 'deadlock' if r['deadlock'] else 'unstable')
        return False, None

    def bs_sat(self, routing, pattern):
        r = self.booksim(routing, pattern, 1.0, 'throughput', 'sat')
        return r['acc'] if r['stable'] else None

    # ---------------------------------------------------------------- shared
    def record(self, side, routing, pattern, load, lat_bs_units, lat_raw, plat, acc, stable, retries):
        with self.lock:
            self.points.append([side, routing, pattern, f'{load:.3f}', fmt(lat_bs_units), fmt(lat_raw), fmt(plat), fmt(acc), int(stable), retries])

    def knee(self, point, sat):
        """lowest grid rate (packets per 1000 ticks) whose point is unstable; point(rate) -> (stable, lat)."""
        lo, hi = 0, max(2, math.ceil(1.1 * sat / GRID))
        while hi < 200 and point(hi)[0]: lo, hi = hi, min(200, hi * 2)
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if point(mid)[0]: lo = mid
            else: hi = mid
        return hi

    def case(self, routing, pattern):
        cache_o, cache_b = {}, {}
        def po(rate):
            if rate not in cache_o: cache_o[rate] = self.oct_point(routing, pattern, rate)
            return cache_o[rate]
        def pb(rate):
            if rate not in cache_b: cache_b[rate] = self.bs_point(routing, pattern, rate)
            return cache_b[rate]
        with cf.ThreadPoolExecutor(4) as ex:
            fo, fb = ex.submit(self.oct_sat, routing, pattern), ex.submit(self.bs_sat, routing, pattern)
            zo = ex.submit(self.oct_point, routing, pattern, 1, T_ZERO); zb = ex.submit(pb, 1)
            sat_o, sat_b = fo.result(), fb.result()
            ko, kb = ex.submit(self.knee, po, sat_o), ex.submit(self.knee, pb, sat_b if sat_b is not None else sat_o)
            knee_o, knee_b = ko.result(), kb.result()
            z_o, z_b = zo.result()[1], zb.result()[1]
        lower = min(knee_o, knee_b)
        fr = [(f, max(1, int(f * lower))) for f in (0.25, 0.5, 0.7, 0.8, 0.9)]
        with cf.ThreadPoolExecutor(4) as ex:
            futs = [(f, r, ex.submit(po, r), ex.submit(pb, r)) for f, r in fr]
            curve = [(f, r, a.result(), b.result()) for f, r, a, b in futs]
        res = {'routing': routing, 'pattern': pattern, 'zero_oct': z_o, 'zero_bs': z_b,
               'sat_oct': sat_o, 'sat_bs': sat_b, 'knee_oct': knee_o * GRID, 'knee_bs': knee_b * GRID}
        res['c1'] = z_b is not None and abs(z_b - z_o) <= C1_ZERO_ABS
        res['c2'] = None if sat_b is None else abs(sat_o - sat_b) / max(sat_o, sat_b) <= C2_SAT_REL   # None: BookSim deadlocked
        res['c3'] = abs(knee_o - knee_b) <= 1 or abs(knee_o - knee_b) / max(knee_o, knee_b) <= C3_KNEE_REL
        worst, c4 = 0.0, True; detail = []
        for f, r, (so, lo_), (sb, lb) in curve:
            if not (so and sb):
                detail.append(f'{r * GRID:.3f}:{"" if so else "O-unstable"}{"" if sb else "B-unstable"}')
                if f <= C4_MAXFRAC: c4 = False
                continue
            d = lo_ - lb; detail.append(f'{r * GRID:.3f}:{d:+.1f}')
            if f <= C4_MAXFRAC:
                worst = d if abs(d) > abs(worst) else worst
                if abs(d) > max(C4_ABS, C4_REL * lb): c4 = False
        res['c4'] = c4; res['worst_curve'] = worst; res['curve'] = ' '.join(detail)
        res['pass'] = res['c1'] and res['c2'] is not False and res['c3'] and res['c4']
        return res

    def patternmap(self, pattern, routing='XY'):
        """BookSim's destinations (its pair matrix) against Octopus's per-endpoint receive counts."""
        if pattern == 'uniform': return None
        b = self.booksim(routing, pattern, 0.05, 'latency', 'map')
        o = self.octopus(routing, pattern, 10, 20000, 'map')
        fixed = self.fixed(pattern); bad = []
        dest = {}
        for (i, j) in b.get('pairs', {}): dest.setdefault(i, set()).add(j)
        for i in range(self.N):
            dj = dest.get(i, set())
            if len(dj) != 1: bad.append(f'{i}:bs{sorted(dj)}'); continue
            j = next(iter(dj))
            if i in fixed:
                if j != i or o['sent'][i] != 0: bad.append(f'{i}:fixed')
                continue
            if o['recv'].get(j, -1) != o['sent'][i]: bad.append(f'{i}->{j}')
        return bad


def fmt(x):
    if x == '' or x is None: return ''
    return f'{x:.4f}' if isinstance(x, float) else str(x)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--octopus', required=True); ap.add_argument('--booksim', required=True)
    ap.add_argument('--cfg', required=True); ap.add_argument('--out', required=True)
    ap.add_argument('--k', type=int, default=8); ap.add_argument('--vcs', type=int, default=2)
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 4) - 4))
    ap.add_argument('--routings', default=','.join(ROUTINGS))
    ap.add_argument('--patterns', default='uniform,transpose,bitcomp,bitrev,shuffle,tornado')
    ap.add_argument('--reuse', action='store_true', help='reuse the runs already in --out')
    ap.add_argument('--valiant-booksim', default='', help="a BookSim binary for Valiant (stock valiant_mesh deadlocks, see the document)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    run = Runner(a)
    routings, patterns = a.routings.split(','), a.patterns.split(',')
    # the destination check, on XY
    pm_bad = {}
    with cf.ThreadPoolExecutor(8) as ex:
        futs = {p: ex.submit(run.patternmap, p) for p in patterns if p != 'uniform'}
        for p, f in futs.items(): pm_bad[p] = f.result()
    with open(os.path.join(a.out, 'patternmap.csv'), 'w', newline='') as f:
        w = csv.writer(f); w.writerow(['pattern', 'fixed_points', 'mismatches'])
        for p, bad in pm_bad.items(): w.writerow([p, len(run.fixed(p)), ' '.join(bad)])
    allbad = [f'{p}:{len(b)}' for p, b in pm_bad.items() if b]
    print('PATTERNMAP', 'FAIL' if allbad else 'PASS', ' '.join(allbad) if allbad else
          'every source reaches the same destination on both simulators: ' + ', '.join(f'{p} ({len(run.fixed(p))} fixed points)' for p in pm_bad), flush=True)
    # the matrix
    results = []
    with cf.ThreadPoolExecutor(len(routings) * len(patterns)) as ex:
        futs = [ex.submit(run.case, r, p) for r in routings for p in patterns]
        for f in futs:
            res = f.result(); results.append(res)
            print('CASE', res['routing'], res['pattern'], 'PASS' if res['pass'] else 'FAIL',
                  f"zero {res['zero_oct']:.2f}/{res['zero_bs']:.2f} sat {res['sat_oct']:.3f}/{fmt(res['sat_bs']) or 'deadlock'} "
                  f"knee {res['knee_oct']:.3f}/{res['knee_bs']:.3f} curve {res['curve']} "
                  f"[C1 {int(res['c1'])} C2 {'-' if res['c2'] is None else int(res['c2'])} C3 {int(res['c3'])} C4 {int(res['c4'])}]", flush=True)
    with open(os.path.join(a.out, 'points.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['simulator', 'routing', 'pattern', 'offered', 'latency_bs_units', 'latency_raw', 'booksim_plat', 'accepted', 'stable', 'retries_or_queueing'])
        for p in sorted(run.points, key=lambda r: (r[1], r[2], r[0], float(r[3]))): w.writerow(p)
    keys = ['routing', 'pattern', 'zero_oct', 'zero_bs', 'sat_oct', 'sat_bs', 'knee_oct', 'knee_bs', 'worst_curve', 'c1', 'c2', 'c3', 'c4', 'pass', 'curve']
    with open(os.path.join(a.out, 'cases.csv'), 'w', newline='') as f:
        w = csv.writer(f); w.writerow(keys)
        for res in sorted(results, key=lambda r: (routings.index(r['routing']), patterns.index(r['pattern']))):
            w.writerow([fmt(res[k]) if isinstance(res[k], float) else (int(res[k]) if isinstance(res[k], bool) else ('' if res[k] is None else res[k])) for k in keys])
    for r in routings:
        rs = [x for x in results if x['routing'] == r]
        def misses(x): return {c for c in ('c1', 'c2', 'c3', 'c4') if x[c] is False}
        unknown = [x for x in rs if not misses(x) <= KNOWN_MISSES.get((r, x['pattern']), set())]
        bad = [f"{x['pattern']}({'+'.join(sorted(c.upper() for c in misses(x)))})" for x in rs if not x['pass']]
        tag = 'REPORT' if r in REPORT_ONLY else ('PASS' if not bad else ('KNOWN' if not unknown else 'FAIL'))
        print('ROUTING', r, tag, (f'{len(rs) - len(bad)}/{len(rs)} patterns meet C1-C4' + (f'; not: {",".join(bad)}' if bad else '')), flush=True)


if __name__ == '__main__':
    main()
