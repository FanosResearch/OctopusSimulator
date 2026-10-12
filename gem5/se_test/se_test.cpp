// SPDX-License-Identifier: BSD-3-Clause
/*
 * se_test.cpp -- SE-mode workload for the Octopus <-> gem5 bridge.
 *
 * Self-checking and multi-threaded; the only system calls inside the ROI are
 * the clone()/futex() behind thread creation and joining.  Three phases, each
 * aimed at a path a cache model must get right:
 *
 *   P1  private streaming.  Each thread fills, then sums, its own buffer.
 *       With the default 1 MiB per thread the buffer is twice the Octopus L1,
 *       so the pass is misses, write-backs and LLC hits.  The sum has a closed
 *       form: a wrong value means a lost store or a stale load.
 *   P2  shared counter.  Every thread adds to one counter with the compiler's
 *       atomic (LSE ldadd, or ldxr/stxr without LSE); the line migrates
 *       between cores on every increment.
 *   P3  producer/consumer.  Each thread writes its slice of a shared array,
 *       all threads meet at a barrier, then each reads and checks every other
 *       thread's slice: invalidation and data forwarding between cores.
 *       Repeated for a few rounds with round-dependent values.
 *
 * The ROI (m5_work_begin .. m5_work_end) spans the three phases plus thread
 * creation and joins, like the FS runs where the whole binary is the ROI.
 * The last line is RESULT: PASS / FAIL and the exit status matches.
 *
 * Threads never exceed the number of online CPUs: gem5 SE does not preempt,
 * so a thread without a core of its own would spin forever in the barrier.
 * The main thread is worker 0, so `threads` cores are used in total.
 *
 * usage: se_test [threads] [KiB per thread] [iterations]  (default 4 1024 2000)
 * build: make (aarch64-linux-gnu-g++).  The m5ops come from gem5/util/m5's
 *        m5op.S; -DNO_M5 stubs them for a qemu-user smoke test.
 */

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

#include <unistd.h>

#ifdef NO_M5
static inline void m5_work_begin(uint64_t, uint64_t) {}
static inline void m5_work_end(uint64_t, uint64_t) {}
#else
#include <gem5/m5ops.h>
#endif

namespace {

constexpr int kMaxThreads = 64;
constexpr int kLine = 64;
constexpr size_t kSliceKiB = 64;  /* P3: shared slice per thread */
constexpr int kRounds = 4;        /* P3 rounds */

/* Keeps the compiler from forwarding stored values into the following loads
 * (the read pass must really read memory). */
#define COMPILER_BARRIER() __asm__ volatile("" ::: "memory")

/* Spin barrier: no futex, so nothing but loads and atomics inside a phase. */
class SpinBarrier
{
  public:
    explicit SpinBarrier(int n) : n_(n) {}

    void
    wait()
    {
        const unsigned gen = gen_.load(std::memory_order_acquire);
        if (count_.fetch_add(1, std::memory_order_acq_rel) == n_ - 1) {
            count_.store(0, std::memory_order_relaxed);
            gen_.store(gen + 1, std::memory_order_release);
        } else {
            while (gen_.load(std::memory_order_acquire) == gen) {
            }
        }
    }

  private:
    const int n_;
    alignas(kLine) std::atomic<int> count_{0};
    alignas(kLine) std::atomic<unsigned> gen_{0};
};

struct alignas(kLine) Counter
{
    std::atomic<uint64_t> value{0};
};

struct alignas(kLine) Result
{
    uint64_t p1_sum = 0;
    uint64_t p1_expect = 0;
    uint64_t p3_bad = 0;
};

inline uint64_t
tag(int t, size_t i, int round)
{
    return (uint64_t(round) << 48) ^ (uint64_t(t + 1) << 40) ^
           (uint64_t(i) * 0x9E3779B97F4A7C15ull);
}

void
worker(int tid, int threads, uint64_t *priv, size_t words, int iters,
       uint64_t *shared, size_t slice_words, SpinBarrier &bar,
       Counter &counter, Result &res)
{
    /* P1 */
    const uint64_t base = 0x1000 * uint64_t(tid + 1);
    for (size_t i = 0; i < words; i++)
        priv[i] = base + i;
    COMPILER_BARRIER();
    uint64_t sum = 0;
    for (size_t i = 0; i < words; i++)
        sum += priv[i];
    res.p1_sum = sum;
    res.p1_expect = base * words + (words * (words - 1)) / 2;
    bar.wait();

    /* P2 */
    for (int i = 0; i < iters; i++)
        counter.value.fetch_add(1, std::memory_order_relaxed);
    bar.wait();

    /* P3 */
    uint64_t *mine = shared + size_t(tid) * slice_words;
    uint64_t bad = 0;
    for (int r = 0; r < kRounds; r++) {
        for (size_t i = 0; i < slice_words; i++)
            mine[i] = tag(tid, i, r);
        bar.wait();
        for (int t = 0; t < threads; t++) {
            if (t == tid)
                continue;
            const uint64_t *s = shared + size_t(t) * slice_words;
            for (size_t i = 0; i < slice_words; i++)
                if (s[i] != tag(t, i, r))
                    bad++;
        }
        bar.wait(); /* nobody rewrites a slice while others still read it */
    }
    res.p3_bad = bad;
}

int fails = 0;

void
expect(const char *name, uint64_t got, uint64_t want)
{
    const bool ok = got == want;
    printf("[%s] %s: %llu (expected %llu)\n", ok ? "PASS" : "FAIL", name,
           (unsigned long long)got, (unsigned long long)want);
    if (!ok)
        fails++;
}

} // namespace

int
main(int argc, char **argv)
{
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    if (online < 1)
        online = 1;
    int threads = online < 4 ? int(online) : 4;
    size_t kib = 1024;
    int iters = 2000;

    if (argc > 1)
        threads = atoi(argv[1]);
    if (argc > 2)
        kib = strtoul(argv[2], nullptr, 10);
    if (argc > 3)
        iters = atoi(argv[3]);
    if (threads < 1)
        threads = 1;
    if (threads > kMaxThreads)
        threads = kMaxThreads;
    if (threads > online) {
        printf("[INFO] %d threads requested but %ld cpus online: using %ld\n",
               threads, online, online);
        threads = int(online);
    }
    if (kib < 1)
        kib = 1;
    if (iters < 1)
        iters = 1;

    const size_t words = kib * 1024 / sizeof(uint64_t);
    const size_t slice_words = kSliceKiB * 1024 / sizeof(uint64_t);

    printf("se_test: %d threads on %ld cpus, %zu KiB private per thread, "
           "%d atomic adds per thread, %d x %zu KiB shared slices, %d rounds\n",
           threads, online, kib, iters, threads, kSliceKiB, kRounds);

    /* Allocated before the ROI but not initialised (a std::vector would
     * zero-fill it here, on core 0): the first touch is in the worker. */
    std::unique_ptr<uint64_t[]> priv(new uint64_t[size_t(threads) * words]);
    std::unique_ptr<uint64_t[]> shared(
        new uint64_t[size_t(threads) * slice_words]);
    std::vector<Result> res(threads);
    SpinBarrier bar(threads);
    Counter counter;

    m5_work_begin(0, 0);

    std::vector<std::thread> pool;
    for (int t = 1; t < threads; t++)
        pool.emplace_back([&, t] {
            worker(t, threads, priv.get() + size_t(t) * words, words, iters,
                   shared.get(), slice_words, bar, counter, res[t]);
        });
    worker(0, threads, priv.get(), words, iters, shared.get(), slice_words,
           bar, counter, res[0]);
    for (auto &th : pool)
        th.join();

    m5_work_end(0, 0);

    int p1_ok = 0;
    uint64_t p3_bad = 0;
    for (int t = 0; t < threads; t++) {
        if (res[t].p1_sum == res[t].p1_expect)
            p1_ok++;
        else
            printf("  thread %d: P1 sum %llu, expected %llu\n", t,
                   (unsigned long long)res[t].p1_sum,
                   (unsigned long long)res[t].p1_expect);
        p3_bad += res[t].p3_bad;
    }
    expect("P1 private streaming, threads with a correct sum", p1_ok, threads);
    expect("P2 shared counter", counter.value.load(),
           uint64_t(threads) * uint64_t(iters));
    expect("P3 producer/consumer, stale words seen", p3_bad, 0);

    printf("RESULT: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
