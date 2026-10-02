// SPDX-License-Identifier: BSD-3-Clause
/*
 * slam_demo.cpp -- minimal real-time SLAM interference demo for gem5 SE mode.
 *
 * A 2D lidar SLAM has to keep up with a sensor while co-runners load the
 * shared memory system; the end metric is position error against ground
 * truth (the idea of Bechtel & Yun 2024, reduced to a proof of concept).
 *
 *   main       player: publishes scan k at k * period (simulated time)
 *   thread 1   front-end: takes the newest scan (older ones are dropped),
 *              predicts the pose with constant velocity, refines it by
 *              matching the scan against the shared occupancy grid
 *              (Gauss-Newton, Hector SLAM), sends every Nth scan to the mapper
 *   thread 2   mapper: writes each keyframe into the grid (free cells along
 *              each beam, occupied at its end)
 *   thread 3.. co-runners (--aggr N): sweep a private buffer, 64 B stride
 *
 * Timing reaches accuracy two ways: a late front-end drops scans, so its
 * prediction is further off; a late mapper leaves the front-end matching
 * against a map that lacks the area it is entering.
 *
 * Threads spin instead of sleeping: gem5 SE has no scheduler, so every
 * thread needs a core of its own (se_arm.py --num-cores >= 3 + aggr), and
 * clock_gettime follows simulated time there.  ROI = m5_reset_stats ..
 * m5_dump_stats around scans 1..N-1; scan 0 is mapped at its true pose.
 *
 * usage: slam_demo [--offline] [--period-us US] [--aggr N] [--aggr-kib K]
 *                  [--aggr-write] [--aggr-warm N] [--frames N] [--kf-every N]
 *                  [--abort-m M] [--dump]
 */

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include <time.h>

#ifdef NO_M5
static inline void m5_reset_stats(uint64_t, uint64_t) {}
static inline void m5_dump_stats(uint64_t, uint64_t) {}
#else
#include <gem5/m5ops.h>
#endif

namespace {

constexpr int kLine = 64;
constexpr int kBeams = 180;           /* 2 degree lidar */
constexpr double kRange = 10.0;       /* m */
constexpr double kRes = 0.05;         /* grid cell, m */
constexpr double kSpeed = 0.2;        /* m per scan */
constexpr double kW = 44, kH = 20, kInset = 5;   /* corridor loop, m */

int frames = 20;
int kf_every = 2;                     /* every Nth tracked scan to the mapper */
double period_us = 100;
bool offline = false;
bool dump = false;                    /* per-scan / per-keyframe lines after the report */
int n_aggr = 0;
size_t aggr_kib = 2048;
bool aggr_write = false;
int aggr_warm = 1;                    /* passes over its buffer before the ROI */
double abort_m = 0;                   /* > 0: end the run once a tracked scan is this far off */

uint64_t
now_ns()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

double
wrap(double a)
{
    while (a > M_PI)
        a -= 2 * M_PI;
    while (a <= -M_PI)
        a += 2 * M_PI;
    return a;
}

struct Pose
{
    double x = 0, y = 0, th = 0;
};

struct Rng
{
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint64_t
    next()
    {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uni(double a, double b) { return a + (b - a) * (next() >> 11) * 0x1p-53; }
    double
    gauss()
    {
        const double u1 = std::max(uni(0, 1), 1e-300), u2 = uni(0, 1);
        return std::sqrt(-2 * std::log(u1)) * std::cos(2 * M_PI * u2);
    }
};

/* ------------------------------------------------------------- scenario */

struct Seg
{
    double x0, y0, x1, y1;
};

std::vector<Seg> walls;
std::vector<Pose> gt;                 /* true pose per scan */
std::vector<float> sensor;            /* frames x kBeams, -1 = no return */

void
box(double x0, double y0, double x1, double y1)
{
    walls.push_back({x0, y0, x1, y0});
    walls.push_back({x1, y0, x1, y1});
    walls.push_back({x1, y1, x0, y1});
    walls.push_back({x0, y1, x0, y0});
}

/* Corridor loop around an inner block; the robot follows a superellipse
 * centreline (smooth turns) and weaves +-0.35 m.  Boxes line the walls. */
void
build_scenario()
{
    Rng rng(1);
    const double a = kW / 2 - kInset / 2, b = kH / 2 - kInset / 2;
    std::vector<Pose> line;
    std::vector<double> cum{0};
    for (int i = 0; i <= 8000; i++) {
        const double t = -M_PI / 2 + 2 * M_PI * i / 8000;
        const double c = std::cos(t), s = std::sin(t);
        line.push_back({kW / 2 + a * std::copysign(std::pow(std::fabs(c), 1 / 3.0), c),
                        kH / 2 + b * std::copysign(std::pow(std::fabs(s), 1 / 3.0), s),
                        0});
        if (i)
            cum.push_back(cum.back() + std::hypot(line[i].x - line[i - 1].x,
                                                  line[i].y - line[i - 1].y));
    }
    auto at = [&](double s) {
        const size_t j = std::min<size_t>(
            std::upper_bound(cum.begin(), cum.end(), s) - cum.begin(),
            line.size() - 1);
        return line[j];
    };

    /* Start on the bottom straight, a few metres before its corner. */
    double s = cum.back() * 0.14;
    for (int k = 0; k < frames; k++) {
        const Pose c = at(s), n = at(s + 0.05);
        const double hd = std::atan2(n.y - c.y, n.x - c.x);
        const double lat = 0.35 * std::sin(0.23 * k);
        const double dlat = 0.35 * 0.23 * std::cos(0.23 * k);
        gt.push_back({c.x - lat * std::sin(hd), c.y + lat * std::cos(hd),
                      wrap(hd + std::atan2(dlat, kSpeed))});
        s += kSpeed * (1 + 0.25 * std::sin(2 * M_PI * k / 37.0));
    }

    box(0, 0, kW, kH);
    box(kInset, kInset, kW - kInset, kH - kInset);
    for (int tries = 0, placed = 0; tries < 4000 && placed < 140; tries++) {
        const double x = rng.uni(0.2, kW - 0.2), y = rng.uni(0.2, kH - 0.2);
        const double w = rng.uni(0.2, 0.8), h = rng.uni(0.2, 0.8);
        const double x0 = x - w / 2, y0 = y - h / 2, x1 = x + w / 2, y1 = y + h / 2;
        if (x1 > kInset && x0 < kW - kInset && y1 > kInset && y0 < kH - kInset)
            continue;                 /* inside or touching the block */
        if (x0 < 0.05 || y0 < 0.05 || x1 > kW - 0.05 || y1 > kH - 0.05)
            continue;
        bool clear = true;            /* keep 1.1 m off the centreline */
        for (size_t i = 0; clear && i < line.size(); i += 20) {
            const double px = std::clamp(line[i].x, x0, x1) - line[i].x;
            const double py = std::clamp(line[i].y, y0, y1) - line[i].y;
            clear = px * px + py * py > 1.1 * 1.1;
        }
        if (clear) {
            box(x0, y0, x1, y1);
            placed++;
        }
    }

    /* Ray-cast the scans.  Each segment in range only visits the beams
     * inside the angle it spans; 1 cm range noise. */
    const double dth = 2 * M_PI / kBeams;
    std::vector<double> best(kBeams), bc(kBeams), bs(kBeams);
    sensor.assign(size_t(frames) * kBeams, -1.0f);
    for (int k = 0; k < frames; k++) {
        const Pose &p = gt[k];
        for (int i = 0; i < kBeams; i++) {
            best[i] = kRange;
            bc[i] = std::cos(p.th - M_PI + i * dth);
            bs[i] = std::sin(p.th - M_PI + i * dth);
        }
        for (const Seg &g : walls) {
            const double ex = g.x1 - g.x0, ey = g.y1 - g.y0;
            const double qx = g.x0 - p.x, qy = g.y0 - p.y;
            const double l2 = ex * ex + ey * ey;
            const double u0 = std::clamp(-(qx * ex + qy * ey) / l2, 0.0, 1.0);
            const double cx = qx + u0 * ex, cy = qy + u0 * ey;
            if (cx * cx + cy * cy > kRange * kRange)
                continue;             /* nearest point out of range */
            const double a0 = std::atan2(qy, qx) - p.th;
            const double span = wrap(std::atan2(qy + ey, qx + ex) - p.th - a0);
            const double lo = wrap(span > 0 ? a0 : a0 + span);
            const int i0 = int(std::ceil((lo + M_PI) / dth));
            const int n = int(std::fabs(span) / dth) + 2;
            for (int m = -1; m < n; m++) {
                const int i = ((i0 + m) % kBeams + kBeams) % kBeams;
                const double den = bc[i] * ey - bs[i] * ex;
                if (std::fabs(den) < 1e-12)
                    continue;
                const double t = (qx * ey - qy * ex) / den;
                const double u = (qx * bs[i] - qy * bc[i]) / den;
                if (t > 0 && u >= 0 && u <= 1 && t < best[i])
                    best[i] = t;
            }
        }
        for (int i = 0; i < kBeams; i++)
            if (best[i] < kRange)
                sensor[size_t(k) * kBeams + i] = float(best[i] + 0.01 * rng.gauss());
    }
}

/* ------------------------------------------------------------------ map */

/* Occupancy grid, log-odds x10 in int8.  The mapper is the only writer; the
 * front-end reads it concurrently (relaxed atomics, as a real single-map
 * SLAM would), so map updates reach the front-end as coherence traffic. */
int gw, gh;
std::unique_ptr<std::atomic<int8_t>[]> grid;
std::vector<uint16_t> stamp;          /* mapper-private: one update per scan */
float prob[256];

float
cellp(int x, int y)
{
    return prob[grid[size_t(y) * gw + x].load(std::memory_order_relaxed) + 128];
}

void
bump(int x, int y, int d, uint16_t id)
{
    if (x < 0 || y < 0 || x >= gw || y >= gh)
        return;
    const size_t i = size_t(y) * gw + x;
    if (stamp[i] == id)
        return;
    stamp[i] = id;
    const int v = std::clamp(grid[i].load(std::memory_order_relaxed) + d, -50, 50);
    grid[i].store(int8_t(v), std::memory_order_relaxed);
}

/* Endpoints first (+0.9 log-odds), then free space along each beam (-0.4). */
void
integrate(const Pose &p, const float *r, uint16_t id)
{
    auto cell = [](double v) { return int(std::lround(v / kRes)); };
    const int rx = cell(p.x), ry = cell(p.y);
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < kBeams; i++) {
            if (pass == 0 && r[i] < 0)
                continue;
            const double len = r[i] < 0 ? kRange : r[i];
            const double ang = p.th - M_PI + i * 2 * M_PI / kBeams;
            const int ex = cell(p.x + len * std::cos(ang));
            const int ey = cell(p.y + len * std::sin(ang));
            if (pass == 0) {
                bump(ex, ey, 9, id);
                continue;
            }
            int x = rx, y = ry, err = std::abs(ex - rx) - std::abs(ey - ry);
            while (x != ex || y != ey) {          /* Bresenham */
                bump(x, y, -4, id);
                const int e2 = 2 * err;
                if (e2 >= -std::abs(ey - ry)) {
                    err -= std::abs(ey - ry);
                    x += rx < ex ? 1 : -1;
                }
                if (e2 <= std::abs(ex - rx)) {
                    err += std::abs(ex - rx);
                    y += ry < ey ? 1 : -1;
                }
            }
        }
    }
}

/* Gauss-Newton on sum (1 - M(beam endpoint))^2 with bilinear M (Hector SLAM
 * eq. 7-13).  Returns the beams that saw mapped structure. */
int
match(const float *sx, const float *sy, int n, Pose &p)
{
    int used = 0;
    for (int it = 0; it < 8; it++) {
        double H[3][3] = {}, g[3] = {};
        const double c = std::cos(p.th), s = std::sin(p.th);
        used = 0;
        for (int i = 0; i < n; i++) {
            const double mx = (c * sx[i] - s * sy[i] + p.x) / kRes;
            const double my = (s * sx[i] + c * sy[i] + p.y) / kRes;
            const int x0 = int(std::floor(mx)), y0 = int(std::floor(my));
            if (x0 < 0 || y0 < 0 || x0 + 1 >= gw || y0 + 1 >= gh)
                continue;
            const double fx = mx - x0, fy = my - y0;
            const double p00 = cellp(x0, y0), p10 = cellp(x0 + 1, y0);
            const double p01 = cellp(x0, y0 + 1), p11 = cellp(x0 + 1, y0 + 1);
            const double dx = ((1 - fy) * (p10 - p00) + fy * (p11 - p01)) / kRes;
            const double dy = ((1 - fx) * (p01 - p00) + fx * (p11 - p10)) / kRes;
            if (dx == 0 && dy == 0)
                continue;             /* unmapped: no information */
            used++;
            const double m = (1 - fy) * ((1 - fx) * p00 + fx * p10) +
                             fy * ((1 - fx) * p01 + fx * p11);
            const double J[3] = {dx, dy,
                                 dx * (-s * sx[i] - c * sy[i]) +
                                     dy * (c * sx[i] - s * sy[i])};
            for (int r = 0; r < 3; r++) {
                g[r] += J[r] * (1 - m);
                for (int q = 0; q < 3; q++)
                    H[r][q] += J[r] * J[q];
            }
        }
        if (used < 10)
            return used;
        for (int r = 0; r < 3; r++)
            H[r][r] += 1e-3 * H[r][r] + 1e-6;     /* damping */
        /* 3x3 solve by Cramer's rule. */
        auto det3 = [](double m[3][3]) {
            return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                   m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                   m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        };
        const double d = det3(H);
        if (std::fabs(d) < 1e-12)
            return used;
        double step[3];
        for (int col = 0; col < 3; col++) {
            double M[3][3];
            memcpy(M, H, sizeof(M));
            for (int r = 0; r < 3; r++)
                M[r][col] = g[r];
            step[col] = det3(M) / d;
        }
        p.x += step[0];
        p.y += step[1];
        p.th = wrap(p.th + step[2]);
    }
    return used;
}

/* ------------------------------------------------------------- pipeline */

std::vector<float> published;         /* scan k as delivered by the player */
alignas(kLine) std::atomic<int> released{0};   /* newest scan published */
alignas(kLine) std::atomic<int> fe_done{0};    /* newest scan tracked */
alignas(kLine) std::atomic<int> kf_tail{0};    /* keyframes queued */
alignas(kLine) std::atomic<int> kf_head{0};    /* keyframes mapped */
alignas(kLine) std::atomic<bool> fe_exit{false};
alignas(kLine) std::atomic<int> aborted_at{0};  /* scan whose error passed abort_m */
alignas(kLine) std::atomic<int> ready{0};      /* pipeline threads started */
alignas(kLine) std::atomic<bool> aggr_stop{false};

std::vector<int> kf_scan;             /* keyframe -> scan index */
std::vector<Pose> kf_pose;
std::vector<Pose> est;
std::vector<char> tracked;
std::vector<char> lost_at;             /* scan whose match was discarded (coasted) */
std::vector<double> fe_us, map_us;
int map_backlog = 0, lost = 0;
/* Timestamps (ns) for the step-by-step visualisation; stored, not printed. */
uint64_t roi_t0;
std::vector<uint64_t> rel_ns, fe_t0, fe_t1, map_t0, map_t1;

void
front_end()
{
    std::vector<float> sx(kBeams), sy(kBeams);
    int last = 0, since_kf = 0;
    ready.fetch_add(1, std::memory_order_release);
    Pose vel{gt[1].x - gt[0].x, gt[1].y - gt[0].y, wrap(gt[1].th - gt[0].th)};
    for (;;) {
        int k;
        while ((k = released.load(std::memory_order_acquire)) == last) {
        }
        if (k >= frames)
            break;                    /* end-of-run sentinel */
        const uint64_t t0 = now_ns();

        const int gap = k - last;     /* > 1: scans were dropped */
        Pose p = est[last];
        p.x += vel.x * gap;
        p.y += vel.y * gap;
        p.th = wrap(p.th + vel.th * gap);
        const Pose pred = p;

        const float *r = &published[size_t(k) * kBeams];
        int n = 0;
        for (int i = 0; i < kBeams; i++) {
            if (r[i] < 0)
                continue;
            const double ang = -M_PI + i * 2 * M_PI / kBeams;
            sx[n] = float(r[i] * std::cos(ang));
            sy[n] = float(r[i] * std::sin(ang));
            n++;
        }
        if (match(sx.data(), sy.data(), n, p) < 10 ||
            std::hypot(p.x - pred.x, p.y - pred.y) > 1.0) {
            p = pred;                 /* tracking lost: coast */
            lost++;
            lost_at[k] = 1;
        }
        vel = {(p.x - est[last].x) / gap, (p.y - est[last].y) / gap,
               wrap(p.th - est[last].th) / gap};
        est[k] = p;
        tracked[k] = 1;
        last = k;
        if (abort_m > 0 && !aborted_at.load(std::memory_order_relaxed) &&
            std::hypot(p.x - gt[k].x, p.y - gt[k].y) > abort_m)
            aborted_at.store(k, std::memory_order_release);

        if (++since_kf == kf_every) {
            since_kf = 0;
            const int t = kf_tail.load(std::memory_order_relaxed);
            kf_scan[t] = k;
            kf_pose[t] = p;
            kf_tail.store(t + 1, std::memory_order_release);
        }
        const uint64_t t1 = now_ns();
        fe_us[k] = (t1 - t0) / 1000.0;
        fe_t0[k] = t0;
        fe_t1[k] = t1;
        fe_done.store(k, std::memory_order_release);
    }
    fe_exit.store(true, std::memory_order_release);
}

void
mapper()
{
    uint16_t id = 1;
    ready.fetch_add(1, std::memory_order_release);
    for (;;) {
        const int h = kf_head.load(std::memory_order_relaxed);
        int t;
        while ((t = kf_tail.load(std::memory_order_acquire)) == h &&
               !fe_exit.load(std::memory_order_acquire)) {
        }
        if (t == h || fe_exit.load(std::memory_order_acquire))
            break;                    /* a backlog at the end changes nothing */
        map_backlog = std::max(map_backlog, t - h);
        const uint64_t t0 = now_ns();
        integrate(kf_pose[h], &published[size_t(kf_scan[h]) * kBeams], ++id);
        const uint64_t t1 = now_ns();
        map_us.push_back((t1 - t0) / 1000.0);
        map_t0.push_back(t0);
        map_t1.push_back(t1);
        kf_head.store(h + 1, std::memory_order_release);
    }
}

struct alignas(kLine) Counter
{
    std::atomic<uint64_t> bytes{0};
};
Counter aggr_bytes[8];

void
aggressor(int id, uint8_t *buf, size_t size)
{
    uint64_t sum = 0;
    while (!aggr_stop.load(std::memory_order_relaxed)) {
        for (size_t base = 0; base < size; base += 4096) {
            for (size_t i = base; i < base + 4096; i += kLine) {
                if (aggr_write)
                    buf[i] = uint8_t(i);
                else
                    sum += buf[i];
            }
            aggr_bytes[id].bytes.fetch_add(4096, std::memory_order_relaxed);
        }
    }
    __asm__ volatile("" ::"r"(sum));
}

double
pct(std::vector<double> v, double f)
{
    if (v.empty())
        return 0;
    std::sort(v.begin(), v.end());
    return v[size_t(f * (v.size() - 1) + 0.5)];
}

} // namespace

int
main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const bool more = i + 1 < argc;
        if (!strcmp(argv[i], "--offline"))
            offline = true;
        else if (!strcmp(argv[i], "--dump"))
            dump = true;
        else if (!strcmp(argv[i], "--aggr-write"))
            aggr_write = true;
        else if (!strcmp(argv[i], "--period-us") && more)
            period_us = atof(argv[++i]);
        else if (!strcmp(argv[i], "--aggr") && more)
            n_aggr = std::clamp(atoi(argv[++i]), 0, 8);
        else if (!strcmp(argv[i], "--aggr-kib") && more)
            aggr_kib = strtoul(argv[++i], nullptr, 10);
        else if (!strcmp(argv[i], "--aggr-warm") && more)
            aggr_warm = std::max(0, atoi(argv[++i]));
        else if (!strcmp(argv[i], "--abort-m") && more)
            abort_m = atof(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && more)
            frames = std::max(3, atoi(argv[++i]));
        else if (!strcmp(argv[i], "--kf-every") && more)
            kf_every = std::max(1, atoi(argv[++i]));
        else {
            printf("usage: %s [--offline] [--period-us US] [--aggr N] "
                   "[--aggr-kib K] [--aggr-write] [--aggr-warm N] [--frames N] [--kf-every N] "
                   "[--abort-m M] [--dump]\n", argv[0]);
            return 2;
        }
    }
    printf("slam_demo: %d scans, period %.0f us%s, keyframe every %d, "
           "%d co-runner(s) x %zu KiB %s\n",
           frames, period_us, offline ? " (offline: no deadlines)" : "", kf_every,
           n_aggr, aggr_kib, aggr_write ? "write" : "read");

    /* ---- setup, outside the ROI */
    build_scenario();
    for (int v = -128; v < 128; v++)
        prob[v + 128] = float(1 / (1 + std::exp(-v / 10.0)));
    gw = int(kW / kRes) + 2;
    gh = int(kH / kRes) + 2;
    grid.reset(new std::atomic<int8_t>[size_t(gw) * gh]);
    for (size_t i = 0; i < size_t(gw) * gh; i++)
        grid[i].store(0, std::memory_order_relaxed);
    stamp.assign(size_t(gw) * gh, 0);
    published.assign(sensor.size(), 0.0f);
    est.assign(frames, Pose());
    tracked.assign(frames, 0);
    lost_at.assign(frames, 0);
    fe_us.assign(frames, 0);
    kf_scan.assign(frames, 0);
    rel_ns.assign(frames, 0);
    fe_t0.assign(frames, 0);
    fe_t1.assign(frames, 0);
    map_t0.reserve(frames);
    map_t1.reserve(frames);
    kf_pose.assign(frames, Pose());
    est[0] = gt[0];
    tracked[0] = 1;
    integrate(gt[0], &sensor[0], 1);

    std::vector<std::unique_ptr<uint8_t[]>> bufs;
    std::vector<std::thread> aggr;
    for (int i = 0; i < n_aggr; i++) {
        bufs.emplace_back(new uint8_t[aggr_kib * 1024]);
        aggr.emplace_back(aggressor, i, bufs.back().get(), aggr_kib * 1024);
    }
    for (int i = 0; i < n_aggr; i++)          /* aggr_warm passes: warmed up */
        while (aggr_bytes[i].bytes.load() < size_t(aggr_warm) * aggr_kib * 1024) {
        }

    /* ---- ROI: main is the player */
    std::vector<uint64_t> b0(n_aggr);
    for (int i = 0; i < n_aggr; i++)
        b0[i] = aggr_bytes[i].bytes.load();
    m5_reset_stats(0, 0);
    std::thread tm(mapper), tf(front_end);
    /* The schedule starts once both threads run, so thread creation cannot
     * make the first scans late. */
    while (ready.load(std::memory_order_acquire) != 2) {
    }
    const uint64_t t0 = now_ns();
    roi_t0 = t0;
    const uint64_t period = uint64_t(period_us * 1000);
    for (int k = 1; k <= frames; k++) {
        if (aborted_at.load(std::memory_order_acquire)) {
            released.store(frames, std::memory_order_release);  /* end now */
            break;
        }
        if (offline) {
            while (fe_done.load(std::memory_order_acquire) != k - 1 ||
                   kf_head.load(std::memory_order_acquire) !=
                       kf_tail.load(std::memory_order_acquire)) {
            }
        } else {
            while (now_ns() < t0 + k * period) {
            }
        }
        if (k < frames) {
            memcpy(&published[size_t(k) * kBeams], &sensor[size_t(k) * kBeams],
                   sizeof(float) * kBeams);
            rel_ns[k] = now_ns();
        }
        released.store(k, std::memory_order_release);  /* k == frames: end */
    }
    tf.join();
    tm.join();
    const uint64_t t1 = now_ns();
    if (const int a = aborted_at.load()) {   /* report the scans up to the abort */
        printf("[ABORT] scan %d: position error over %.2f m, run ended there\n", a, abort_m);
        frames = a + 1;
    }
    m5_dump_stats(0, 0);
    std::vector<uint64_t> b1(n_aggr);
    for (int i = 0; i < n_aggr; i++)
        b1[i] = aggr_bytes[i].bytes.load();
    aggr_stop.store(true);
    for (auto &t : aggr)
        t.join();

    /* ---- report.  A dropped scan is scored by the pose the system would
     * have published for it: constant velocity from the last two tracked. */
    int n_tracked = 0, a = 0, b = 0;
    std::vector<double> err, fe;
    std::vector<Pose> pub(frames);        /* the pose the system published per scan */
    pub[0] = est[0];
    double sq = 0;
    for (int k = 1; k < frames; k++) {
        Pose p;
        if (tracked[k]) {
            n_tracked++;
            fe.push_back(fe_us[k]);
            p = est[k];
            a = b;
            b = k;
        } else {
            const double f = b > a ? double(k - b) / (b - a) : 0;
            p.x = est[b].x + (est[b].x - est[a].x) * f;
            p.y = est[b].y + (est[b].y - est[a].y) * f;
        }
        const double e = std::hypot(p.x - gt[k].x, p.y - gt[k].y);
        pub[k] = p;
        err.push_back(e);
        sq += e * e;
    }
    printf("[RT]   roi %.2f ms, scans tracked %d/%d, dropped %d, tracking lost %d, "
           "keyframes mapped %zu/%d, max mapper backlog %d\n",
           (t1 - t0) / 1e6, n_tracked, frames - 1, frames - 1 - n_tracked, lost,
           map_us.size(), kf_tail.load(), map_backlog);
    printf("[TIME] front-end us per scan: p50 %.1f max %.1f; mapper us per "
           "keyframe: p50 %.1f max %.1f\n",
           pct(fe, 0.5), pct(fe, 1), pct(map_us, 0.5), pct(map_us, 1));
    for (int i = 0; i < n_aggr; i++)
        printf("[AGGR] co-runner %d: %.0f MB/s\n", i,
               (b1[i] - b0[i]) / 1e6 / ((t1 - t0) / 1e9));
    printf("[ATE]  position error: median %.3f m, rmse %.3f m, max %.3f m\n",
           pct(err, 0.5), std::sqrt(sq / err.size()), pct(err, 1));
    if (dump) {
        /* SCAN,k,tracked,lost,gt_x,gt_y,est_x,est_y,err_m,fe_us  (dropped: tracked 0) */
        for (int k = 0; k < frames; k++)
            printf("SCAN,%d,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f\n", k, int(tracked[k]),
                   int(lost_at[k]), gt[k].x, gt[k].y, pub[k].x, pub[k].y,
                   std::hypot(pub[k].x - gt[k].x, pub[k].y - gt[k].y), fe_us[k]);
        /* KF,j,scan,map_us  (keyframes the mapper wrote before the end) */
        for (size_t j = 0; j < map_us.size(); j++)
            printf("KF,%zu,%d,%.2f\n", j, kf_scan[j], map_us[j]);
    }
    return 0;
}
