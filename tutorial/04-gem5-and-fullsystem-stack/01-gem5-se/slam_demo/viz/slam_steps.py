#!/usr/bin/env python3
"""slam_steps.py -- step-by-step pictures of what slam_demo does.

Reads steps.json (written by dump_steps, which runs slam_demo's own C++ code)
and draws one figure per step into out/:

  1_world        the corridor loop and the robot's true path
  2_scan         one lidar scan, in the robot's frame and placed in the world
  3_mapper       one keyframe written into the grid, one beam cell by cell
  4_guess        the front-end's constant-velocity guess, with and without drops
  5_matching     Gauss-Newton moving the scan onto the walls, step by step
  6_keyframes    the map growing keyframe by keyframe
  7_timeline     player / front-end / mapper activity, on time and late
  8_result       estimated paths against the truth, error per scan

integrate() and match() are line-for-line ports of slam_demo.cpp so every
intermediate step can be drawn; the script checks that they reproduce the
C++ results (final grid and every tracked pose of the no-deadline run).

usage: python slam_steps.py [--data steps.json] [--out out] [--scan K] [--show]
"""

import argparse
import json
import math
import os
import textwrap

import matplotlib

import numpy as np

COL = dict(wall="#17202b", beam="#d97a12", est="#11807e", guess="#c73e5a",
           late="#c73e5a", gt="#17202b", block="#c9d2dc", corridor="#e9eef3",
           free="#e6edf3", unknown="#b4bec9")


# ----------------------------------------------------------- the C++ ports

def lround(v):
    """C lround: round half away from zero."""
    return int(math.copysign(math.floor(abs(v) + 0.5), v))


def wrap(a):
    while a > math.pi:
        a -= 2 * math.pi
    while a <= -math.pi:
        a += 2 * math.pi
    return a


class Grid:
    """slam_demo's occupancy grid: log-odds x10 in int8, one per 5 cm cell."""

    def __init__(self, d):
        self.res, self.w, self.h = d["res"], d["gw"], d["gh"]
        self.v = np.zeros((self.h, self.w), np.int16)    # [y, x]
        self.stamp = np.zeros((self.h, self.w), np.uint16)
        self.prob = np.array([np.float32(1 / (1 + math.exp(-v / 10.0)))
                              for v in range(-128, 128)], np.float64)

    def copy(self):
        g = Grid.__new__(Grid)
        g.res, g.w, g.h, g.prob = self.res, self.w, self.h, self.prob
        g.v, g.stamp = self.v.copy(), self.stamp.copy()
        return g

    def cell(self, v):
        return lround(v / self.res)

    def bump(self, x, y, d, sid, log=None):
        if x < 0 or y < 0 or x >= self.w or y >= self.h:
            return
        if self.stamp[y, x] == sid:
            return                                   # once per keyframe
        self.stamp[y, x] = sid
        before = int(self.v[y, x])
        self.v[y, x] = max(-50, min(50, before + d))
        if log is not None:
            log.append((x, y, before, int(self.v[y, x])))


def integrate(G, pose, r, sid, beams, rng, trace=None):
    """Mapper: +9 at every beam end, then -4 along every beam (Bresenham).
    trace = beam index whose cell updates are returned."""
    log = []
    x0, y0, th = pose
    rx, ry = G.cell(x0), G.cell(y0)
    for pas in (0, 1):
        for i in range(beams):
            if pas == 0 and r[i] < 0:
                continue
            ln = rng if r[i] < 0 else float(r[i])
            ang = th - math.pi + i * 2 * math.pi / beams
            ex, ey = G.cell(x0 + ln * math.cos(ang)), G.cell(y0 + ln * math.sin(ang))
            lg = log if i == trace else None
            if pas == 0:
                G.bump(ex, ey, 9, sid, lg)
                continue
            x, y = rx, ry
            adx, ady = abs(ex - rx), abs(ey - ry)
            err = adx - ady
            while x != ex or y != ey:
                G.bump(x, y, -4, sid, lg)
                e2 = 2 * err
                if e2 >= -ady:
                    err -= ady
                    x += 1 if rx < ex else -1
                if e2 <= adx:
                    err += adx
                    y += 1 if ry < ey else -1
    return log


def local_points(r, beams):
    """Beam endpoints relative to the robot (float32, as in the C++)."""
    i = np.nonzero(r >= 0)[0]
    ang = -math.pi + i * 2 * math.pi / beams
    rr = r[i].astype(np.float64)
    return (rr * np.cos(ang)).astype(np.float32), (rr * np.sin(ang)).astype(np.float32)


def to_world(sx, sy, pose):
    c, s = math.cos(pose[2]), math.sin(pose[2])
    sx, sy = sx.astype(np.float64), sy.astype(np.float64)
    return c * sx - s * sy + pose[0], s * sx + c * sy + pose[1]


def match(G, sx, sy, pose):
    """Front-end: 8 Gauss-Newton steps on sum (1 - M(endpoint))^2.
    Returns (final pose, beams used, [pose before each step ... final])."""
    p = list(pose)
    hist = [tuple(p)]
    used = 0
    res = G.res
    for _ in range(8):
        c, s = math.cos(p[2]), math.sin(p[2])
        wx, wy = to_world(sx, sy, p)
        mx, my = wx / res, wy / res
        x0, y0 = np.floor(mx).astype(int), np.floor(my).astype(int)
        ok = (x0 >= 0) & (y0 >= 0) & (x0 + 1 < G.w) & (y0 + 1 < G.h)
        x0, y0, mx, my = x0[ok], y0[ok], mx[ok], my[ok]
        fx, fy = mx - x0, my - y0
        pr = G.prob
        p00, p10 = pr[G.v[y0, x0] + 128], pr[G.v[y0, x0 + 1] + 128]
        p01, p11 = pr[G.v[y0 + 1, x0] + 128], pr[G.v[y0 + 1, x0 + 1] + 128]
        dx = ((1 - fy) * (p10 - p00) + fy * (p11 - p01)) / res
        dy = ((1 - fx) * (p01 - p00) + fx * (p11 - p10)) / res
        live = ~((dx == 0) & (dy == 0))
        used = int(live.sum())
        if used < 10:
            break
        m = (1 - fy) * ((1 - fx) * p00 + fx * p10) + fy * ((1 - fx) * p01 + fx * p11)
        lx, ly = sx.astype(np.float64)[ok], sy.astype(np.float64)[ok]
        J = np.stack([dx, dy, dx * (-s * lx - c * ly) + dy * (c * lx - s * ly)])[:, live]
        e = (1 - m)[live]
        H = J @ J.T
        g = J @ e
        H[np.diag_indices(3)] += 1e-3 * np.diag(H) + 1e-6
        if abs(np.linalg.det(H)) < 1e-12:
            break
        step = np.linalg.solve(H, g)
        p = [p[0] + step[0], p[1] + step[1], wrap(p[2] + step[2])]
        hist.append(tuple(p))
    return tuple(p), used, hist


def front_end_guess(est, gt, last, prev, gap):
    """Constant velocity: last estimate + (last - previous) per scan x gap."""
    if prev is None:
        vel = (gt[1][0] - gt[0][0], gt[1][1] - gt[0][1], wrap(gt[1][2] - gt[0][2]))
    else:
        n = last - prev
        vel = ((est[last][0] - est[prev][0]) / n, (est[last][1] - est[prev][1]) / n,
               wrap(est[last][2] - est[prev][2]) / n)
    e = est[last]
    return (e[0] + vel[0] * gap, e[1] + vel[1] * gap, wrap(e[2] + vel[2] * gap)), vel


# ------------------------------------------------------------ the replays

class Replay:
    """Rebuilds the no-deadline run: map state before every scan."""

    def __init__(self, d):
        self.d = d
        self.B, self.R = d["beams"], d["range"]
        self.sensor = np.array(d["sensor"], np.float32).reshape(-1, self.B)
        self.gt = [tuple(p) for p in d["gt"]]
        run = d["offline"]
        self.est = [tuple(p) for p in run["est"]]
        self.kf_scan, self.kf_pose = run["kf_scan"], [tuple(p) for p in run["kf_pose"]]
        # grid after scan 0 (true pose), then after each keyframe
        G = Grid(d)
        integrate(G, self.gt[0], self.sensor[0], 1, self.B, self.R)
        self.after_kf = [G.copy()]
        for j, (k, p) in enumerate(zip(self.kf_scan, self.kf_pose)):
            integrate(G, p, self.sensor[k], 2 + j, self.B, self.R)
            self.after_kf.append(G.copy())

    def map_before_scan(self, k):
        """No deadlines: every keyframe from scans before k is in the map."""
        n = sum(1 for s in self.kf_scan if s < k)
        return self.after_kf[n]

    def check(self):
        final = np.array(self.d["grid_final"], np.int16).reshape(self.d["gh"], self.d["gw"])
        bad_cells = int((final != self.after_kf[-1].v).sum())
        worst = 0.0
        for k in range(1, len(self.gt)):
            guess, _ = front_end_guess(self.est, self.gt, k - 1, k - 2 if k >= 2 else None, 1)
            sx, sy = local_points(self.sensor[k], self.B)
            p, used, _ = match(self.map_before_scan(k), sx, sy, guess)
            if used < 10 or math.hypot(p[0] - guess[0], p[1] - guess[1]) > 1.0:
                p = guess
            worst = max(worst, math.hypot(p[0] - self.est[k][0], p[1] - self.est[k][1]))
        return bad_cells, worst


# ----------------------------------------------------------------- drawing

def grid_cmap():
    from matplotlib.colors import LinearSegmentedColormap
    return LinearSegmentedColormap.from_list(
        "occ", [(0, COL["free"]), (0.5, COL["unknown"]), (1, COL["wall"])])


def show_grid(ax, G, alpha=1.0):
    r = G.res
    ax.imshow(G.v, origin="lower", cmap=grid_cmap(), vmin=-50, vmax=50, alpha=alpha,
              interpolation="nearest",
              extent=(-r / 2, (G.w - 0.5) * r, -r / 2, (G.h - 0.5) * r))


def draw_walls(ax, walls, color=COL["wall"], lw=1.0, alpha=1.0):
    from matplotlib.collections import LineCollection
    ax.add_collection(LineCollection([((a, b), (c, e)) for a, b, c, e in walls],
                                     colors=color, linewidths=lw, alpha=alpha))


def robot(ax, p, color, size=0.35, label=None):
    ax.annotate("", xy=(p[0] + size * math.cos(p[2]), p[1] + size * math.sin(p[2])),
                xytext=(p[0], p[1]),
                arrowprops=dict(arrowstyle="-|>", color=color, lw=2, mutation_scale=14))
    ax.plot(p[0], p[1], "o", color=color, ms=5, label=label)


def window(ax, cx, cy, hw, hh, world=None):
    if world:                                # keep the view inside the floor
        W, H = world
        cx = min(max(cx, hw - 0.3), W + 0.3 - hw)
        cy = min(max(cy, hh - 0.3), H + 0.3 - hh)
    ax.set_xlim(cx - hw, cx + hw)
    ax.set_ylim(cy - hh, cy + hh)
    ax.set_aspect("equal")


def caption(fig, text, width=150):
    fig.text(0.01, 0.01, "\n".join(textwrap.wrap(text, width)), ha="left", va="bottom",
             fontsize=9.5, color="#333")


def finish(fig, out, name, show, bottom):
    fig.subplots_adjust(bottom=bottom)
    path = os.path.join(out, name + ".png")
    fig.savefig(path, dpi=140)
    print("  " + path)
    if not show:
        import matplotlib.pyplot as plt
        plt.close(fig)


# ------------------------------------------------------------- the figures

def fig_world(plt, d, rp):
    fig, ax = plt.subplots(figsize=(12, 6.4))
    W, H, I = d["W"], d["H"], d["inset"]
    ax.add_patch(plt.Rectangle((0, 0), W, H, color=COL["corridor"], zorder=0))
    ax.add_patch(plt.Rectangle((I, I), W - 2 * I, H - 2 * I, color=COL["block"], zorder=0))
    draw_walls(ax, d["walls"])
    xs, ys = zip(*[(p[0], p[1]) for p in rp.gt])
    ax.plot(xs, ys, "-", color=COL["est"], lw=2.5, label="true path, 40 scans (0.2 m apart)")
    ax.plot(xs, ys, ".", color=COL["est"], ms=5)
    robot(ax, rp.gt[0], COL["est"], 0.8)
    ax.text(W / 2, H / 2, "inner block (solid)", ha="center", va="center", color="#555")
    ax.text(W / 2, I / 2, "corridor, 5 m wide", ha="center", va="center", color="#555")
    ax.text(rp.gt[0][0] - 0.4, rp.gt[0][1] - 0.9, "start", ha="right")
    ax.set_xlim(-0.5, W + 0.5); ax.set_ylim(-0.5, H + 0.5); ax.set_aspect("equal")
    ax.set_xlabel("x (m)"); ax.set_ylabel("y (m)")
    ax.set_title("Step 1 - the world: a corridor loop with boxes along its walls")
    ax.legend(loc="upper left")
    caption(fig, "The simulated floor is 44 x 20 m. The black lines are walls: the outer "
            "rectangle, the inner block, and ~140 boxes (4 segments each). The robot follows "
            "a scripted path through the bottom-right corner, 40 scans about 0.2 m apart, "
            "with small speed changes and a side-to-side weave. The SLAM never sees these "
            "walls or this path; it only gets the lidar distances (step 2). The path is the "
            "ground truth the estimate is scored against.")
    return fig, 0.13


def fig_scan(plt, d, rp, k):
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(13, 6.2))
    r = rp.sensor[k]
    sx, sy = local_points(r, rp.B)
    for x, y in zip(sx, sy):
        a1.plot([0, x], [0, y], "-", color=COL["beam"], lw=0.6, alpha=0.5)
    a1.plot(sx, sy, ".", color=COL["beam"], ms=4)
    robot(a1, (0, 0, 0), COL["wall"], 0.8)
    a1.set_aspect("equal"); a1.grid(alpha=0.3)
    a1.set_xlabel("forward (m)"); a1.set_ylabel("left (m)")
    a1.set_title(f"(a) scan {k} as the lidar reports it:\n{len(sx)} of {rp.B} beams returned, "
                 "distances from the robot")
    p = rp.gt[k]
    wx, wy = to_world(sx, sy, p)
    draw_walls(a2, d["walls"], "#999", 1.0)
    for x, y in zip(wx, wy):
        a2.plot([p[0], x], [p[1], y], "-", color=COL["beam"], lw=0.6, alpha=0.5)
    a2.plot(wx, wy, ".", color=COL["beam"], ms=4)
    robot(a2, p, COL["wall"], 0.8)
    window(a2, p[0], p[1], 8, 6.5, (d["W"], d["H"]))
    a2.set_xlabel("x (m)"); a2.set_ylabel("y (m)")
    a2.set_title("(b) the same beams placed in the world with the true pose:\n"
                 "the endpoints trace the walls (grey)")
    caption(fig, "A scan is 180 distances, one per 2 degrees, up to 10 m (beams with no wall "
            "in range return nothing). On its own it is only a shape around the robot (a). "
            "To use it, the SLAM needs the pose: rotate the shape by the heading and shift it "
            "by x, y (b). With the true pose, every endpoint lands on a wall. The SLAM does "
            "not know the true pose; it has to find the pose at which the endpoints line up "
            "with the walls it has mapped so far (steps 4 and 5).")
    return fig, 0.14


def fig_mapper(plt, d, rp, j):
    k, p = rp.kf_scan[j], rp.kf_pose[j]
    before = rp.after_kf[j]
    after = before.copy()
    r = rp.sensor[k]
    # trace a beam that ends 2.5-4 m away, roughly ahead
    cand = [i for i in range(rp.B) if 2.5 < r[i] < 4.0]
    ib = min(cand, key=lambda i: abs(i * 2 * math.pi / rp.B - math.pi))
    log = integrate(after, p, r, 1000 + j, rp.B, rp.R, trace=ib)
    fig = plt.figure(figsize=(15, 9.6))
    gs = fig.add_gridspec(2, 3, height_ratios=[1, 1.05])
    axs = [fig.add_subplot(gs[0, i]) for i in range(3)]
    for ax, G, t in ((axs[0], before, f"(a) map before keyframe {j + 1} (scan {k})"),
                     (axs[1], after, f"(b) map after writing keyframe {j + 1}")):
        show_grid(ax, G)
        robot(ax, p, COL["est"], 0.6)
        window(ax, p[0] + 1.5, p[1] + 1.5, 8, 6, (d["W"], d["H"]))
        ax.set_title(t)
    diff = after.v.astype(int) - before.v.astype(int)
    ax = axs[2]
    from matplotlib.colors import ListedColormap
    show = np.where(diff > 0, 1, np.where(diff < 0, -1, np.nan))
    rr = after.res
    ax.imshow(show, origin="lower", cmap=ListedColormap([COL["free"], COL["wall"]]),
              vmin=-1, vmax=1, interpolation="nearest",
              extent=(-rr / 2, (after.w - 0.5) * rr, -rr / 2, (after.h - 0.5) * rr))
    ax.set_facecolor("#f7f7f7")
    wx, wy = to_world(*local_points(r, rp.B), p)
    robot(ax, p, COL["est"], 0.6)
    window(ax, p[0] + 1.5, p[1] + 1.5, 8, 6, (d["W"], d["H"]))
    n_up, n_dn = int((diff > 0).sum()), int((diff < 0).sum())
    ax.set_title(f"(c) cells changed: {n_up} raised (+9, beam ends),\n"
                 f"{n_dn} lowered (-4, beams passed through)")
    # (d) one beam, cell by cell
    ax = fig.add_subplot(gs[1, :])
    # log: the end cell first (pass 0), then the beam's cells from the robot out
    shown = [log[0]] + log[1:][-18:]
    x_lo = min(c[0] for c in shown) - 2
    x_hi = max(c[0] for c in shown) + 2
    y_lo = min(c[1] for c in shown) - 2
    y_hi = max(c[1] for c in shown) + 2
    sub = after.v[y_lo:y_hi + 1, x_lo:x_hi + 1]
    ax.imshow(sub, origin="lower", cmap=grid_cmap(), vmin=-50, vmax=50,
              extent=(x_lo - 0.5, x_hi + 0.5, y_lo - 0.5, y_hi + 0.5), interpolation="nearest")
    logd = {(x, y): (b, a) for x, y, b, a in log}
    for (x, y), (b, a) in logd.items():
        if x_lo <= x <= x_hi and y_lo <= y <= y_hi:
            ax.add_patch(plt.Rectangle((x - 0.5, y - 0.5), 1, 1, fill=False,
                                       ec=COL["beam"], lw=2))
            ax.text(x, y, f"{b}\n>{a}", ha="center", va="center",
                    fontsize=7, color="white" if a > 20 else "black")
    ax.set_xticks([]); ax.set_yticks([])
    ax.set_aspect("equal")
    ax.set_title(f"(d) one beam (beam {ib}, {r[ib]:.2f} m) near its end, cell by cell: "
                 "orange cells are the ones this beam touched, labelled before > after")
    caption(fig, "The mapper takes a keyframe (a scan plus the pose the front-end estimated "
            "for it) and writes it into the grid. Each cell is 5 x 5 cm and holds a number "
            "from -50 (surely free) to +50 (surely a wall); 0 means never seen (grey). "
            "First, the cell where each beam ended gets +9. Then every cell on the straight "
            "line from the robot to that end (Bresenham's line algorithm) gets -4, because "
            "the laser passed through it. Each cell changes at most once per keyframe. The "
            "pose decides where all of this lands: a wrong pose writes the walls in the "
            "wrong place.")
    return fig, 0.10


def fig_guess(plt, d, rp, k):
    fig, axs = plt.subplots(1, 2, figsize=(13, 6.2))
    for ax, gap in zip(axs, (1, 4)):
        last = k - gap
        prev = last - 1
        guess, vel = front_end_guess(rp.est, rp.gt, last, prev if prev >= 0 else None, gap)
        gx = [p[0] for p in rp.gt[max(0, prev - 2):k + 3]]
        gy = [p[1] for p in rp.gt[max(0, prev - 2):k + 3]]
        ax.plot(gx, gy, "--", color=COL["gt"], lw=1, label="true path")
        for i in range(max(0, prev - 2), min(len(rp.gt), k + 3)):
            ax.plot(rp.gt[i][0], rp.gt[i][1], ".", color="#999", ms=5)
        robot(ax, rp.est[prev], "#666", 0.25, label=f"estimate, scan {prev}")
        robot(ax, rp.est[last], COL["est"], 0.25, label=f"estimate, scan {last} (last tracked)")
        ax.annotate("", xy=(rp.est[last][0] + vel[0], rp.est[last][1] + vel[1]),
                    xytext=(rp.est[last][0], rp.est[last][1]),
                    arrowprops=dict(arrowstyle="->", color=COL["est"], lw=1.5, ls=":"))
        robot(ax, guess, COL["guess"], 0.25, label=f"guess for scan {k}")
        robot(ax, rp.gt[k], COL["gt"], 0.25, label=f"truth, scan {k}")
        err = math.hypot(guess[0] - rp.gt[k][0], guess[1] - rp.gt[k][1])
        derr = math.degrees(abs(wrap(guess[2] - rp.gt[k][2])))
        cx = (rp.est[prev][0] + rp.gt[k][0]) / 2
        cy = (rp.est[prev][1] + rp.gt[k][1]) / 2
        span = max(abs(rp.est[prev][0] - rp.gt[k][0]), abs(rp.est[prev][1] - rp.gt[k][1]))
        window(ax, cx, cy, span / 2 + 0.6, span / 2 + 0.6)
        ax.grid(alpha=0.3); ax.legend(loc="best", fontsize=8.5)
        what = "no scans dropped" if gap == 1 else f"{gap - 1} scans dropped"
        ax.set_title(f"({'ab'[gap > 1]}) {what}: extrapolate {gap} x the last motion\n"
                     f"guess is {err * 100:.1f} cm and {derr:.1f} deg from the truth")
    caption(fig, "The front-end's starting point for each scan is a guess: take the last "
            "estimate and repeat the last per-scan motion (dotted arrow) once for every scan "
            "since then. With no drops (a) the robot moved one step, and the guess is only off "
            "by how much the speed and turning changed. If the front-end was late and "
            "skipped scans (b), it extrapolates over several steps, and the error grows with "
            "the square of the gap. Matching (step 5) has to close that gap, and it can only "
            "do so if the guess is within a few cells of the truth.")
    return fig, 0.2


def fig_matching(plt, d, rp, k):
    G = rp.map_before_scan(k)
    sx, sy = local_points(rp.sensor[k], rp.B)
    rows = []
    for gap in (1, 4, 8):
        last = k - gap
        guess, _ = front_end_guess(rp.est, rp.gt, last, last - 1 if last >= 1 else None, gap)
        p, used, hist = match(G, sx, sy, guess)
        rows.append((gap, guess, hist, used))
    ref = rows[0][2][-1]            # where the normal guess ends: the map's answer
    fig = plt.figure(figsize=(16, 12.5))
    gs = fig.add_gridspec(3, 5, width_ratios=[1, 1, 1, 1, 1.25], wspace=0.3, hspace=0.35)
    show_it = (0, 1, 2, 8)
    t = rp.gt[k]
    for ri, (gap, guess, hist, used) in enumerate(rows):
        for ci, it in enumerate(show_it):
            ax = fig.add_subplot(gs[ri, ci])
            pose = hist[min(it, len(hist) - 1)]
            show_grid(ax, G)
            wx, wy = to_world(sx, sy, pose)
            ax.plot(wx, wy, ".", color=COL["guess"] if it == 0 else COL["est"], ms=3.5)
            robot(ax, pose, COL["guess"] if it == 0 else COL["est"], 0.4)
            window(ax, t[0] + 0.5, t[1] + 0.5, 3.2, 3.2)
            ax.set_xticks([]); ax.set_yticks([])
            e = math.hypot(pose[0] - t[0], pose[1] - t[1])
            lbl = "guess" if it == 0 else f"after step {min(it, len(hist) - 1)}"
            ax.set_title(f"{lbl}: {e * 100:.1f} cm off", fontsize=9.5)
            if ci == 0:
                ax.set_ylabel(f"guess over {gap} scan(s)", fontsize=10)
        ax = fig.add_subplot(gs[ri, 4])
        errs = [math.hypot(h[0] - t[0], h[1] - t[1]) * 100 for h in hist]
        miss = [math.hypot(h[0] - ref[0], h[1] - ref[1]) * 100 for h in hist]
        ax.plot(range(len(errs)), errs, "o-", color=COL["gt"], ms=4, label="to the truth")
        ax.plot(range(len(miss)), miss, "s--", color=COL["est"], ms=4,
                label="to the map's answer")
        ax.set_xlabel("Gauss-Newton step"); ax.set_ylabel("distance (cm)", fontsize=9)
        ax.set_xlim(-0.3, 8.3); ax.set_ylim(bottom=0); ax.grid(alpha=0.3)
        ax.legend(fontsize=8)
        verdict = ("reached the map's answer" if miss[-1] < 1 else
                   f"ends {miss[-1]:.0f} cm from the map's answer")
        ax.set_title(f"{used} beams felt a wall; {verdict}", fontsize=9.5)
    fig.suptitle(f"Step 5 - matching scan {k}: move the pose until the endpoints sit on the "
                 "walls in the map", fontsize=13)
    caption(fig, "Each panel shows the map as it was when scan "
            f"{k} arrived, and the scan's endpoints placed at the current pose. Each "
            "Gauss-Newton step reads the 4 cells around every endpoint, blends them into a "
            "smooth 'how much wall is here' value and its slope, and solves a 3 x 3 system "
            "for the move in x, y and heading that pushes all endpoints up that slope. "
            "Endpoints only feel a wall within a cell or two of it; unknown and free cells "
            "are flat and give no direction. Top: the normal guess (one scan). Middle and "
            "bottom: guesses made after 3 and 7 dropped scans. Right: distance to the truth "
            "(black) and to the pose the top row ends at (green dashed), which is the best fit "
            "this map allows. That best fit is itself ~7 cm from the truth, because the map "
            "was built from estimated poses: matching can only be as right as the map. From "
            "a guess too far off, the endpoints never feel the right walls and the match "
            "stops short or wanders.", 170)
    return fig, 0.09


def fig_keyframes(plt, d, rp):
    picks = [0, 3, 8, 13, len(rp.kf_scan)]
    fig, axs = plt.subplots(1, len(picks), figsize=(17, 4.8))
    xs = [p[0] for p in rp.gt]
    ys = [p[1] for p in rp.gt]
    for ax, n in zip(axs, picks):
        G = rp.after_kf[n]
        show_grid(ax, G)
        upto = rp.kf_scan[n - 1] if n else 0
        ax.plot([p[0] for p in rp.est[:upto + 1]], [p[1] for p in rp.est[:upto + 1]], "-",
                color=COL["est"], lw=2)
        for j in range(n):
            q = rp.kf_pose[j]
            ax.plot(q[0], q[1], "s", color=COL["est"], ms=4)
        robot(ax, rp.est[upto], COL["est"], 0.6)
        window(ax, (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2 + 0.5, 7.5, 7.5)
        ax.set_xticks([]); ax.set_yticks([])
        ax.set_title("scan 0 only (true pose)" if n == 0 else
                     f"after {n} keyframe(s), scan {upto}", fontsize=10)
    fig.suptitle("Step 6 - keyframes: the map grows as the mapper writes them", fontsize=13)
    caption(fig, "A keyframe is a tracked scan that the front-end also hands to the mapper, "
            "together with its estimated pose (squares). In the demo that is every 2nd "
            "tracked scan. Before the run, scan 0 is written at its true pose so the map "
            "starts in the right place. As the robot turns the corner, new walls only appear "
            "once a keyframe that saw them has been written. If the mapper is late, the "
            "front-end keeps matching against an older map (like the left panels) while the "
            "robot is already in the next area.", 170)
    return fig, 0.17


def fig_timeline(plt, d, rp):
    fig, axs = plt.subplots(2, 1, figsize=(15, 8.4), gridspec_kw=dict(hspace=0.5))
    for ax, name in zip(axs, ("offline", "late")):
        run = d[name]
        rel, f0, f1 = run["rel_us"], run["fe_t0_us"], run["fe_t1_us"]
        m0, m1 = run["map_t0_us"], run["map_t1_us"]
        kfs = set(run["kf_scan"])
        for k in range(1, len(rel)):
            if rel[k] < 0:
                continue
            tracked = run["tracked"][k]
            ax.plot([rel[k]] * 2, [2.6, 3.4], "-", color=COL["wall"] if tracked else COL["late"],
                    lw=1.3)
            if not tracked:
                ax.plot(rel[k], 3.55, "x", color=COL["late"], ms=6)
            if f0[k] >= 0:
                ax.add_patch(plt.Rectangle((f0[k], 1.65), f1[k] - f0[k], 0.7,
                                           color=COL["est"], alpha=0.85))
                if k in kfs:
                    ax.plot(f1[k], 1.55, "^", color=COL["beam"], ms=5)
        for a, b in zip(m0, m1):
            ax.add_patch(plt.Rectangle((a, 0.65), b - a, 0.7, color=COL["beam"], alpha=0.85))
        end = max([x for x in f1 + m1 + rel if x > 0])
        ax.set_xlim(0, end * 1.02); ax.set_ylim(0.3, 3.9)
        ax.set_yticks([1, 2, 3]); ax.set_yticklabels(["mapper", "front-end", "player"])
        ax.set_xlabel("time since the schedule started (us, host)")
        dropped = sum(1 for k in range(1, len(rel)) if not run["tracked"][k])
        title = ("no deadlines: the next scan is released only when the front-end and mapper "
                 "are done" if run["offline"] else
                 f"real time, one scan every {run['period_us']:g} us: {dropped} scans dropped "
                 "(red), keyframes wait for the mapper")
        ax.set_title(title, fontsize=10.5)
        ax.grid(axis="x", alpha=0.3)
    fig.suptitle("Step 7 - who does what, when", fontsize=13)
    caption(fig, "Player: a tick for every scan released (red with an x: the front-end never "
            "processed it, because a newer scan had arrived by the time it was free). "
            "Front-end: one bar per scan it tracked; an orange triangle marks a scan it sent "
            "to the mapper as a keyframe. Mapper: one bar per keyframe written. In the "
            "real-time run the player does not wait for anyone, so a slow front-end drops "
            "scans and a slow mapper builds a backlog. These are host timings (noisy); under "
            "gem5 the same code runs on simulated cores and the timing is deterministic.", 170)
    return fig, 0.14


def fig_result(plt, d, rp):
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(15, 6.4), gridspec_kw=dict(width_ratios=[1, 1.2]))
    late = d["late"]
    est_l = [tuple(p) for p in late["est"]]
    tr = late["tracked"]
    xs = [p[0] for p in rp.gt]
    ys = [p[1] for p in rp.gt]
    show_grid(a1, rp.after_kf[-1], alpha=0.5)
    a1.plot(xs, ys, "--", color=COL["gt"], lw=1.5, label="true path")
    a1.plot([p[0] for p in rp.est], [p[1] for p in rp.est], "-", color=COL["est"], lw=2,
            label="estimate, no deadlines")
    lk = [k for k in range(len(tr)) if tr[k]]
    a1.plot([est_l[k][0] for k in lk], [est_l[k][1] for k in lk], "-", color=COL["late"], lw=2,
            label=f"estimate, period {late['period_us']:g} us")
    for k in range(len(tr)):
        if not tr[k]:
            a1.plot(rp.gt[k][0], rp.gt[k][1], "o", mfc="none", mec=COL["late"], ms=7)
    window(a1, (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, 6.5, 6.5)
    a1.legend(loc="upper left", fontsize=9)
    a1.set_title("paths (circles: scans the late run dropped)")

    def errs(est, trk):
        out, a, b = [], 0, 0
        for k in range(1, len(rp.gt)):
            if trk[k]:
                p = est[k]
                a, b = b, k
            else:                         # what the system would have published
                f = (k - b) / (b - a) if b > a else 0
                p = (est[b][0] + (est[b][0] - est[a][0]) * f,
                     est[b][1] + (est[b][1] - est[a][1]) * f)
            out.append(math.hypot(p[0] - rp.gt[k][0], p[1] - rp.gt[k][1]))
        return out
    eo = errs(rp.est, [1] * len(rp.gt))
    el = errs(est_l, tr)
    ks = range(1, len(rp.gt))
    a2.semilogy(ks, eo, "o-", color=COL["est"], ms=3, label="no deadlines")
    a2.semilogy(ks, el, "o-", color=COL["late"], ms=3, label=f"period {late['period_us']:g} us")
    a2.set_xlabel("scan"); a2.set_ylabel("position error (m, log scale)")
    a2.grid(alpha=0.3, which="both"); a2.legend()
    a2.set_title(f"error per scan: median {np.median(eo) * 100:.0f} cm vs "
                 f"{np.median(el):.2f} m, worst {max(eo) * 100:.0f} cm vs {max(el):.1f} m")
    fig.suptitle("Step 8 - the result the demo prints: position error against the truth",
                 fontsize=13)
    caption(fig, "The demo's [ATE] line summarises the right-hand plot. With no deadlines the "
            "estimate stays within about 10 cm of the truth. In the late run, dropped scans "
            "make the guesses worse, some matches fail or lock onto the wrong walls, and "
            "those poses are written into the map by the next keyframes. From then on the "
            "robot is matching against a map that is itself wrong, and the error keeps "
            "growing. Under gem5, interference from the co-runners is what makes the "
            "front-end and mapper late.", 170)
    return fig, 0.18


# -------------------------------------------------------------------- main

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--data", default=os.path.join(here, "steps.json"))
    ap.add_argument("--out", default=os.path.join(here, "out"))
    ap.add_argument("--scan", type=int, default=20, help="scan used in steps 2, 4, 5")
    ap.add_argument("--show", action="store_true", help="also open the figures in windows")
    a = ap.parse_args()
    if not a.show:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    with open(a.data) as f:
        d = json.load(f)
    rp = Replay(d)
    bad, worst = rp.check()
    print(f"check against the C++ run: {bad} grid cells differ, "
          f"largest pose difference {worst * 1000:.4f} mm")
    os.makedirs(a.out, exist_ok=True)
    k = a.scan
    j = max(0, min(len(rp.kf_scan) - 1,
                   next((i for i, s in enumerate(rp.kf_scan) if s >= k), 0)))
    steps = [("1_world", lambda: fig_world(plt, d, rp)),
             ("2_scan", lambda: fig_scan(plt, d, rp, k)),
             ("3_mapper", lambda: fig_mapper(plt, d, rp, j)),
             ("4_guess", lambda: fig_guess(plt, d, rp, k)),
             ("5_matching", lambda: fig_matching(plt, d, rp, k)),
             ("6_keyframes", lambda: fig_keyframes(plt, d, rp)),
             ("7_timeline", lambda: fig_timeline(plt, d, rp)),
             ("8_result", lambda: fig_result(plt, d, rp))]
    for name, make in steps:
        fig, bottom = make()
        finish(fig, a.out, name, a.show, bottom)
    if a.show:
        plt.show()


if __name__ == "__main__":
    main()
