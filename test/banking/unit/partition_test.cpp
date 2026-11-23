// Partitioning contract tests (docs/LLC_Banking_and_Partitioning_Design.md s6, s8.2).
//   g++ -std=c++17 -O1 -I header -I header/Partitioning test/banking/unit/partition_test.cpp src/Partitioning/*.cpp -o /tmp/pt && /tmp/pt
// Runs on EVERY registered scheme, so a new scheme is covered by adding its source file.
#include "Partitioning.h"
#include <cstdio>
#include <map>
#include <random>
#include <string>
using namespace octopus;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 20) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

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
        int c0 = rng() % cores, c1 = c0 + rng() % 2, u0 = rng() % (units + 1), u1 = u0 + rng() % 3;
        s += (i ? ";" : "") + std::to_string(c0) + (c1 > c0 ? "-" + std::to_string(c1) : "") + ":" +
             std::to_string(u0) + (u1 > u0 ? "-" + std::to_string(u1) : "");
    }
    return s;
}

int main()
{
    std::mt19937 rng(7);
    AddressMap map(64, 256);
    PartitionContext ctx; ctx.cache = "test"; ctx.ways = 8; ctx.sets = 256; ctx.map = &map;
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
    CHECK(PartitionRegistry::schemes().count("way") == 1, "way scheme not registered");
    CHECK(PartitionRegistry::ownerships().count("static") == 1, "static ownership not registered");
    if (fails) printf("partition_test: %d FAILURES\n", fails);
    else printf("partition_test: PASS (%zu schemes)\n", PartitionRegistry::schemes().size());
    return fails != 0;
}
