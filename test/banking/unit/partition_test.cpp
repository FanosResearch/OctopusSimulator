// Partitioning contract tests (docs/LLC_Banking_and_Partitioning_Design.md s6, s8.2).
//   g++ -std=c++17 -O1 -I header -I header/Partitioning test/banking/unit/partition_test.cpp src/Partitioning/*.cpp -o /tmp/pt && /tmp/pt
// Runs on EVERY registered scheme, so a new scheme is covered by adding its source file.
#include "Partitioning.h"
#include "PagePlacer.h"
#include "DramMap.h"
#include <set>
#include <sstream>
#include <cstdio>
#include <map>
#include <random>
#include <string>
using namespace octopus;
// the sender's gate with only `core`'s own counts (queued, outstanding)
static bool adm(const PartitionSet &s, int core, int q, int o) { return s.admitsDemand(core, {{core, q}}, {{core, o}}); }

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
    // a DRAM layout as MCsim's FRFCFS mapping of DDR4 8Gb x8 gives it: bank bits 13-14, group 15-16
    DramMap::instance().set(13, 2, 15, 2, [](uint64_t x, unsigned &bg, unsigned &b) { b = (x >> 13) & 3; bg = (x >> 15) & 3; });
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
                    if (m & (1u << w)) CHECK(s->countsEntries() || s->units() != (int)ctx.ways || s->owners().owns(core, w),
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
        // unplace() inverts place() (the gem5 bridge maps every address handed back to the guest's
        // own): every placed line comes back to its original address, offset included; an address
        // never placed passes through
        bool back = true;
        for (uint64_t page = 0; page < 256; page++)
            for (uint64_t off : {0x0ull, 0x48ull, 0xfc0ull})
                back = back && pp.unplace(pp.place(0, (page << 12) | off)) == ((page << 12) | off);
        CHECK(back, "unplace(place(a)) != a");
        CHECK(pp.unplace(0x7fff00000048ull) == 0x7fff00000048ull, "an unplaced address changed");

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
        CHECK(pp.unplace(0x5048) == 0x5048 && pp.place(1, 0x5048) == 0x5048, "placer off: addresses changed");
    }
    // 5. MSHR partition (P7): 8 entries, core 0 owns 0-1, cores 1-2 share 2-7, core 3 unlisted
    //    (every unit is claimed, so it shares all of them)
    {
        PartitionContext c = ctx; c.mshr_entries = 8;
        PartitionSet set; set.setName("test");
        set.add(PartitionRegistry::build("mshr", "static", "0:0-1;1-2:2-7", c));
        using E = PartitionScheme::Entry;
        CHECK(set.admit(0, 0) && set.admit(1, 0), "empty: refused");
        set.allocate(E::MSHR, 0, 0x1000); set.allocate(E::MSHR, 0, 0x2000);
        CHECK(!set.admit(0, 0), "core 0 admitted with both its units held");
        CHECK(set.admit(1, 0) && set.admit(-1, 0), "core 1 / no requester refused while its units are free");
        set.allocate(E::PWB, 0, 0x3000);   // another kind of entry: not this scheme's
        CHECK(set.fills(0, 0) == 2, "a PWB entry counted by the mshr scheme");
        set.allocate(E::MSHR, 0, 0x1000);  // the same key again (no new entry)
        CHECK(!set.admit(0, 0) && set.violations() == 0, "re-opening a held key changed the count");
        set.release(E::MSHR, 0x1000);
        CHECK(set.admit(0, 0), "core 0 refused after a release");
        for (int i = 0; i < 6; i++) set.allocate(E::MSHR, 1 + i % 2, 0x10000 + i * 64);
        CHECK(!set.admit(1, 0) && !set.admit(2, 0), "cores 1-2 admitted with units 2-7 held");
        CHECK(set.admit(0, 0) && set.admit(3, 0), "core 0 / core 3 refused with unit 0 free");
        set.allocate(E::MSHR, 2, 0x20000);  // core 2 without a free unit: a violation
        CHECK(set.violations(0, 2) == 1 && set.fills(0, 2) == 4, "core 2: fills %llu violations %llu",
              (unsigned long long)set.fills(0, 2), (unsigned long long)set.violations(0, 2));
        set.release(E::MSHR, 0x20000); set.release(E::MSHR, 0x10000);   // the violating entry held no unit
        CHECK(set.admit(1, 0), "core 1 refused after unit 2 freed");
        CHECK(set.admit(2, 0), "core 2 refused: it shares the freed unit 2");
        set.release(E::MSHR, 0x999999);      // an unknown key is ignored
        CHECK(set.violations() == 1, "total violations %llu", (unsigned long long)set.violations());
        PartitionContext u = ctx; u.mshr_entries = -1;
        CHECK(!PartitionRegistry::schemes().at("mshr")(u)->invalid().empty(), "unbounded num_mshr not refused");
        // the sender's gate: core 0 holds unit 1 (0x2000), so 1 unit is free
        CHECK(set.gatesDemand(), "an mshr scheme does not gate demand");
        CHECK(adm(set, 0, 0, 5) && !adm(set, 0, 1, 0), "mshr gate: 1 free unit, queued 0 / 1");
    }
    // 6. write-back buffer partition (P8): 4 entries, core 0 owns 0-1, core 1 owns 2-3
    {
        using E = PartitionScheme::Entry;
        PartitionContext c = ctx; c.pwb_entries = 4;
        PartitionSet req; req.setName("test");
        req.add(PartitionRegistry::build("pwb", "static", "0:0-1;1:2-3", c));
        CHECK(req.gatesDemand(), "pwb requester does not gate demand");
        req.allocate(E::MSHR, 0, 0x1000);   // an MSHR entry: not this scheme's
        CHECK(req.fills(0, 0) == 0, "an MSHR entry counted by the pwb scheme");
        req.allocate(E::PWB, 0, 0x1000, 1);  // core 0's fill evicts core 1's line: charged to core 0
        CHECK(req.fills(0, 0) == 1 && req.fills(0, 1) == 0, "requester mode charged the victim");
        // core 0: 1 free unit; it may send while queued + open misses < 1
        CHECK(adm(req, 0, 0, 0) && !adm(req, 0, 0, 1) && !adm(req, 0, 1, 0),
              "pwb gate counts queued requests and open misses");
        CHECK(adm(req, 1, 1, 0), "core 1 refused with 2 free units");
        req.release(E::PWB, 0x1000);
        CHECK(adm(req, 0, 1, 0), "core 0 refused after its write-back completed");

        PartitionContext v = c; v.pwb_owner = "victim";
        PartitionSet vic; vic.setName("test");
        vic.add(PartitionRegistry::build("pwb", "static", "0:0-1;1:2-3", v));
        CHECK(!vic.gatesDemand(), "pwb victim gates demand (the victim is known only at fill time)");
        vic.allocate(E::PWB, 0, 0x1000, 1); vic.allocate(E::PWB, 0, 0x2000, 1); vic.allocate(E::PWB, 0, 0x3000, 1);
        CHECK(vic.fills(0, 1) == 3 && vic.fills(0, 0) == 0, "victim mode: fills core0 %llu core1 %llu",
              (unsigned long long)vic.fills(0, 0), (unsigned long long)vic.fills(0, 1));
        CHECK(vic.violations(0, 1) == 1, "victim mode: core 1's third entry is not a violation");
        vic.allocate(E::PWB, 0, 0x4000, -1);  // unknown placer: counted for nobody
        CHECK(vic.violations() == 1, "an entry of unknown placer counted");

        PartitionContext bad = c; bad.pwb_owner = "owner";
        CHECK(!PartitionRegistry::schemes().at("pwb")(bad)->invalid().empty(), "pwb_owner 'owner' not refused");
        PartitionContext u = c; u.pwb_entries = -1;
        CHECK(!PartitionRegistry::schemes().at("pwb")(u)->invalid().empty(), "unbounded pwb_size not refused");
    }
    // 7. cat ownership (P9): hex capacity masks, unlisted cores own everything, contiguity,
    //    run-time schedule; with every core listed and disjoint masks it owns as static does
    {
        auto cat = [](const std::string &owners, int units, bool contiguous = true, const std::string &sched = "") {
            PartitionContext c; c.contiguous = contiguous; c.schedule = sched;
            auto o = PartitionRegistry::ownerships().at("cat")();
            o->setContext(c); o->configure(owners, units);
            return o;
        };
        auto o = cat("0:0x00f;1:0x0f0;2-3:0xff0", 12);
        CHECK(o->invalid().empty(), "valid masks refused: %s", o->invalid().c_str());
        CHECK(o->owns(0, 3) && !o->owns(0, 4) && o->owns(1, 4) && !o->owns(1, 8), "core 0 / 1 masks");
        CHECK(o->owns(2, 4) && o->owns(3, 11) && !o->owns(2, 3), "cores 2-3 mask 0xff0");
        CHECK(o->owns(5, 0) && o->owns(5, 11) && o->owns(-1, 0), "an unlisted core does not own every unit");
        CHECK(o->restricts() && !o->ticks(), "restricts / ticks");
        CHECK(!cat("0:0xfff", 12)->restricts(), "a full mask restricts");
        CHECK(!cat("0:0x5", 4)->invalid().empty(), "non-contiguous mask 0x5 not refused");
        CHECK(cat("0:0x5", 4, false)->invalid().empty(), "contiguous(i),0 still refuses 0x5");
        CHECK(!cat("0:0x0", 4)->invalid().empty(), "empty mask not refused");
        CHECK(!cat("0:0x10", 4)->invalid().empty(), "a bit past the units not refused");
        CHECK(!cat("0:zz", 4)->invalid().empty(), "a non-hex mask not refused");
        // disjoint masks for every core: the same owners as static, for every scheme's units
        for (auto &kv : PartitionRegistry::schemes())
        {
            int units = kv.second(ctx)->units();
            if (units > 64 || units < 4) continue;
            int q = units / 4; std::string cs, ss;
            for (int c = 0; c < 4; c++)
            {
                int lo = c * q, hi = (c == 3) ? units - 1 : (c + 1) * q - 1;
                uint64_t m = 0; for (int u = lo; u <= hi; u++) m |= 1ull << u;
                char b[64]; snprintf(b, sizeof b, "%d:0x%llx;", c, (unsigned long long)m); cs += b;
                ss += std::to_string(c) + ":" + std::to_string(lo) + "-" + std::to_string(hi) + ";";
            }
            auto a = PartitionRegistry::build(kv.first, "cat", cs, ctx), s = PartitionRegistry::build(kv.first, "static", ss, ctx);
            for (int c = -1; c < 4; c++) for (int u = 0; u < units; u++)
                CHECK(a->owners().owns(c, u) == s->owners().owns(c, u), "%s: cat '%s' vs static '%s' core %d unit %d",
                      kv.first.c_str(), cs.c_str(), ss.c_str(), c, u);
        }
        // schedule: from cycle 100 core 0 owns units 2-3, from 200 unit 0 only
        auto t = cat("0:0x3", 4, true, "100@0:0xc|200@0:0x1");
        CHECK(t->invalid().empty() && t->ticks(), "schedule refused: %s", t->invalid().c_str());
        CHECK(!t->tick(99) && t->owns(0, 1) && !t->owns(0, 2), "before cycle 100");
        CHECK(t->tick(100) && t->owns(0, 2) && !t->owns(0, 1), "at cycle 100");
        CHECK(!t->tick(150) && t->tick(250) && t->owns(0, 0) && !t->owns(0, 2), "at cycle 200");
        CHECK(!cat("0:0x3", 4, true, "200@0:0x1|100@0:0x2")->invalid().empty(), "decreasing schedule not refused");
        CHECK(!cat("0:0x3", 4, true, "100@0:0x5")->invalid().empty(), "non-contiguous scheduled mask not refused");
        CHECK(!cat("0:0x3", 4, true, "x@0:0x1")->invalid().empty(), "bad schedule cycle not refused");
        // the way scheme follows a schedule (its cached masks are dropped at the change)
        PartitionContext c = ctx; c.schedule = "100@0:0xf0";
        auto w = PartitionRegistry::build("way", "cat", "0:0x0f", c);
        uint32_t m1 = 0xff; w->restrictVictims(0, 0, m1);
        w->tick(100);
        uint32_t m2 = 0xff; w->restrictVictims(0, 0, m2);
        CHECK(m1 == 0x0f && m2 == 0xf0, "way under a schedule: %x then %x", m1, m2);
        // SharedFills: a fill into a unit another core also owns
        PartitionSet set; set.setName("test"); set.setCores(2);
        set.add(PartitionRegistry::build("way", "cat", "0:0x0f;1:0x3c", ctx));
        set.onFill(0, 0x1000, 0, 0); set.onFill(0, 0x1000, 0, 3);
        set.onEvicted(1, 0); set.onEvicted(0, 0);
        std::ostringstream rep; set.report(rep);
        CHECK(rep.str().find("test,0,way,0,2,0,0,1,0") != std::string::npos && rep.str().find(",1,0,0,0,0,1") == std::string::npos,
              "report: %s", rep.str().c_str());
    }
    // 8. overlapping entry owners (cat): the sender's gate and the allocator look at every core
    {
        using E = PartitionScheme::Entry;
        PartitionContext c = ctx; c.mshr_entries = 4;
        // A = core 0 owns units 0-1, B = core 1 owns 1-2 (unit 1 shared)
        PartitionSet set; set.setName("test");
        set.add(PartitionRegistry::build("mshr", "cat", "0:0x3;1:0x6", c)); set.setCores(2);
        CHECK(set.admitsDemand(0, {{0, 1}, {1, 1}}, {}), "A refused: A 2 + B 1 fit units 0-2");
        // B has 2 waiting: A alone has 2 free units for 1 + 1, but A and B need 4 of units 0-2
        CHECK(!set.admitsDemand(0, {{0, 1}, {1, 2}}, {}), "A admitted although A and B need 4 of 3 units");
        CHECK(set.admitsDemand(0, {{0, 0}, {1, 2}}, {}), "A refused: A 1 + B 2 fit units 0-2");
        // A's new miss takes its private unit 0, not the shared unit 1
        set.allocate(E::MSHR, 0, 0x1000);
        CHECK(set.violations() == 0 && set.admitsDemand(1, {{1, 1}}, {}), "A took the shared unit");

        // least shared is not enough: A owns 0-1, B 0, C 1-2, D 2; every unit has two owners.
        // With B waiting, A must leave unit 0 to B and take unit 1.
        PartitionSet s4; s4.setName("test");
        s4.add(PartitionRegistry::build("mshr", "cat", "0:0x3;1:0x1;2:0x6;3:0x4", c)); s4.setCores(4);
        PartitionSet::Waiting w = [](std::map<int, int> &q, std::map<int, int> &) { q[1] = 1; };
        s4.allocate(E::MSHR, 0, 0x1000, -1, &w);
        CHECK(s4.admitsDemand(1, {{1, 0}}, {}), "A took unit 0, the only one B owns");
        CHECK(!s4.admitsDemand(2, {{2, 0}, {3, 2}}, {}), "C admitted although C and D need 3 of units 1-2 with 1 held");
    }
    CHECK(PartitionRegistry::schemes().count("way") == 1, "way scheme not registered");
    CHECK(PartitionRegistry::ownerships().count("static") == 1, "static ownership not registered");
    if (fails) printf("partition_test: %d FAILURES\n", fails);
    else printf("partition_test: PASS (%zu schemes)\n", PartitionRegistry::schemes().size());
    return fails != 0;
}
