/*
 * pingpong_synthetic.cpp
 *
 * A real multi-threaded benchmark that, as a side effect of execution, emits
 * per-thread Octopus memory-trace files. Designed to stress simultaneously
 * (a) NoC hop latency on a 2D mesh, and (b) directory-based coherence.
 *
 * Workload structure
 * ------------------
 * Threads are paired with their antipode on a sqrt(N) x sqrt(N) core grid.
 * For 4x4 (N=16): (0<->15), (1<->14), (2<->13), (3<->12),
 *                 (4<->11), (5<->10), (6<->9),  (7<->8).
 * Manhattan distance = 6 (the mesh diameter) for every pair.
 *
 * Each pair shares one cache line. Each iteration, every thread:
 *   1) Does a small amount of compute over a private working set (warm L1).
 *   2) Writes to the shared line (forces RWITM, partner invalidated).
 *   3) Reads the shared line (pulls the line back when partner has written).
 *
 * Every memory access goes through `traced_load` / `traced_store` wrappers
 * that append one line to the thread's trace file in the Octopus format:
 *
 *     <hex_addr> <ignored_int> <R|W> <cycle>
 *
 * The cycle field is a per-thread monotonic counter incremented by
 * CYCLE_STRIDE per access. Octopus uses cycle deltas (CPU.cpp:170-173) so
 * absolute values are arbitrary; only the gap matters.
 *
 * Build
 * -----
 *     g++ -std=c++17 -O2 -pthread pingpong_synthetic.cpp -o pingpong_synthetic
 *
 * Run
 * ---
 *     ./pingpong_synthetic <cores> <iters> <out_dir>
 *                          [strategy] [private_lines] [seed]
 *
 *     strategy      antipodal (worst-case hop) | neighbor (best-case) | random
 *     private_lines per-thread working-set lines (0 = pure stress; default 4)
 *     seed          RNG seed for strategy=random (default 0)
 *
 * Examples:
 *     # Worst-case sweep (16-core 4x4, antipodal):
 *     ./pingpong_synthetic 16 10000 BMs/Synthetic/16Cores/PingPong-antipodal antipodal
 *
 *     # Best-case control (neighbor pairing on same mesh):
 *     ./pingpong_synthetic 16 10000 BMs/Synthetic/16Cores/PingPong-neighbor neighbor
 *
 *     # Realistic mix (random pairing):
 *     ./pingpong_synthetic 16 10000 BMs/Synthetic/16Cores/PingPong-random random
 */

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

constexpr size_t   CACHE_LINE   = 64;
constexpr uint64_t CYCLE_STRIDE = 50;   // simulated cycles between traced accesses

// Pairing strategies. Each yields a perfect matching (partner[partner[i]] == i,
// partner[i] != i) on a rows x cols grid. They sweep the hop-distance axis:
//
//   antipodal : (r,c) <-> (rows-1-r, cols-1-c)
//               Manhattan range from inner pairs up to diameter = (rows-1)+(cols-1).
//               Corner-antipode pairs sit at the diameter; inner pairs are closer.
//               Strongest mean-distance matching achievable.
//   neighbor  : pair adjacent rows (rows must be even)
//               Manhattan = 1 uniformly. Best-case baseline.
//   random    : shuffle, then pair consecutive    - distribution depends on seed
enum class Strategy { ANTIPODAL, NEIGHBOR, RANDOM };

// ---- Cache-line-aligned data containers (avoid false sharing). -----------

struct alignas(CACHE_LINE) SharedLine {
    std::atomic<uint64_t> value;
    char pad[CACHE_LINE - sizeof(std::atomic<uint64_t>)];
};

struct alignas(CACHE_LINE) PrivateLine {
    std::atomic<int> value;
    char pad[CACHE_LINE - sizeof(std::atomic<int>)];
};

// ---- Per-thread state passed to the worker kernel. -----------------------

struct ThreadCtx {
    int            tid;
    int            partner;
    SharedLine*    shared;
    PrivateLine*   priv;            // n_private_lines entries, contiguous
    int            n_private_lines;
    FILE*          trace_file;
    uint64_t       cycle;
};

// ---- Pairing construction. Returns partner[] of length n_cores. ---------
// Cores are laid out on a rows x cols grid with linear index i = r*cols + c.

static std::vector<int> build_pairing(int n_cores, int rows, int cols,
                                      Strategy strat, uint32_t seed) {
    std::vector<int> partner(n_cores);

    switch (strat) {
        case Strategy::ANTIPODAL:
            for (int i = 0; i < n_cores; i++) {
                int r = i / cols, c = i % cols;
                partner[i] = (rows - 1 - r) * cols + (cols - 1 - c);
            }
            break;

        case Strategy::NEIGHBOR:
            if (rows % 2 != 0) {
                std::fprintf(stderr,
                             "neighbor strategy requires an even row count "
                             "(got rows=%d)\n", rows);
                std::exit(1);
            }
            for (int r = 0; r < rows; r += 2) {
                for (int c = 0; c < cols; c++) {
                    int a = r       * cols + c;
                    int b = (r + 1) * cols + c;
                    partner[a] = b;
                    partner[b] = a;
                }
            }
            break;

        case Strategy::RANDOM: {
            std::vector<int> ids(n_cores);
            for (int i = 0; i < n_cores; i++) ids[i] = i;
            std::mt19937 rng(seed);
            std::shuffle(ids.begin(), ids.end(), rng);
            for (int i = 0; i < n_cores; i += 2) {
                partner[ids[i]]     = ids[i + 1];
                partner[ids[i + 1]] = ids[i];
            }
            break;
        }
    }
    return partner;
}

static const char* strategy_name(Strategy s) {
    switch (s) {
        case Strategy::ANTIPODAL: return "antipodal";
        case Strategy::NEIGHBOR:  return "neighbor";
        case Strategy::RANDOM:    return "random";
    }
    return "?";
}

static int manhattan(int a, int b, int cols) {
    int ar = a / cols, ac = a % cols;
    int br = b / cols, bc = b % cols;
    return std::abs(ar - br) + std::abs(ac - bc);
}

// ---- Trace emission. Every memory access in the workload calls one of
//      these. fprintf doubles as an optimization barrier so the compiler
//      cannot reorder or eliminate the surrounding access. ---------------

static inline void emit_read(ThreadCtx& ctx, const void* addr) {
    ctx.cycle += CYCLE_STRIDE;
    std::fprintf(ctx.trace_file, "%llx 0 R %lld\n",
                 (unsigned long long)addr, (long long)ctx.cycle);
}
static inline void emit_write(ThreadCtx& ctx, const void* addr) {
    ctx.cycle += CYCLE_STRIDE;
    std::fprintf(ctx.trace_file, "%llx 0 W %lld\n",
                 (unsigned long long)addr, (long long)ctx.cycle);
}

template <typename T>
static inline T traced_load(ThreadCtx& ctx, const std::atomic<T>* addr,
                            std::memory_order mo = std::memory_order_acquire) {
    emit_read(ctx, addr);
    return addr->load(mo);
}
template <typename T>
static inline void traced_store(ThreadCtx& ctx, std::atomic<T>* addr, T value,
                                std::memory_order mo = std::memory_order_release) {
    addr->store(value, mo);
    emit_write(ctx, addr);
}

// ---- Worker kernel: private compute + shared ping-pong. -----------------

static void worker(ThreadCtx ctx, int iters) {
    uint64_t accum = 0;

    for (int k = 0; k < iters; k++) {
        // (1) Private compute: read-modify-write each line of the per-thread
        // working set. After warm-up these are L1 hits; first touch generates
        // an LLC miss -> DRAM. Mimics realistic compute interleaved with
        // coherence traffic. Set --private-lines=0 for pure-stress mode.
        for (int p = 0; p < ctx.n_private_lines; p++) {
            int v = traced_load(ctx, &ctx.priv[p].value);
            traced_store(ctx, &ctx.priv[p].value, v * 3 + 1);
        }

        // (2) Synchronize with partner via the shared line. The write forces
        // a RWITM (invalidate partner's M copy, pull line here); the read
        // pulls the line back after the partner writes. Together both cores
        // keep the line bouncing across the mesh diameter.
        traced_store(ctx, &ctx.shared->value,
                     (uint64_t)((static_cast<uint64_t>(ctx.tid) << 32) | k));
        accum += traced_load(ctx, &ctx.shared->value);
    }

    std::fclose(ctx.trace_file);

    // Defeat dead-store elimination on accum: print to stderr (NOT to the
    // trace file, which must contain only valid Octopus trace records --
    // CPU.cpp::readSampleFromWorkload's sscanf doesn't check its return
    // value, so a stray comment line would parse as garbage).
    std::fprintf(stderr, "tid=%d accum=%llu\n",
                 ctx.tid, (unsigned long long)accum);
}

// ---- Driver. -------------------------------------------------------------

static void usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s <cores> <rows> <iters> <out_dir> [strategy] [private_lines] [seed]\n"
        "\n"
        "  cores         number of cores (must equal rows*cols where cols = cores/rows)\n"
        "  rows          mesh row count; cols is derived as cores/rows\n"
        "  iters         ping-pong iterations per thread\n"
        "  out_dir       directory to write trace_C*.trc.shared into\n"
        "  strategy      antipodal (default) | neighbor | random\n"
        "  private_lines per-thread private working set in cache lines (default 4)\n"
        "  seed          RNG seed for --strategy=random (default 0)\n"
        "\n"
        "Examples:\n"
        "  %s 16 4 10000 BMs/Synthetic/16Cores/Antipodal antipodal 2    # 4x4 mesh\n"
        "  %s 32 8 10000 BMs/Synthetic/32Cores/Antipodal antipodal 2    # 8x4 mesh\n"
        "  %s 8  4  10000 BMs/Synthetic/8Cores/Antipodal  antipodal 2    # 4x2 mesh\n",
        prog, prog, prog, prog);
}

int main(int argc, char** argv) {
    if (argc < 5) { usage(argv[0]); return 1; }

    int         n_cores       = std::atoi(argv[1]);
    int         rows          = std::atoi(argv[2]);
    int         iters         = std::atoi(argv[3]);
    const char* out_dir       = argv[4];
    const char* strat_str     = (argc > 5) ? argv[5] : "antipodal";
    int         private_lines = (argc > 6) ? std::atoi(argv[6]) : 4;
    uint32_t    seed          = (argc > 7) ? (uint32_t)std::strtoul(argv[7], nullptr, 0)
                                           : 0u;

    // Parse strategy.
    Strategy strat;
    if      (std::strcmp(strat_str, "antipodal") == 0) strat = Strategy::ANTIPODAL;
    else if (std::strcmp(strat_str, "neighbor")  == 0) strat = Strategy::NEIGHBOR;
    else if (std::strcmp(strat_str, "random")    == 0) strat = Strategy::RANDOM;
    else {
        std::fprintf(stderr, "unknown strategy: %s\n", strat_str);
        usage(argv[0]);
        return 1;
    }

    // Validate grid: rows must divide n_cores. cols is derived.
    if (rows <= 0 || n_cores % rows != 0) {
        std::fprintf(stderr,
                     "rows must be a positive divisor of cores "
                     "(got cores=%d rows=%d)\n", n_cores, rows);
        return 1;
    }
    int cols = n_cores / rows;
    if (n_cores < 2 || (n_cores & 1)) {
        std::fprintf(stderr, "n_cores must be even and >= 2 (got %d)\n", n_cores);
        return 1;
    }
    if (private_lines < 0) {
        std::fprintf(stderr, "private_lines must be >= 0 (got %d)\n", private_lines);
        return 1;
    }

    // Best-effort mkdir -p of the output directory.
    {
        std::string cmd = "mkdir -p \"";
        cmd += out_dir;
        cmd += "\"";
        int rc = std::system(cmd.c_str());
        (void)rc;   // discard; mkdir may print its own error
    }

    // ---- Build pairing per the selected strategy. ----
    std::vector<int> partner = build_pairing(n_cores, rows, cols, strat, seed);

    // Validate matching invariant: involutive, no self-pair.
    for (int i = 0; i < n_cores; i++) {
        if (partner[i] == i || partner[partner[i]] != i) {
            std::fprintf(stderr,
                         "internal error: pairing not a perfect matching "
                         "(i=%d partner=%d partner-of-partner=%d)\n",
                         i, partner[i], partner[partner[i]]);
            return 3;
        }
    }

    // Assign pair_id deterministically: lower-id core in each pair gets the
    // pair its number, both cores look up the same id.
    int n_pairs = n_cores / 2;
    std::vector<int> pair_of(n_cores, -1);
    int next_pair = 0;
    for (int i = 0; i < n_cores; i++) {
        if (i < partner[i]) {
            pair_of[i]          = next_pair;
            pair_of[partner[i]] = next_pair;
            next_pair++;
        }
    }

    // ---- Allocate shared lines (one per pair) and private working sets. ----
    auto* shared_lines = new SharedLine[n_pairs];
    for (int p = 0; p < n_pairs; p++) shared_lines[p].value.store(0);

    auto* priv_lines = new PrivateLine[(size_t)n_cores * private_lines];
    for (size_t i = 0; i < (size_t)n_cores * private_lines; i++)
        priv_lines[i].value.store(0);

    // ---- Print plan for the user. ----
    std::printf("Strategy:      %s\n", strategy_name(strat));
    std::printf("Mesh:          %dx%d (%d cores)\n", rows, cols, n_cores);
    std::printf("Iters/thread:  %d\n", iters);
    std::printf("Private lines: %d  (per-iter accesses = %d private + 2 shared)\n",
                private_lines, 2 * private_lines);
    if (strat == Strategy::RANDOM)
        std::printf("Seed:          %u\n", seed);
    std::printf("Output:        %s\n", out_dir);

    // Compute and report hop-distance summary.
    int min_d = INT32_MAX, max_d = 0;
    long sum_d = 0;
    for (int i = 0; i < n_cores; i++) {
        int d = manhattan(i, partner[i], cols);
        if (d < min_d) min_d = d;
        if (d > max_d) max_d = d;
        sum_d += d;
    }
    std::printf("Manhattan dist: min=%d max=%d mean=%.2f (diameter=%d)\n",
                min_d, max_d, (double)sum_d / n_cores,
                (rows - 1) + (cols - 1));

    std::printf("Pairings:\n");
    for (int i = 0; i < n_cores; i++) {
        if (i < partner[i]) {
            std::printf("  C%-3d <-> C%-3d   d=%d   line=%p\n",
                        i, partner[i],
                        manhattan(i, partner[i], cols),
                        (void*)&shared_lines[pair_of[i]].value);
        }
    }

    // ---- Launch threads. Each thread opens its own trace file. ----
    std::vector<std::thread> threads;
    threads.reserve(n_cores);
    for (int i = 0; i < n_cores; i++) {
        std::string path = std::string(out_dir) + "/trace_C"
                         + std::to_string(i) + ".trc.shared";
        FILE* f = std::fopen(path.c_str(), "w");
        if (!f) {
            std::perror(path.c_str());
            return 2;
        }
        ThreadCtx ctx {
            /*tid*/             i,
            /*partner*/         partner[i],
            /*shared*/          &shared_lines[pair_of[i]],
            /*priv*/            &priv_lines[(size_t)i * private_lines],
            /*n_private_lines*/ private_lines,
            /*trace_file*/      f,
            /*cycle*/           0,
        };
        threads.emplace_back(worker, ctx, iters);
    }
    for (auto& t : threads) t.join();

    delete[] shared_lines;
    delete[] priv_lines;

    std::printf("Done. %d trace files written to %s\n", n_cores, out_dir);
    return 0;
}
