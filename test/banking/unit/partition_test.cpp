// Partitioning contract tests (docs/LLC_Banking_and_Partitioning_Design.md s6, s8.2).
//   g++ -std=c++17 -O1 -I header -I header/Partitioning test/banking/unit/partition_test.cpp src/Partitioning/*.cpp -o /tmp/pt && /tmp/pt
// Runs on EVERY registered scheme, so a new scheme is covered by adding its source file.
#include "Partitioning.h"
#include "PagePlacer.h"
#include <set>
#include <cstdio>
#include <map>
#include <random>
#include <string>
using namespace octopus;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 20) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)


// A placing scheme on address bits [lo, lo + bits): the unit is that field. Two of these on
// overlapping fields exercise joint placement (P5).
class FieldScheme : public PartitionScheme
{
public:
    FieldScheme(const char *n, int lo, int bits) : m_name(n), m_lo(lo), m_bits(bits) {}
    const char *name() const override { return m_name; }
    int units() const override { return 1 << m_bits; }
    bool placesAddresses() const override { return true; }
    uint64_t pageBytes() const override { return 4096; }
    int unitOf(uint64_t a) const override { return (int)((a >> m_lo) & ((1u << m_bits) - 1)); }
    uint64_t withUnit(uint64_t a, int u) const override
    { uint64_t m = (uint64_t)((1u << m_bits) - 1) << m_lo; return (a & ~m) | ((uint64_t)u << m_lo); }
private:
    const char *m_name; int m_lo, m_bits;
};
static void own(FieldScheme &s, const std::string &spec)
{
    auto o = PartitionRegistry::ownerships().at("static")();
    o->configure(spec, s.units());
    s.setOwnership(std::move(o), spec);
}

// The way_partition(s) parser as it was before the framework (CacheDataHandler.cpp at 4baae8dd).
static uint32_t old_allowed(const std::string &spec, uint32_t ways, int core)
{
    uint32_t all = (ways >= 32) ? 0xffffffffu : ((1u << ways) - 1), shared = all;
    std::map<int, uint32_t> mask;
    auto range = [](const std::string &s, int &lo, int &hi) {
        size_t d = s.find('-'); lo = atoi(s.c_str()); hi = (d == std::string::npos) ? lo : atoi(s.c_str() + d + 1); };
    size_t p = 0;
    while (p < spec.size())
    {
        size_t e = spec.find(';', p); if (e == std::string::npos) e = spec.size();
        std::string entry = spec.substr(p, e - p); p = e + 1;
        size_t c = entry.find(':'); if (c == std::string::npos || entry.empty()) continue;
        int c0, c1, w0, w1; range(entry.substr(0, c), c0, c1); range(entry.substr(c + 1), w0, w1);
        uint32_t m = 0; for (int w = w0; w <= w1 && w < (int)ways; w++) m |= 1u << w;
        for (int k = c0; k <= c1; k++) mask[k] = m;
        shared &= ~m;
    }
    if (!mask.empty() && shared == 0) shared = all;
    if (core < 0 || mask.empty()) return all;
    auto it = mask.find(core);
    return it != mask.end() ? it->second : shared;
}

static std::string random_spec(std::mt19937 &rng, int cores, int units)
{
    std::string s;
    int n = rng() % 4;
    for (int i = 0; i < n; i++)
    {
        int c0 = rng() % cores, c1 = c0 + rng() % 2, u0 = rng() % units, u1 = u0 + rng() % 3;   // every core owns >= 1 unit (others are refused)
        s += (i ? ";" : "") + std::to_string(c0) + (c1 > c0 ? "-" + std::to_string(c1) : "") + ":" +
             std::to_string(u0) + (u1 > u0 ? "-" + std::to_string(u1) : "");
    }
    return s;
}

int main()
{
    std::mt19937 rng(7);
    // one bank of a 4-bank, page-interleaved LLC: every scheme (way, colour, bank) is valid here
    Interleave banks; banks.ids = {10, 11, 12, 13}; banks.shift = 6;
    AddressMap map(64, 1024); map.setBanks(banks, 0);
    PartitionContext ctx; ctx.cache = "test"; ctx.ways = 8; ctx.sets = 1024; ctx.map = &map;
    const uint32_t all = 0xff;

    // 1. way scheme + static ownership == the old way_partition masks, on random specs
    for (int t = 0; t < 5000; t++)
    {
        uint32_t ways = 1 + rng() % 16;
        PartitionContext c = ctx; c.ways = ways;
        std::string spec = random_spec(rng, 4, ways);
        PartitionSet set; set.add(PartitionRegistry::build("way", "static", spec, c));
        uint32_t a = (ways >= 32) ? 0xffffffffu : ((1u << ways) - 1);
        for (int core = -1; core < 6; core++)
            CHECK(set.allowedWays(core, rng() % 256, a) == old_allowed(spec, ways, core),
                  "spec '%s' ways %u core %d: %x vs old %x", spec.c_str(), ways, core, set.allowedWays(core, 0, a), old_allowed(spec, ways, core));
    }

    // 2. every registered scheme, with static ownership
    for (auto &kv : PartitionRegistry::schemes())
    {
        const std::string key = kv.first;
        // no owners: nothing is restricted
        auto s0 = PartitionRegistry::build(key, "static", "", ctx);
        CHECK(!s0->owners().restricts(), "%s: empty owners restricts", key.c_str());
        for (int core = -1; core < 4; core++)
        {
            uint32_t m = all; s0->restrictVictims(core, 0, m);
            CHECK(m == all, "%s: empty owners restricts victims of core %d", key.c_str(), core);
            CHECK(s0->admit(core, 0x1000), "%s: empty owners refuses core %d", key.c_str(), core);
        }
        // owners: a core never gets a unit outside its share
        for (int t = 0; t < 200; t++)
        {
            std::string spec = random_spec(rng, 4, s0->units());
            auto s = PartitionRegistry::build(key, "static", spec, ctx);
            for (int core = 0; core < 4; core++)
            {
                uint32_t m = all; s->restrictVictims(core, rng() % 256, m);
                for (int w = 0; w < 8; w++)
                    if (m & (1u << w)) CHECK(s->units() != (int)ctx.ways || s->owners().owns(core, w),
                                             "%s '%s': core %d may fill way %d it does not own", key.c_str(), spec.c_str(), core, w);
            }
        }
        // placing schemes: unitOf and withUnit are inverse
        if (s0->placesAddresses())
            for (int t = 0; t < 20000; t++)
            {
                uint64_t a = (rng() | ((uint64_t)rng() << 32)) >> 16;
                int u = rng() % s0->units();
                CHECK(s0->withUnit(a, s0->unitOf(a)) == a, "%s: withUnit(a, unitOf(a)) != a", key.c_str());
                CHECK(s0->unitOf(s0->withUnit(a, u)) == u, "%s: unitOf(withUnit(a, %d)) != %d", key.c_str(), u, u);
            }
    }
    // 3. violation counting: a fill outside the requester's units is counted, per core
    {
        PartitionSet set; set.setName("test");
        set.add(PartitionRegistry::build("way", "static", "0:0;1:1-2", ctx));
        set.onFill(0, 0x40, 3, 0);    // core 0 in its way
        set.onFill(0, 0x80, 3, 5);    // core 0 outside
        set.onFill(1, 0xc0, 3, 2);    // core 1 in
        set.onFill(2, 0x100, 3, 0);   // core 2 shares ways 3-7: way 0 is outside
        set.onFill(2, 0x140, 3, 7);   // inside
        set.onFill(-1, 0x180, 3, 0);  // no requester: not counted
        CHECK(set.fills(0, 0) == 2 && set.violations(0, 0) == 1, "core 0 fills %llu violations %llu",
              (unsigned long long)set.fills(0, 0), (unsigned long long)set.violations(0, 0));
        CHECK(set.fills(0, 1) == 1 && set.violations(0, 1) == 0, "core 1");
        CHECK(set.fills(0, 2) == 2 && set.violations(0, 2) == 1, "core 2");
        CHECK(set.violations() == 2, "total violations %llu", (unsigned long long)set.violations());
    }
    // 4. joint placement (P5): A on bits 12-13 and B on bits 13-14 share bit 13.
    {
        FieldScheme a("fieldA", 12, 2), b("fieldB", 13, 2);
        // core 0: A {0,1} (bit 13 = 0), B {0,1} (bit 13 = 0 for 0, 1 for 1): 2 of 4 combinations fit;
        // core 1: A {2,3} (bit 13 = 1), B {0,2} (bit 13 = 0): none fits
        own(a, "0:0-1;1:2-3"); own(b, "0:0-1;1:0;1:2");
        PagePlacer &pp = PagePlacer::instance();
        pp.configure(true, {&a, &b}, 0);
        CHECK(pp.conflict(0).empty(), "core 0 refused: %s", pp.conflict(0).c_str());
        CHECK(!pp.conflict(1).empty(), "core 1 (units disagree on bit 13) not refused");
        std::set<uint64_t> frames; std::set<std::pair<int, int>> combos; bool inside = true;
        for (uint64_t page = 0; page < 256; page++)
        {
            uint64_t f = pp.place(0, (page << 12) | 0x48);
            inside = inside && a.owners().owns(0, a.unitOf(f)) && b.owners().owns(0, b.unitOf(f)) && (f & 0xfff) == 0x48;
            frames.insert(f >> 12); combos.insert({a.unitOf(f), b.unitOf(f)});
        }
        CHECK(inside, "a page of core 0 was placed outside its units of A or B");
        CHECK(frames.size() == 256, "pages share frames (%zu frames for 256 pages)", frames.size());
        CHECK(combos.size() == 2, "core 0 uses %zu unit combinations, 2 fit", combos.size());
        CHECK(pp.place(0, 0x5048) == pp.place(0, 0x5048), "a page placed twice moved");

        // disjoint fields (12-13, 14-15): the odometer uses all 4 x 4 combinations, each equally often
        // (lockstep per-scheme cursors used only the 4 diagonal ones)
        FieldScheme c("fieldC", 12, 2), d("fieldD", 14, 2);
        own(c, ""); own(d, "");
        pp.configure(true, {&c, &d}, 4);
        std::map<std::pair<int, int>, int> all16;
        for (uint64_t page = 0; page < 64; page++) { uint64_t f = pp.place(2, page << 12); all16[{c.unitOf(f), d.unitOf(f)}]++; }
        bool even = all16.size() == 16; for (auto &kv : all16) even = even && kv.second == 4;
        CHECK(even, "disjoint: %zu of 16 combinations used, not 4 pages each", all16.size());
        pp.configure(false, {}, 0);
    }
    CHECK(PartitionRegistry::schemes().count("way") == 1, "way scheme not registered");
    CHECK(PartitionRegistry::ownerships().count("static") == 1, "static ownership not registered");
    if (fails) printf("partition_test: %d FAILURES\n", fails);
    else printf("partition_test: PASS (%zu schemes)\n", PartitionRegistry::schemes().size());
    return fails != 0;
}
