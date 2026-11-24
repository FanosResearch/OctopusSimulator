/*
 * File  :      Partitioning.cpp
 *
 * The partitioning registry and the built-in StaticOwnership
 * (docs/LLC_Banking_and_Partitioning_Design.md s6).
 */
#include "Partitioning.h"
#include "PagePlacer.h"

#include <cstdlib>
#include <iostream>

namespace octopus
{
    void PartitionSet::onFill(int core, uint64_t addr, uint64_t set, int way, bool memory)
    {
        if (core < 0) return;
        for (size_t k = 0; k < m_view.size(); k++)
        {
            if (m_view[k]->countedAtMemory() != memory || m_view[k]->countsEntries()) continue;
            m_stats[k].fills[core]++;
            if (m_view[k]->inPartition(core, addr, set, way)) continue;
            // A page the placer gave another core (its first toucher) is shared data: outside the
            // requester's units by design, counted apart and never strict.
            int placed_for = m_view[k]->placesAddresses() ? PagePlacer::instance().frameOwner(addr) : -1;
            if (placed_for >= 0 && placed_for != core) { m_stats[k].shared[core]++; continue; }
            m_stats[k].violations[core]++;
            if (m_strict[k])
            {
                if (memory)
                    std::cerr << m_cache << ": partition (" << m_view[k]->name() << ") violated: core " << core
                              << " accessed line 0x" << std::hex << addr << std::dec << " in DRAM unit " << m_view[k]->unitOf(addr)
                              << " outside its units (strict)" << std::endl;
                else
                    std::cerr << m_cache << ": partition[" << k << "] (" << m_view[k]->name() << ") violated: core " << core
                              << " filled line 0x" << std::hex << addr << std::dec << " into set " << set << " way " << way
                              << " outside its units (strict)" << std::endl;
                exit(2);
            }
        }
    }

    void PartitionSet::allocate(PartitionScheme::Entry e, int requester, uint64_t key, int placed_by)
    {
        for (auto &p : m_entries)
        {
            if (p.second->entryKind() != e) continue;
            size_t k = p.first;
            int core = p.second->chargedCore(requester, placed_by);
            bool inside = p.second->allocate(core, key);
            if (core < 0) continue;
            m_stats[k].fills[core]++;
            if (inside) continue;
            m_stats[k].violations[core]++;
            if (m_strict[k])
            {
                std::cerr << m_cache << ": partition[" << m_index[k] << "] (" << m_view[k]->name() << ") violated: core " << core
                          << " opened an entry for line 0x" << std::hex << key << std::dec << " with none of its units free (strict)" << std::endl;
                exit(2);
            }
        }
    }

    void PartitionSet::summary(std::ostream &os) const
    {
        for (auto &p : m_entries) p.second->summary(os, m_cache, m_index[p.first]);
    }

    uint64_t PartitionSet::fills(size_t k, int core) const
    {
        auto it = m_stats[k].fills.find(core);
        return it == m_stats[k].fills.end() ? 0 : it->second;
    }

    uint64_t PartitionSet::violations(size_t k, int core) const
    {
        auto it = m_stats[k].violations.find(core);
        return it == m_stats[k].violations.end() ? 0 : it->second;
    }

    uint64_t PartitionSet::sharedOutside(size_t k, int core) const
    {
        auto it = m_stats[k].shared.find(core);
        return it == m_stats[k].shared.end() ? 0 : it->second;
    }

    uint64_t PartitionSet::violations() const
    {
        uint64_t n = 0;
        for (auto &s : m_stats) for (auto &kv : s.violations) n += kv.second;
        return n;
    }

    void PartitionSet::report(std::ostream &os) const
    {
        for (size_t k = 0; k < m_view.size(); k++)
            for (auto &kv : m_stats[k].fills)
                os << m_cache << "," << m_index[k] << "," << m_view[k]->name() << "," << kv.first << ","
                   << kv.second << "," << violations(k, kv.first) << "," << sharedOutside(k, kv.first) << "\n";
    }

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
        std::string bad = s->invalid();
        if (!bad.empty())
        {
            std::cerr << ctx.cache << ": partition scheme '" << scheme << "': " << bad << std::endl;
            exit(2);
        }
        std::unique_ptr<Ownership> o = of->second();
        o->configure(owners, s->units());
        std::string why = o->invalid();
        if (!why.empty())
        {
            std::cerr << ctx.cache << ": partition scheme '" << scheme << "' owners '" << owners << "': " << why << std::endl;
            exit(2);
        }
        s->setOwnership(std::move(o), owners);
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
                bool some = false; for (bool b : mine) some = some || b;
                if (!some && m_invalid.empty())
                    m_invalid = "cores " + std::to_string(c0) + "-" + std::to_string(c1) + " own no unit (units are 0-" +
                                std::to_string(units - 1) + "); such a core could never fill";
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
        std::string invalid() const override { return m_invalid; }

    private:
        std::string m_invalid;
        int m_units = 0;
        std::map<int, std::vector<bool>> m_core;
        std::vector<bool> m_shared;
    };

    REGISTER_OWNERSHIP("static", StaticOwnership);
}
