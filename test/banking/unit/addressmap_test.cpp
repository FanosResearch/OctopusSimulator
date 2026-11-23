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

            // sub-banks are numbered in the bank's own line space: every sub-bank gets lines
            for (int sb : {2, 4}) for (int sbs : {0, 2})
            {
                m.setSubBanks(sb, sbs);
                std::set<int> subs; uint64_t seen = 0;
                for (uint64_t line = 0; seen < 4096; line++)
                {
                    if (il.index(line) != self) continue;
                    seen++; subs.insert(m.subBank(line * b));
                }
                CHECK((int)subs.size() == sb, "bank n=%d shift=%d self=%d sub_banks=%d shift=%d reaches %zu sub-banks", n, shift, self, sb, sbs, subs.size());
            }
        }
        // strip/insert inverse for every index
        for (int i = 0; i < 20000; i++)
        {
            uint64_t line = rng() >> 20;
            CHECK(il.insert(il.strip(line), il.index(line)) == line, "strip/insert n=%d shift=%d", n, shift);
        }
    }
    // 3. home map: matches the formula homeOf used before (line / block, then select); empty = fallback;
    //    independent of the bank group (an LLC bank's home is memory, not its peers)
    for (int n : {1, 2, 3, 4}) for (int shift : {0, 6})
    {
        Interleave il; for (int k = 0; k < n; k++) il.ids.push_back(20 + k); il.shift = shift;
        AddressMap l1(64, 64); l1.setHome(il, 99);
        AddressMap bank(64, 256); bank.setBanks(il, 0); bank.setHome(Interleave(), 7);
        for (int i = 0; i < 20000; i++)
        {
            uint64_t a = rng() >> 12;
            CHECK(l1.home(a) == il.ids[((a / 64) >> shift) % n], "home n=%d shift=%d", n, shift);
            CHECK(bank.home(a) == 7, "bank home is memory n=%d", n);
        }
        for (int k = 0; k < n; k++) CHECK(l1.isHome(20 + k), "isHome n=%d k=%d", n, k);
        CHECK(!l1.isHome(99) && !l1.isHome(19), "isHome rejects non-banks n=%d", n);
        CHECK(bank.isHome(7) && !bank.isHome(20), "bank isHome n=%d", n);
    }
    // 4. page colours: every line of a page has one colour; withColour keeps the page offset and
    //    the bank, and inverts colour(); layouts with no contiguous colour bits report -1
    for (int n : {1, 2, 3, 4, 8}) for (int shift : {0, 3, 6, 7}) for (uint32_t sets : {256u, 1024u}) for (uint64_t page : {4096ull, 8192ull})
    {
        Interleave il; for (int k = 0; k < n; k++) il.ids.push_back(30 + k); il.shift = shift;
        AddressMap m(64, sets); m.setBanks(il, 0);
        int lo = m.colourLo(page), k = m.colours(page);
        bool pow2 = (n & (n - 1)) == 0;
        int P = (int)log2((double)page) - 6;
        CHECK((lo < 0) == (n > 1 && !pow2 && shift < P), "colourLo n=%d shift=%d page=%llu: %d", n, shift, (unsigned long long)page, lo);
        if (lo < 0 || k < 2) continue;
        for (int i = 0; i < 4000; i++)
        {
            uint64_t a = (rng() >> 20) & ~63ull;
            uint64_t pg = a & ~(page - 1);
            CHECK(m.colour(a, page) == m.colour(pg, page), "page has one colour n=%d shift=%d", n, shift);
            int c = (int)(rng() % k);
            uint64_t b = m.withColour(a, c, page);
            CHECK(m.colour(b, page) == c, "withColour/colour n=%d shift=%d sets=%u", n, shift, sets);
            CHECK((b & (page - 1)) == (a & (page - 1)), "page offset kept n=%d shift=%d", n, shift);
            CHECK(il.index(b / 64) == il.index(a / 64), "bank kept n=%d shift=%d", n, shift);
            CHECK(m.withColour(a, m.colour(a, page), page) == a, "identity n=%d shift=%d", n, shift);
        }
    }
    printf(fails ? "addressmap_test: %d FAILURES\n" : "addressmap_test: PASS\n", fails);
    return fails != 0;
}
