/*
 * File  :      PagePlacer.cpp  (docs/LLC_Banking_and_Partitioning_Design.md s6.6)
 */
#include "PagePlacer.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace octopus
{
    void PagePlacer::configure(bool enabled, const std::vector<const PartitionScheme *> &schemes, int num_cores)
    {
        m_schemes.clear(); m_table.clear(); m_frame_owner.clear(); m_next.clear(); m_cursor.clear(); m_pages_by_core.clear();
        m_core_units.clear(); m_shared_bits = false;
        m_on = false;
        if (!enabled) return;

        std::set<std::string> seen;
        std::map<std::string, std::string> by_name;   // a scheme name must mean one function
        uint64_t page = 0;
        for (const PartitionScheme *s : schemes)
        {
            if (!s->placesAddresses()) continue;
            std::string sig = s->signature();
            if (!seen.insert(sig).second) continue;
            auto it = by_name.find(s->name());
            if (it != by_name.end())
            {
                std::cerr << "PagePlacer: two different '" << s->name() << "' partitions (" << it->second << " / " << sig
                          << "); configure the same one on every cache (e.g. llc_controller[*])" << std::endl;
                exit(2);
            }
            by_name[s->name()] = sig;
            if (page && s->pageBytes() != page)
            {
                std::cerr << "PagePlacer: placing partitions use different page sizes (" << page << ", " << s->pageBytes() << ")" << std::endl;
                exit(2);
            }
            page = s->pageBytes();
            m_schemes.push_back(s);
        }
        if (m_schemes.empty())
        {
            std::cout << "PagePlacer: page_placer(i)=1 but no placing partition (colour, bank) is configured; addresses pass through" << std::endl;
            return;
        }
        m_page_bits = (int)log2((double)page);

        // Do the schemes share address bits (placing for one moves another)? Then a frame must
        // satisfy all of them at once (joint placement, P5); disjoint schemes place as before.
        for (uint64_t k = 0; k < 64 && !m_shared_bits; k++)
        {
            std::vector<int> u;
            for (size_t i = 0; i < m_schemes.size(); i++) u.push_back((int)((k * 7 + i * 3) % m_schemes[i]->units()));
            if (!matches(compose((k * 977) << m_page_bits, u), u)) m_shared_bits = true;
        }
        for (int core = 0; core < num_cores; core++)
        {
            std::string why = conflict(core);
            if (!why.empty()) { std::cerr << "PagePlacer: " << why << std::endl; exit(2); }
        }
        m_on = true;
        std::cout << "PagePlacer: on, " << page << " B pages, placing for";
        for (auto *s : m_schemes) std::cout << " " << s->name() << " (" << s->units() << " units)";
        if (m_shared_bits) std::cout << "; the schemes share address bits: each frame satisfies all of them";
        std::cout << std::endl;
    }

    uint64_t PagePlacer::compose(uint64_t f, const std::vector<int> &units) const
    {
        for (size_t i = 0; i < m_schemes.size(); i++) f = m_schemes[i]->withUnit(f, units[i]);
        return f;
    }

    bool PagePlacer::matches(uint64_t f, const std::vector<int> &units) const
    {
        for (size_t i = 0; i < m_schemes.size(); i++)
            if (m_schemes[i]->unitOf(f) != units[i]) return false;
        return true;
    }

    // A combination is satisfiable when chaining withUnit gives a frame every scheme agrees with:
    // with bit-field units that is exactly when the combination agrees on the shared bits.
    bool PagePlacer::satisfiable(const std::vector<int> &units) const
    {
        for (uint64_t k = 0; k < 4; k++)
            if (matches(compose((k * 977) << m_page_bits, units), units)) return true;
        return false;
    }

    PagePlacer::CoreUnits &PagePlacer::coreUnits(int core)
    {
        auto it = m_core_units.find(core);
        if (it != m_core_units.end()) return it->second;
        CoreUnits &cu = m_core_units[core];
        std::vector<std::vector<int>> mine(m_schemes.size());
        uint64_t combos = 1;
        for (size_t i = 0; i < m_schemes.size(); i++)
        {
            for (int u = 0; u < m_schemes[i]->units(); u++) if (m_schemes[i]->owners().owns(core, u)) mine[i].push_back(u);
            combos *= mine[i].size();
        }
        if (combos > (1u << 20))
        {
            std::cerr << "PagePlacer: core " << core << " owns " << combos << " unit combinations; at most 2^20 are supported" << std::endl;
            exit(2);
        }
        std::vector<int> t(m_schemes.size());
        for (uint64_t n = 0; n < combos; n++)
        {
            uint64_t r = n;   // n-th combination, the last scheme fastest
            for (size_t i = m_schemes.size(); i-- > 0;) { t[i] = mine[i][r % mine[i].size()]; r /= mine[i].size(); }
            if (satisfiable(t)) cu.tuples.push_back(t);
        }
        cu.joint = cu.tuples.size() != combos;
        return cu;
    }

    std::string PagePlacer::conflict(int core)
    {
        if (!m_on && m_schemes.empty()) return "";
        if (!coreUnits(core).tuples.empty()) return "";
        std::string why = "core " + std::to_string(core) + ": no frame satisfies all placing partitions (they share address bits"
                          " and its units disagree on them):";
        for (const PartitionScheme *s : m_schemes) why += " '" + s->signature() + "'";
        return why;
    }

    uint64_t PagePlacer::frameFor(const std::vector<int> &units)
    {
        uint64_t &k = m_next[units];
        for (uint64_t tries = 0;; k++, tries++)
        {
            uint64_t f = compose(k << m_page_bits, units);
            uint64_t fp = f >> m_page_bits;
            if (matches(f, units) && !m_frame_owner.count(fp)) { k++; return fp; }
            if (tries == (1ull << 32))
            {
                std::cerr << "PagePlacer: no free frame found for a page" << std::endl;
                exit(2);
            }
        }
    }

    uint64_t PagePlacer::placeSlow(int core, uint64_t addr)
    {
        uint64_t pg = addr >> m_page_bits, off = addr & ((1ull << m_page_bits) - 1);
        auto it = m_table.find(pg);
        if (it != m_table.end()) return (it->second << m_page_bits) | off;

        CoreUnits &cu = coreUnits(core);
        if (cu.tuples.empty()) { std::cerr << "PagePlacer: " << conflict(core) << std::endl; exit(2); }
        std::vector<int> units;
        if (cu.joint)
            units = cu.tuples[cu.cursor++ % cu.tuples.size()];   // round-robin over what a frame can hold
        else
            for (size_t i = 0; i < m_schemes.size(); i++)
            {
                const PartitionScheme *s = m_schemes[i];
                std::vector<int> mine;
                for (int u = 0; u < s->units(); u++) if (s->owners().owns(core, u)) mine.push_back(u);
                size_t &c = m_cursor[{i, core}];
                units.push_back(mine[c % mine.size()]);   // Ownership::invalid() guarantees one unit
                c++;
            }
        uint64_t fp = frameFor(units);
        m_table[pg] = fp;
        m_frame_owner[fp] = core;
        m_pages_by_core[core]++;
        return (fp << m_page_bits) | off;
    }

    void PagePlacer::report(std::ostream &os) const
    {
        os << "PagePlacer: " << m_table.size() << " pages placed (";
        bool first = true;
        for (auto &kv : m_pages_by_core) { os << (first ? "" : ", ") << "core " << kv.first << ": " << kv.second; first = false; }
        os << ")" << std::endl;
    }
}
