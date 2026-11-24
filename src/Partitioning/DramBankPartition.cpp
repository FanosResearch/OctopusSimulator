/*
 * File  :      DramBankPartition.cpp
 *
 * DRAM bank partitioning (docs/LLC_Banking_and_Partitioning_Design.md s6.9): the units are the
 * banks of the MCsim device, bank group x bank (dram_bank; DDR4 8Gb x8: 16), or its bank groups
 * (dram_bank_group: 4). Where the bank fields sit in the address is read from the AddressMapping
 * of the MCsim .ini actually loaded (DramMap, filled by MCsimInterface), so a changed mapping is
 * followed. The address fixes the bank, so the PagePlacer enforces it; the memory controller
 * counts accesses outside the core's banks (the "DRAM" rows of Partitions.csv). Needs
 * main_memory_type(s)=MCsim and bank bits above the page offset.
 *
 *   llc_controller.m_data_handler.partition[0].scheme(s),dram_bank
 *   llc_controller.m_data_handler.partition[0].owners(s),0:0-3;1:4-7;2:8-11;3:12-15
 */
#include "Partitioning.h"
#include "DramMap.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <sstream>

namespace octopus
{
    class DramBankPartition : public PartitionScheme
    {
    public:
        DramBankPartition(const PartitionContext &c, bool groups_only) : m_groups_only(groups_only), m_page(c.page_bytes)
        {
            const DramMap &d = DramMap::instance();
            if (!d.valid())
            {
                std::cerr << c.cache << ": partition scheme '" << name() << "' needs main_memory_type(s)=MCsim (DRAM banks)" << std::endl;
                exit(2);
            }
            m_bank_lo = d.bankLo(); m_bank_bits = groups_only ? 0 : d.bankBits();
            m_group_lo = d.groupLo(); m_group_bits = d.groupBits();
            if (units() < 2)
            {
                std::cerr << c.cache << ": partition scheme '" << name() << "': the DRAM device has " << units() << " unit(s)" << std::endl;
                exit(2);
            }
            int page_bits = (int)std::log2((double)m_page);
            if ((m_bank_bits && m_bank_lo < page_bits) || (m_group_bits && m_group_lo < page_bits))
            {
                std::cerr << c.cache << ": partition scheme '" << name() << "': MCsim's AddressMapping puts DRAM bank bits at "
                          << m_bank_lo << " / bank-group bits at " << m_group_lo << ", inside a " << m_page
                          << " B page; a page placer cannot choose them" << std::endl;
                exit(2);
            }
            // unitOf(withUnit(a, u)) == u through MCsim's own decode (MCsim keeps 32 bits of the
            // line address, so test below 2^38)
            std::mt19937_64 rng(1);
            for (int t = 0; t < 4096; t++)
            {
                uint64_t a = (rng() & ((1ull << 38) - 1)) & ~63ull;
                int u = (int)(rng() % (uint64_t)units());
                uint64_t f = withUnit(a, u);
                unsigned bg = 0, b = 0; d.decode(f, bg, b);
                int mc = groups_only ? (int)bg : (int)(bg << m_bank_bits | b);
                if (mc != u || unitOf(f) != u || (f & (m_page - 1)) != (a & (m_page - 1)))
                {
                    std::cerr << c.cache << ": partition scheme '" << name() << "': MCsim decodes address 0x" << std::hex << f
                              << std::dec << " as unit " << mc << ", expected " << u << std::endl;
                    exit(2);
                }
            }
        }
        const char *name() const override { return m_groups_only ? "dram_bank_group" : "dram_bank"; }
        int units() const override { return 1 << (m_bank_bits + m_group_bits); }

        bool countedAtMemory() const override { return true; }
        bool inPartition(int core, uint64_t addr, uint64_t set, int way) const override { return m_owners->owns(core, unitOf(addr)); }

        bool placesAddresses() const override { return true; }
        uint64_t pageBytes() const override { return m_page; }
        int unitOf(uint64_t a) const override
        {
            int bank = m_bank_bits ? (int)((a >> m_bank_lo) & ((1u << m_bank_bits) - 1)) : 0;
            int group = (int)((a >> m_group_lo) & ((1u << m_group_bits) - 1));
            return group << m_bank_bits | bank;
        }
        uint64_t withUnit(uint64_t a, int u) const override
        {
            if (m_bank_bits)
            {
                uint64_t m = (uint64_t)((1u << m_bank_bits) - 1) << m_bank_lo;
                a = (a & ~m) | ((uint64_t)(u & ((1 << m_bank_bits) - 1)) << m_bank_lo);
            }
            uint64_t m = (uint64_t)((1u << m_group_bits) - 1) << m_group_lo;
            return (a & ~m) | ((uint64_t)(u >> m_bank_bits) << m_group_lo);
        }
        std::string signature() const override
        {
            std::ostringstream o;
            o << name() << " bank=" << m_bank_lo << "+" << m_bank_bits << " group=" << m_group_lo << "+" << m_group_bits
              << " page=" << m_page << " owners=" << ownerSpec();
            return o.str();
        }

    private:
        bool m_groups_only;
        uint64_t m_page;
        int m_bank_lo = 0, m_bank_bits = 0, m_group_lo = 0, m_group_bits = 0;
    };

    struct DramBankScheme : DramBankPartition { explicit DramBankScheme(const PartitionContext &c) : DramBankPartition(c, false) {} };
    struct DramBankGroupScheme : DramBankPartition { explicit DramBankGroupScheme(const PartitionContext &c) : DramBankPartition(c, true) {} };
    REGISTER_PARTITION_SCHEME("dram_bank", DramBankScheme);
    REGISTER_PARTITION_SCHEME("dram_bank_group", DramBankGroupScheme);
}
