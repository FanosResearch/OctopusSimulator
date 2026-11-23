/*
 * File  :      Partitioning.cpp
 *
 * The partitioning registry and the built-in StaticOwnership
 * (docs/LLC_Banking_and_Partitioning_Design.md s6).
 */
#include "Partitioning.h"

#include <cstdlib>
#include <iostream>

namespace octopus
{
    std::unique_ptr<PartitionScheme> PartitionRegistry::build(const std::string &scheme, const std::string &ownership,
                                                              const std::string &owners, const PartitionContext &ctx)
    {
        auto list = [](const auto &m) { std::string s; for (auto &kv : m) s += (s.empty() ? "" : ", ") + kv.first; return s; };
        auto sf = schemes().find(scheme);
        if (sf == schemes().end())
        {
            std::cerr << ctx.cache << ": unknown partition scheme '" << scheme << "' (known: " << list(schemes()) << ")" << std::endl;
            exit(2);
        }
        auto of = ownerships().find(ownership);
        if (of == ownerships().end())
        {
            std::cerr << ctx.cache << ": unknown partition ownership '" << ownership << "' (known: " << list(ownerships()) << ")" << std::endl;
            exit(2);
        }
        std::unique_ptr<PartitionScheme> s = sf->second(ctx);
        std::unique_ptr<Ownership> o = of->second();
        o->configure(owners, s->units());
        s->setOwnership(std::move(o));
        return s;
    }

    // Static ownership: "<cores>:<units>;..." with a-b ranges, e.g. "0:0;1-3:1" (core 0 owns
    // unit 0, cores 1..3 unit 1). A later entry for a core replaces an earlier one. Cores not
    // listed share the units nobody claimed; when every unit is claimed they share all of them.
    // Units past the scheme's count are ignored. (This is the way_partition(s) syntax.)
    class StaticOwnership : public Ownership
    {
    public:
        void configure(const std::string &spec, int units) override
        {
            m_units = units;
            m_shared.assign(units, true);
            auto range = [](const std::string &s, int &lo, int &hi) {
                size_t d = s.find('-'); lo = atoi(s.c_str()); hi = (d == std::string::npos) ? lo : atoi(s.c_str() + d + 1); };
            size_t p = 0;
            while (p < spec.size())
            {
                size_t e = spec.find(';', p); if (e == std::string::npos) e = spec.size();
                std::string entry = spec.substr(p, e - p); p = e + 1;
                size_t c = entry.find(':'); if (c == std::string::npos || entry.empty()) continue;
                int c0, c1, u0, u1; range(entry.substr(0, c), c0, c1); range(entry.substr(c + 1), u0, u1);
                std::vector<bool> mine(units, false);
                for (int u = u0; u <= u1 && u < units; u++) if (u >= 0) { mine[u] = true; m_shared[u] = false; }
                for (int core = c0; core <= c1; core++) m_core[core] = mine;
            }
            bool any = false; for (bool b : m_shared) any = any || b;
            if (!m_core.empty() && !any) m_shared.assign(units, true);
        }
        bool owns(int core, int unit) const override
        {
            if (core < 0 || m_core.empty() || unit < 0 || unit >= m_units) return core < 0 || m_core.empty();
            auto it = m_core.find(core);
            return it != m_core.end() ? it->second[unit] : m_shared[unit];
        }
        bool restricts() const override { return !m_core.empty(); }

    private:
        int m_units = 0;
        std::map<int, std::vector<bool>> m_core;
        std::vector<bool> m_shared;
    };

    REGISTER_OWNERSHIP("static", StaticOwnership);
}
