#!/usr/bin/env python3
"""gem5 Garnet versus Octopus on a k x k mesh (docs/NoC_Garnet_Validation.md). Standard library only.

  compare_garnet.py --octopus <bin> --gem5 <gem5 tree with build/NULL/gem5.opt> --out <dir>
                    [--k 8] [--vcs 4] [--jobs 6] [--patterns uniform_random,transpose,...]

The gem5 binary is a Garnet_standalone build with test/network/garnet/gem5-validation.patch
applied (it adds the tester switch skip_self; the network model is stock). Every gem5 run goes
through test/network/garnet/garnet_window.py (a warm-up window, then a measurement window).

For every pattern it measures, on both simulators:
  zero-load latency  at 0.005 flits/cycle/node;
  saturation throughput (every source backlogged);
  the knee: the lowest offered load, on a 0.005 grid, at which some source can no longer keep up;
  the latency curve at 0.25, 0.5, 0.7, 0.8 and 0.9 of the lower knee,
and checks the agreement criteria C1..C4 of the document. Writes:
  <out>/points.csv      every run (side, pattern, offered load, latency, stability, wall time, ...)
  <out>/cases.csv       one row per pattern (zero-load, saturation, knee, worst curve diff, verdicts)
  <out>/patternmap.csv  the destination check of the permutation patterns
  <out>/calibration.csv the single-flow runs that fix the latency offset and the credit loop
and prints "CASE <k>x<k> <pattern> PASS|FAIL ..." per pattern and "PATTERNMAP PASS|FAIL ...".

Latency quantities. Gated: Octopus's class mean (head into the source router's local input VC to
tail ejection) plus OFFSET_H cycles against Garnet's packet_head_sent_latency on vnet 2 (head sent by
the source NI to tail received by the destination NI; a statistic the patch adds, timing unchanged).
Reported: Octopus plus OFFSET against Garnet's stock average_packet_network_latency (packet
flitisized into an NI VC to tail received, minus one cycle), which also holds the time a packet
waits in an NI VC behind other packets of the same source (document s7.1).
"""
import argparse, concurrent.futures as cf, csv, math, os, re, subprocess, threading, time

GRID = 0.005          # flits/cycle/node per Octopus rate step (1 packet per 1000 ticks, 5 flits)
FLITS = 5
OFFSET = 4            # Garnet's terminal cycles in its stock network latency (calibration, document s2)
OFFSET_H = 5          # ... in the head-sent latency (the stock one starts a cycle earlier and subtracts one)
T_CURVE = 20000       # Octopus injection cycles per curve or knee point; Garnet measurement window
T_ZERO = 50000        # ... at the zero-load point
T_SAT = (10000, 30000)  # Octopus: two backlogged runs, the difference removes the start-up fill
G_WARM = 10000        # Garnet warm-up cycles (statistics reset after them)
G_SAT_INJ = 0.2       # Garnet packets/cycle/node for the saturation run (1 flit/cycle/node offered)
RETRY_LIMIT = 0.01    # Octopus point is unstable when a generator was refused on > 1 % of the cycles
QUEUE_PACKETS = 16    # Garnet point is unstable when a source's mean queueing delay exceeds the time
                      # to inject this many packets at its offered rate (Octopus's inject_queue_depth)

C1_ZERO_ABS = 1.5
C2_SAT_REL = 0.10
C3_KNEE_REL = 0.10
C4_ABS, C4_REL, C4_MAXFRAC = 2.0, 0.10, 0.8

# Garnet pattern -> Octopus pattern ('table': per-generator hotspot_id = Garnet's destination)
PATTERNS = {
    'uniform_random': 'uniform',
    'transpose': 'transpose',
    'bit_complement': 'bitcomp',
    'bit_reverse': 'bitrev',
    'shuffle': 'shuffle',
    'tornado': 'table',       # Garnet's tornado moves along x only; Octopus's moves in every dimension
    'neighbor': 'table',      # Garnet: (x + 1) mod k in the same row; Octopus's neighbour is id + 1 mod N
    'bit_rotation': 'table',  # rotate right by one bit; Octopus has no such pattern
}


def garnet_dest(p, s, k):
    """The destination of node s, transcribed from GarnetSyntheticTraffic::generatePkt."""
    n = k * k; x, y = s % k, s // k; bits = n.bit_length() - 1
    if p == 'bit_complement': return (k - 1 - y) * k + (k - 1 - x)
    if p == 'bit_reverse':
        r = 0; v = s
        for _ in range(bits): r = (r << 1) | (v & 1); v >>= 1
        return r
    if p == 'bit_rotation': return s // 2 if s % 2 == 0 else s // 2 + n // 2
    if p == 'neighbor': return y * k + (x + 1) % k
    if p == 'shuffle': return 2 * s if s < n // 2 else 2 * s - n + 1
    if p == 'transpose': return x * k + y
    if p == 'tornado': return y * k + (x + k // 2 - 1) % k   # (int) ceil(radix/2): integer division
    return None


def fmt(x):
    if x == '' or x is None: return ''
    return f'{x:.4f}' if isinstance(x, float) else str(x)


class Runner:
    def __init__(self, a):
        self.a = a; self.k = a.k; self.N = a.k * a.k
        self.sem = threading.Semaphore(a.jobs)
        self.lock = threading.Lock()
        self.points = []
        self.gem5 = os.path.join(a.gem5, 'build', 'NULL', 'gem5.opt')
        self.cfg = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'garnet_window.py')
        ids = ','.join(str(i) for i in range(self.N))
        self.G = ['-p', 'interconnect.topology(s)=grid2d', '-p', f'interconnect.dims(vi)={a.k},{a.k}',
                  '-p', f'interconnect.component_ids(vi)={ids}', '-p', 'interconnect.routing(s)=XY']
        for i in range(self.N): self.G += ['-p', f'interconnect.node_map[{i}](i)={i}']
        # Garnet's default router (1 cycle) and links (1 cycle), mapped (document s2)
        self.G += ['-p', 'interconnect.router_latency(i)=0', '-p', 'interconnect.link_latency(i)=2',
                   '-p', 'interconnect.credit_latency(i)=2', '-p', f'interconnect.vc_depth(i)={a.depth}',
                   '-p', 'interconnect.sa_allocator(s)=input_first', '-p', 'interconnect.arbiter_type(s)=RRArbiter',
                   '-p', f'interconnect.vcs_per_class(i)={a.vcs}']
        self.GN = ['--num-cpus', str(self.N), '--num-dirs', str(self.N), '--network', 'garnet',
                   '--topology', 'Mesh_XY', '--mesh-rows', str(a.k), '--routing-algorithm', '1',
                   '--vcs-per-vnet', str(a.vcs), '--router-latency', '1', '--link-latency', '1',
                   '--inj-vnet', '2', '--precision', '6'] + (['--ordered-vnet'] if a.garnet_ordered else []) + [ '--buffers-per-data-vc', str(a.depth), '--sys-clock', '1GHz', '--ruby-clock', '1GHz']

    def fixed(self, p):
        if p == 'uniform_random': return set()
        return {s for s in range(self.N) if garnet_dest(p, s, self.k) == s}

    def active(self, p):
        return self.N - len(self.fixed(p))

    def timed(self, cmd, d, timeout=None):
        """Runs cmd at nice 19; writes <d>/.time = wall seconds and the child's own CPU seconds."""
        with self.sem:
            t0 = time.time()
            with open(os.path.join(d, '.stdout'), 'w') as fo, open(os.path.join(d, '.stderr'), 'w') as fe:
                pr = subprocess.Popen(['nice', '-n', '19'] + cmd, stdout=fo, stderr=fe)
                _, status, ru = os.wait4(pr.pid, 0)
            t1 = time.time()
        rc = os.waitstatus_to_exitcode(status); pr.returncode = rc
        with open(os.path.join(d, '.time'), 'w') as f: f.write(f'{t1 - t0:.3f} {ru.ru_utime + ru.ru_stime:.3f}\n')
        return rc

    @staticmethod
    def wall(d):
        try: return [float(x) for x in open(os.path.join(d, '.time')).read().split()]
        except OSError: return [float('nan'), float('nan')]

    # ---------------------------------------------------------------- Octopus
    def octopus(self, p, rate, cycles, tag, extra=()):
        d = os.path.join(self.a.out, 'oct', p, f'{tag}-r{rate}-t{cycles}')
        stats = os.path.join(d, 'NetworkStats.csv')
        if not (self.a.reuse and os.path.exists(stats)):
            os.makedirs(d, exist_ok=True)
            cmd = [self.a.octopus, '-s', 'NetworkOnly'] + self.G + list(extra)
            op = PATTERNS.get(p)   # None: the caller's extra overrides set the pattern
            if op == 'table':
                cmd += ['-p', 'generator[*].pattern(s)=hotspot']
                for s in range(self.N): cmd += ['-p', f'generator[{s}].hotspot_id(i)={garnet_dest(p, s, self.k)}']
            elif op:
                cmd += ['-p', f'generator[*].pattern(s)={op}']
            cmd += ['-p', f'generator[*].rate(i)={rate}', '-p', 'generator[*].packets(i)=-1',
                    '-p', f'generator[*].cycles(i)={cycles}']
            for s in (sorted(self.fixed(p)) if op else []):   # Garnet's fixed points send nothing (skip_self)
                cmd += ['-p', f'generator[{s}].pattern(s)=none']
            cmd += ['-o', d]
            rc = self.timed(cmd, d)
            if rc != 0 or not os.path.exists(stats):
                raise RuntimeError(f'Octopus failed ({rc}) in {d}')
        R = list(csv.reader(open(stats)))
        cls = [r for r in R if r[0] == 'class' and r[1] == '2'][0]
        gens = {int(r[1]): (int(r[4]), int(r[6])) for r in R if r[0] == 'generator'}
        recv = {int(r[1]): int(r[4]) for r in R if r[0] == 'received'}
        return {'packets': int(cls[4]), 'lat': float(cls[5]) if int(cls[4]) else float('nan'),
                'max_retries': max(v[1] for v in gens.values()),
                'sent': {g: v[0] for g, v in gens.items()}, 'recv': recv, 'wall': self.wall(d)}

    def oct_point(self, p, rate, cycles=T_CURVE):
        r = self.octopus(p, rate, cycles, 'pt')
        stable = r['max_retries'] <= RETRY_LIMIT * cycles
        acc = r['packets'] * FLITS / (self.active(p) * cycles)
        self.record('octopus', p, rate * GRID, r['lat'] + OFFSET_H, r['lat'], r['lat'] + OFFSET, '', acc, stable,
                    r['max_retries'], cycles, r['wall'])
        return stable, r['lat'] + OFFSET_H, r['lat'] + OFFSET

    def oct_sat(self, p):
        a = self.octopus(p, 1000, T_SAT[0], 'sat'); b = self.octopus(p, 1000, T_SAT[1], 'sat')
        acc = (b['packets'] - a['packets']) * FLITS / (self.active(p) * (T_SAT[1] - T_SAT[0]))
        self.record('octopus', p, 1.0, '', '', '', '', acc, False, 'sat', T_SAT[1], b['wall'])
        return acc

    # ---------------------------------------------------------------- Garnet
    def garnet(self, p, inj, meas, tag, warm=G_WARM, extra=()):
        d = os.path.join(self.a.out, 'gar', p, f'{tag}-{inj:.6f}-w{warm}-m{meas}' + ''.join(e.replace('-', '') for e in extra))
        st = os.path.join(d, 'stats.txt')
        if not (self.a.reuse and os.path.exists(st)):
            os.makedirs(d, exist_ok=True)
            if os.path.exists(st): os.remove(st)
            cmd = [self.gem5, '-d', d, self.cfg] + self.GN + ['--synthetic', p, '--injectionrate', f'{inj:.6f}',
                   '--warmup-cycles', str(warm), '--measure-cycles', str(meas)] + list(extra)
            if '--no-skip-self' not in extra: cmd.append('--skip-self')
            cmd = [c for c in cmd if c != '--no-skip-self']
            rc = self.timed(cmd, d)
            if rc != 0 or not os.path.exists(st):
                raise RuntimeError(f'gem5 failed ({rc}) in {d}')
        return self.parse_garnet(st, d, meas)

    def parse_garnet(self, st, d, meas):
        vals = {}
        for line in open(st):
            if line.startswith('---------- End Simulation Statistics'): break
            f = line.split()
            if '|' in line:      # a per-vnet vector: "name | vnet0 ... | vnet1 ... | vnet2 ..."
                groups = line.split('|')[1:]
                if len(groups) >= 3: vals[f[0] + '::2'] = groups[2].split()[0]
            elif len(f) >= 2: vals[f[0]] = f[1]
        net = 'system.ruby.network.'
        def g(key, default=0.0):
            v = vals.get(net + key, default)
            try: return float(v)
            except ValueError: return float('nan')
        out = {'packets': g('packets_received::2'), 'flits': g('flits_received::2'),
               'injected': g('packets_injected::2'), 'wall': self.wall(d)}
        out['nlat'] = g('packet_network_latency::2') / 1000.0 / out['packets'] if out['packets'] else float('nan')
        hn = g('packets_head_sent_received::2')
        out['hlat'] = g('packet_head_sent_latency::2') / 1000.0 / hn if hn else float('nan')
        out['qlat'] = g('packet_queueing_latency::2') / 1000.0 / out['packets'] if out['packets'] else float('nan')
        # per source: mean time in the L1 controller's response MessageBuffer (its queue in front of the NI)
        qmax = 0.0; worst = -1
        for i in range(self.N):
            pre = f'system.ruby.l1_cntrl{i}.responseFromCache.'
            n = float(vals.get(pre + 'm_msg_count', 0) or 0); t = float(vals.get(pre + 'm_stall_time', 0) or 0)
            q = t / n / 1000.0 if n else 0.0
            if q > qmax: qmax, worst = q, i
        out['max_queueing'] = qmax; out['worst_src'] = worst
        out['pairs'] = {}
        for key, v in vals.items():
            m = re.match(r'system\.ruby\.network\.data_traffic_distribution\.n(\d+)\.n(\d+)$', key)
            if m and float(v) > 0: out['pairs'][(int(m.group(1)), int(m.group(2)))] = float(v)
        return out

    def gar_point(self, p, rate, meas=T_CURVE):
        load = rate * GRID
        r = self.garnet(p, load / FLITS, meas, 'pt')
        # issue_latency (2 cycles) of the Garnet_standalone L1 is part of every message's buffer time
        stable = r['max_queueing'] <= 2 + QUEUE_PACKETS * FLITS / load
        acc = r['flits'] / (self.active(p) * meas)
        self.record('garnet', p, load, r['hlat'], r['hlat'] - OFFSET_H, r['nlat'], r['qlat'], acc, stable,
                    f"q{r['max_queueing']:.1f}@{r['worst_src']}", G_WARM + meas, r['wall'])
        return stable, r['hlat'], r['nlat']

    def gar_sat(self, p):
        r = self.garnet(p, G_SAT_INJ, T_SAT[1] - T_SAT[0], 'sat')
        acc = r['flits'] / (self.active(p) * (T_SAT[1] - T_SAT[0]))
        self.record('garnet', p, G_SAT_INJ * FLITS, '', '', '', '', acc, False, 'sat', G_WARM + T_SAT[1] - T_SAT[0], r['wall'])
        return acc

    # ---------------------------------------------------------------- shared
    def record(self, side, p, load, lat_g, lat_raw, lat_stock, qlat, acc, stable, note, cycles, wall):
        with self.lock:
            self.points.append([f'{self.k}x{self.k}', side, p, f'{load:.3f}', fmt(lat_g), fmt(lat_raw), fmt(lat_stock), fmt(qlat), fmt(acc),
                                int(stable), note, cycles, f'{wall[0]:.3f}', f'{wall[1]:.3f}'])

    def knee(self, point, sat):
        lo, hi = 0, max(2, math.ceil(1.1 * sat / GRID))
        while hi < 200 and point(hi)[0]: lo, hi = hi, min(200, hi * 2)
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if point(mid)[0]: lo = mid
            else: hi = mid
        return hi

    def case(self, p):
        cache_o, cache_g = {}, {}
        lk = threading.Lock()
        def memo(cache, fn):
            def f(rate):
                with lk:
                    if rate in cache: return cache[rate]
                v = fn(p, rate)
                with lk: cache[rate] = v
                return v
            return f
        po, pg = memo(cache_o, self.oct_point), memo(cache_g, self.gar_point)
        with cf.ThreadPoolExecutor(4) as ex:
            fo, fg = ex.submit(self.oct_sat, p), ex.submit(self.gar_sat, p)
            zo = ex.submit(self.oct_point, p, 1, T_ZERO); zg = ex.submit(self.gar_point, p, 1, T_ZERO)
            sat_o, sat_g = fo.result(), fg.result()
            ko, kg = ex.submit(self.knee, po, sat_o), ex.submit(self.knee, pg, sat_g)
            knee_o, knee_g = ko.result(), kg.result()
            z_o, z_g = zo.result()[1], zg.result()[1]
            zo_s, zg_s = zo.result()[2], zg.result()[2]
        lower = min(knee_o, knee_g)
        fr = [(f, max(1, int(f * lower))) for f in (0.25, 0.5, 0.7, 0.8, 0.9)]
        with cf.ThreadPoolExecutor(4) as ex:
            futs = [(f, r, ex.submit(po, r), ex.submit(pg, r)) for f, r in fr]
            curve = [(f, r, a.result(), b.result()) for f, r, a, b in futs]
        res = {'mesh': f'{self.k}x{self.k}', 'pattern': p, 'zero_oct': z_o, 'zero_gar': z_g, 'sat_oct': sat_o,
               'sat_gar': sat_g, 'knee_oct': knee_o * GRID, 'knee_gar': knee_g * GRID}
        res['c1'] = abs(z_g - z_o) <= C1_ZERO_ABS
        res['c2'] = abs(sat_o - sat_g) / max(sat_o, sat_g) <= C2_SAT_REL
        res['c3'] = abs(knee_o - knee_g) <= 1 or abs(knee_o - knee_g) / max(knee_o, knee_g) <= C3_KNEE_REL
        worst, c4, detail = 0.0, True, []
        worst_s, c4_s, detail_s = 0.0, True, []
        for f, r, (so, _, ls_o), (sg, _, ls_g) in curve:     # the stock quantity, reported only
            if so and sg and f <= C4_MAXFRAC:
                dd = ls_o - ls_g; worst_s = dd if abs(dd) > abs(worst_s) else worst_s
                if abs(dd) > max(C4_ABS, C4_REL * ls_g): c4_s = False
            if so and sg: detail_s.append(f'{r * GRID:.3f}:{ls_o - ls_g:+.1f}')
        res['c4_stock'] = c4_s; res['worst_curve_stock'] = worst_s; res['curve_stock'] = ' '.join(detail_s)
        res['zero_oct_stock'], res['zero_gar_stock'] = zo_s, zg_s
        for f, r, (so, lo_, _), (sg, lg, _) in curve:
            if not (so and sg):
                detail.append(f'{r * GRID:.3f}:{"" if so else "O-unstable"}{"" if sg else "G-unstable"}')
                if f <= C4_MAXFRAC: c4 = False
                continue
            dd = lo_ - lg; detail.append(f'{r * GRID:.3f}:{dd:+.1f}')
            if f <= C4_MAXFRAC:
                worst = dd if abs(dd) > abs(worst) else worst
                if abs(dd) > max(C4_ABS, C4_REL * lg): c4 = False
        res['c4'] = c4; res['worst_curve'] = worst; res['curve'] = ' '.join(detail)
        res['pass'] = res['c1'] and res['c2'] and res['c3'] and res['c4']
        return res

    def patternmap(self, p):
        """Garnet's injected (source, destination) pairs against Octopus's per-endpoint counts."""
        if p == 'uniform_random': return None
        g = self.garnet(p, 0.01, 5000, 'map', warm=0)
        o = self.octopus(p, 10, 20000, 'map')
        fixed = self.fixed(p); bad = []; dest = {}
        for (i, j) in g['pairs']: dest.setdefault(i, set()).add(j)
        for i in range(self.N):
            dj = dest.get(i, set())
            if i in fixed:
                if dj or o['sent'][i] != 0: bad.append(f'{i}:fixed')
                continue
            if len(dj) != 1 or next(iter(dj)) != garnet_dest(p, i, self.k): bad.append(f'{i}:gar{sorted(dj)}'); continue
            j = next(iter(dj))
            if o['recv'].get(j, -1) != o['sent'][i]: bad.append(f'{i}->{j}')
        return bad

    def calibration(self):
        """Single flows. Zero load: one packet against the hop count (fixes OFFSET). Credit loop:
        one backlogged flow corner to corner with 1 VC of depth 2 and 4, and with the matrix's VCs."""
        rows = []
        k = self.k
        corner = (k - 1) * k + k - 1
        def single(s, t):
            return ['-p', 'generator[*].pattern(s)=none', '-p', f'generator[{s}].pattern(s)=hotspot',
                    '-p', f'generator[{s}].hotspot_id(i)={t}']
        for s, t in [(0, h) for h in range(1, k)] + [(0, corner)]:
            hops = abs(s % k - t % k) + abs(s // k - t // k)
            o = self.octopus(f'cal-{s}-{t}', 1, 50000, 'cal', single(s, t))
            gr = self.garnet('uniform_random', 0.01, 2000, f'cal-{s}-{t}', warm=0,
                             extra=('--single-sender-id', str(s), '--single-dest-id', str(t), '--num-packets-max', '1'))
            rows.append(['zero', s, t, hops, '', '', fmt(o['lat']), fmt(gr['nlat']), fmt(gr['nlat'] - o['lat']),
                         fmt(gr['hlat']), fmt(gr['hlat'] - o['lat'])])
        for vcs, depth in [(1, 2), (1, 4), (self.a.vcs, self.a.depth)]:
            ov = ['-p', f'interconnect.vcs_per_class(i)={vcs}', '-p', f'interconnect.vc_depth(i)={depth}']
            a_ = self.octopus(f'cal-sat-v{vcs}d{depth}', 1000, 10000, 'cal', single(0, corner) + ov)
            b_ = self.octopus(f'cal-sat-v{vcs}d{depth}', 1000, 30000, 'cal', single(0, corner) + ov)
            o_tp = (b_['packets'] - a_['packets']) * FLITS / 20000
            gr = self.garnet('uniform_random', 0.3, 20000, f'cal-sat-v{vcs}d{depth}', warm=10000,
                             extra=('--single-sender-id', '0', '--single-dest-id', str(corner),
                                    '--vcs-per-vnet', str(vcs), '--buffers-per-data-vc', str(depth)))
            rows.append(['flow_throughput', 0, corner, 2 * (k - 1), vcs, depth, fmt(o_tp), fmt(gr['flits'] / 20000), '', '', ''])
        return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--octopus', required=True); ap.add_argument('--gem5', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--k', type=int, default=8); ap.add_argument('--vcs', type=int, default=4)
    ap.add_argument('--depth', type=int, default=4)
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--patterns', default=','.join(PATTERNS))
    ap.add_argument('--reuse', action='store_true', help='reuse the runs already in --out')
    ap.add_argument('--garnet-ordered', action='store_true',
                    help='declare Garnet vnet 2 ordered (no flit interleaving of one source\'s packets, document s7.1)')
    ap.add_argument('--only', default='', help='calibration | patternmap (run just that part)')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    run = Runner(a)
    patterns = a.patterns.split(',')
    cal = run.calibration()
    with open(os.path.join(a.out, 'calibration.csv'), 'w', newline='') as f:
        w = csv.writer(f); w.writerow(['kind', 'src', 'dst', 'hops', 'vcs', 'depth', 'octopus', 'garnet_stock', 'stock_minus_octopus', 'garnet_head_sent', 'head_sent_minus_octopus'])
        w.writerows(cal)
    offs = sorted({f'{r[8]}/{r[10]}' for r in cal if r[0] == 'zero'})
    print('CALIBRATION', f'{a.k}x{a.k}', 'garnet - octopus at zero load per flow (stock/head-sent):', ' '.join(offs), flush=True)
    if a.only == 'calibration': return
    pm_bad = {}
    with cf.ThreadPoolExecutor(8) as ex:
        futs = {p: ex.submit(run.patternmap, p) for p in patterns if p != 'uniform_random'}
        for p, f in futs.items(): pm_bad[p] = f.result()
    with open(os.path.join(a.out, 'patternmap.csv'), 'w', newline='') as f:
        w = csv.writer(f); w.writerow(['pattern', 'fixed_points', 'mismatches'])
        for p, bad in pm_bad.items(): w.writerow([p, len(run.fixed(p)), ' '.join(bad)])
    allbad = [f'{p}:{len(b)}' for p, b in pm_bad.items() if b]
    print('PATTERNMAP', f'{a.k}x{a.k}', 'FAIL' if allbad else 'PASS', ' '.join(allbad) if allbad else
          ', '.join(f'{p} ({len(run.fixed(p))} fixed points)' for p in pm_bad), flush=True)
    if a.only == 'patternmap': return
    results = []
    with cf.ThreadPoolExecutor(len(patterns)) as ex:
        futs = [ex.submit(run.case, p) for p in patterns]
        for f in futs:
            res = f.result(); results.append(res)
            print('CASE', res['mesh'], res['pattern'], 'PASS' if res['pass'] else 'FAIL',
                  f"zero {res['zero_oct']:.2f}/{res['zero_gar']:.2f} sat {res['sat_oct']:.3f}/{res['sat_gar']:.3f} "
                  f"knee {res['knee_oct']:.3f}/{res['knee_gar']:.3f} curve {res['curve']} "
                  f"[C1 {int(res['c1'])} C2 {int(res['c2'])} C3 {int(res['c3'])} C4 {int(res['c4'])}] "
                  f"stock: zero {res['zero_oct_stock']:.2f}/{res['zero_gar_stock']:.2f} worst {res['worst_curve_stock']:+.1f} C4 {int(res['c4_stock'])}", flush=True)
    with open(os.path.join(a.out, 'points.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['mesh', 'simulator', 'pattern', 'offered', 'latency_garnet_units', 'latency_raw', 'latency_stock_units', 'garnet_queueing',
                    'accepted', 'stable', 'retries_or_queueing', 'sim_cycles', 'wall_s', 'cpu_s'])
        for p in sorted(run.points, key=lambda r: (r[2], r[1], float(r[3]))): w.writerow(p)
    keys = ['mesh', 'pattern', 'zero_oct', 'zero_gar', 'sat_oct', 'sat_gar', 'knee_oct', 'knee_gar', 'worst_curve',
            'c1', 'c2', 'c3', 'c4', 'pass', 'curve', 'zero_oct_stock', 'zero_gar_stock', 'worst_curve_stock', 'c4_stock', 'curve_stock']
    with open(os.path.join(a.out, 'cases.csv'), 'w', newline='') as f:
        w = csv.writer(f); w.writerow(keys)
        for res in sorted(results, key=lambda r: patterns.index(r['pattern'])):
            w.writerow([fmt(res[k]) if isinstance(res[k], float) else (int(res[k]) if isinstance(res[k], bool) else res[k]) for k in keys])
    n = sum(1 for r in results if r['pass'])
    print('MESH', f'{a.k}x{a.k}', 'PASS' if n == len(results) else 'FAIL', f'{n}/{len(results)} patterns meet C1-C4', flush=True)


if __name__ == '__main__':
    main()
