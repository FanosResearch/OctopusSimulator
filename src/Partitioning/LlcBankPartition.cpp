/*
 * File  :      LlcBankPartition.cpp
 *
 * LLC bank partitioning (docs/LLC_Banking_and_Partitioning_Design.md s6.5): the units are the
 * banks of a multi-bank LLC (the bank group in the cache's AddressMap). The address fixes the
 * bank, so the PagePlacer enforces it: each page gets a frame in a bank the core owns. This
 * needs page (or coarser) bank interleaving, home_interleave_shift(i) >= log2(page / line):
 * with finer interleaving one page spans several banks. Each bank counts fills by cores that
 * do not own it.
 *
 *   llc_controller[*].m_data_handler.partition[0].scheme(s),bank
 *   llc_controller[*].m_data_handler.partition[0].owners(s),0:0;1-3:1
 */
#include "Partitioning.h"

#include <cmath>
#include <sstream>

namespace octopus
{
    class LlcBankPartition : public PartitionScheme
    {
    public:
        explicit LlcBankPartition(const PartitionContext &c) : m_map(c.map), m_page(c.page_bytes) {}
        const char *name() const override { return "bank"; }
        int units() const override { return m_map->banks().n(); }

        bool inPartition(int core, uint64_t addr, uint64_t set, int way) const override
        {
            return m_owners->owns(core, unitOf(addr));
        }

        bool placesAddresses() const override { return true; }
        int unitOf(uint64_t addr) const override { return m_map->banks().index(m_map->line(addr)); }
        uint64_t withUnit(uint64_t addr, int unit) const override
        {
            const Interleave &b = m_map->banks();
            uint64_t l = m_map->line(addr), bb = m_map->blockBytes();
            return b.insert(b.strip(l), unit) * bb + (addr & (bb - 1));
        }
        uint64_t pageBytes() const override { return m_page; }

        std::string signature() const override
        {
            std::ostringstream s;
            s << "bank n=" << units() << " shift=" << m_map->banks().shift << " page=" << m_page << " owners=" << m_owner_spec;
            return s.str();
        }
        std::string invalid() const override
        {
            if (units() < 2) return "needs a multi-bank LLC (llc_banks(i) >= 2)";
            if (m_page == 0 || (m_page & (m_page - 1))) return "page_size(i) must be a power of two";
            int P = (int)log2((double)m_page) - (int)log2((double)m_map->blockBytes());
            if (m_map->banks().shift < P)
            {
                std::ostringstream s;
                s << "banks interleave every " << (1 << m_map->banks().shift) << " line(s), so a " << m_page
                  << " B page spans several banks; set home_interleave_shift(i) >= " << P << " (page interleave)";
                return s.str();
            }
            return "";
        }

    private:
        const AddressMap *m_map;
        uint64_t m_page;
    };

    REGISTER_PARTITION_SCHEME("bank", LlcBankPartition);
}
