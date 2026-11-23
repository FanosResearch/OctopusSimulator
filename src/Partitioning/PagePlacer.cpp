/*
 * File  :      PagePlacer.cpp  (docs/LLC_Banking_and_Partitioning_Design.md s6.6)
 */
#include "PagePlacer.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace octopus
{
    void PagePlacer::configure(bool enabled, const std::vector<const PartitionScheme *> &schemes)
    {
        m_schemes.clear(); m_table.clear(); m_frame_owner.clear(); m_next.clear(); m_cursor.clear(); m_pages_by_core.clear();
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

        // The schemes must use disjoint address bits: placing for one must not move another.
        for (uint64_t k = 0; k < 64; k++)
        {
            std::vector<int> u;
            for (size_t i = 0; i < m_schemes.size(); i++) u.push_back((int)((k * 7 + i * 3) % m_schemes[i]->units()));
            uint64_t f = (k * 977) << m_page_bits;
            for (size_t i = 0; i < m_schemes.size(); i++) f = m_schemes[i]->withUnit(f, u[i]);
            for (size_t i = 0; i < m_schemes.size(); i++)
                if (m_schemes[i]->unitOf(f) != u[i])
                {
                    std::cerr << "PagePlacer: placing partitions";
                    for (auto *s : m_schemes) std::cerr << " '" << s->signature() << "'";
                    std::cerr << " use overlapping address bits; one undoes another" << std::endl;
                    exit(2);
                }
        }
        m_on = true;
        std::cout << "PagePlacer: on, " << page << " B pages, placing for";
        for (auto *s : m_schemes) std::cout << " " << s->name() << " (" << s->units() << " units)";
        std::cout << std::endl;
    }

    uint64_t PagePlacer::frameFor(const std::vector<int> &units)
    {
        uint64_t &k = m_next[units];
        for (;; k++)
        {
            uint64_t f = k << m_page_bits;
            for (size_t i = 0; i < m_schemes.size(); i++) f = m_schemes[i]->withUnit(f, units[i]);
            uint64_t fp = f >> m_page_bits;
            if (!m_frame_owner.count(fp)) { k++; return fp; }
        }
    }

    uint64_t PagePlacer::placeSlow(int core, uint64_t addr)
    {
        uint64_t pg = addr >> m_page_bits, off = addr & ((1ull << m_page_bits) - 1);
        auto it = m_table.find(pg);
        if (it != m_table.end()) return (it->second << m_page_bits) | off;

        std::vector<int> units;
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
