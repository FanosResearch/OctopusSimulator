// AddressMap unit test (docs/LLC_Banking_and_Partitioning_Design.md s5.5, s8.2).
//   g++ -std=c++17 -O1 -I header test/banking/unit/addressmap_test.cpp -o /tmp/am && /tmp/am
#include "AddressMap.h"
#include <cstdio>
#include <random>
#include <set>
using namespace octopus;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 20) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

// today's formulas (CacheDataHandler.h on stable)
static uint64_t old_tag(uint64_t a, uint32_t b, uint32_t s) { return a >> ((int)log2(b) + (int)log2(s)); }
static uint64_t old_set(uint64_t a, uint32_t b, uint32_t s) { return (a >> (int)log2(b)) & (s - 1); }
static uint64_t old_addr(uint64_t t, uint64_t st, uint32_t b, uint32_t s) { return (t << ((int)log2(b) + (int)log2(s))) | (st << (int)log2(b)); }

int main()
{
    std::mt19937_64 rng(1);
    const uint32_t blocks[] = {32, 64, 128};
    const uint32_t sets[] = {1, 2, 64, 1024, 16384};

    // 1. unbanked: identical to today's formulas, and address() inverts set()/tag()
    for (uint32_t b : blocks) for (uint32_t s : sets)
    {
        AddressMap m(b, s);
        for (int i = 0; i < 20000; i++)
        {
            uint64_t a = rng() >> 12;
            CHECK(m.set(a) == old_set(a, b, s), "set b=%u s=%u", b, s);
            CHECK(m.tag(a) == old_tag(a, b, s), "tag b=%u s=%u", b, s);
            CHECK(m.address(m.tag(a), m.set(a)) == old_addr(old_tag(a, b, s), old_set(a, b, s), b, s), "addr b=%u s=%u", b, s);
            CHECK(m.address(m.tag(a), m.set(a)) == (a & ~(uint64_t)(b - 1)), "roundtrip b=%u s=%u", b, s);
        }
    }

    // 2. banked: round trip, the bank digit is restored, and every set of every bank is used
    for (int n : {2, 3, 4}) for (int shift : {0, 6})
    {
        Interleave il; for (int k = 0; k < n; k++) il.ids.push_back(10 + k); il.shift = shift;
        const uint32_t b = 64, s = 256;
        for (int self = 0; self < n; self++)
        {
            AddressMap m(b, s); m.setBanks(il, self);
            std::set<uint64_t> used;
            uint64_t found = 0;
            for (uint64_t line = 0; found < (uint64_t)s * 8 * (1u << shift); line++)
            {
                if (il.index(line) != self) continue;      // only this bank's lines reach it
                found++;
                uint64_t a = line * b + (line % b);
                used.insert(m.set(a));
                CHECK(m.address(m.tag(a), m.set(a)) == line * b, "banked roundtrip n=%d shift=%d self=%d line=%llu", n, shift, self, (unsigned long long)line);
            }
            CHECK(used.size() == s, "bank n=%d shift=%d self=%d uses %zu of %u sets", n, shift, self, used.size(), s);
        }
        // strip/insert inverse for every index
        for (int i = 0; i < 20000; i++)
        {
            uint64_t line = rng() >> 20;
            CHECK(il.insert(il.strip(line), il.index(line)) == line, "strip/insert n=%d shift=%d", n, shift);
        }
    }
    printf(fails ? "addressmap_test: %d FAILURES\n" : "addressmap_test: PASS\n", fails);
    return fails != 0;
}
