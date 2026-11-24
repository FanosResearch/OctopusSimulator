/*
 * File  :      DramMap.h
 *
 * DRAM side of the partitioning framework (docs/LLC_Banking_and_Partitioning_Design.md s6.9).
 * MCsimInterface records where MCsim's address mapping puts the bank and bank-group fields, and
 * MCsim's own decode for checking; DramBankPartition builds its units from them. DRAM schemes are
 * configured on the LLC (partition[k]) but counted here: MCsimInterface reports every DRAM
 * access, and the "DRAM" rows of Partitions.csv hold the per-core accesses and violations.
 */

#ifndef _DramMap_H
#define _DramMap_H

#include "Partitioning.h"

#include <cstdint>
#include <functional>

namespace octopus
{
    class DramMap
    {
    public:
        static DramMap &instance() { static DramMap d; return d; }

        // Bit positions in the byte address. `decode` is MCsim's own (bank group, bank) of an address.
        using Decode = std::function<void(uint64_t addr, unsigned &bank_group, unsigned &bank)>;
        void set(int bank_lo, int bank_bits, int group_lo, int group_bits, Decode decode);
        bool valid() const { return m_valid; }
        int bankLo() const { return m_bank_lo; }
        int bankBits() const { return m_bank_bits; }
        int groupLo() const { return m_group_lo; }
        int groupBits() const { return m_group_bits; }
        void decode(uint64_t addr, unsigned &bank_group, unsigned &bank) const { m_decode(addr, bank_group, bank); }

        // the DRAM schemes being counted; rebuilt by CacheDataHandler::finalizePartitions
        PartitionSet &partitions() { return m_set; }
        void clearPartitions() { m_set = PartitionSet(); m_set.setName("DRAM"); }
        // a DRAM access for `core` (reads: the requester; write-backs: the page's owner); core < 0 is not counted
        void onAccess(int core, uint64_t addr) { if (!m_set.empty()) m_set.onFill(core, addr, 0, 0, /*memory=*/true); }

    private:
        DramMap() { m_set.setName("DRAM"); }
        bool m_valid = false;
        int m_bank_lo = 0, m_bank_bits = 0, m_group_lo = 0, m_group_bits = 0;
        Decode m_decode;
        PartitionSet m_set;
    };
}

#endif /* _DramMap_H */
