/*
 * File  :      WayPartition.cpp
 *
 * Way partitioning (docs/LLC_Banking_and_Partitioning_Design.md s6.5): the units are the ways of
 * a set; a fill by a core is installed in, and evicts from, only the ways the core owns.
 * Enforced by the cache on victim choice.
 */
#include "Partitioning.h"

namespace octopus
{
    class WayPartition : public PartitionScheme
    {
    public:
        explicit WayPartition(const PartitionContext &c) : m_ways(c.ways) {}
        const char *name() const override { return "way"; }
        int units() const override { return (int)m_ways; }

        void restrictVictims(int core, uint64_t set, uint32_t &way_mask) const override
        {
            auto it = m_mask.find(core);
            if (it == m_mask.end()) it = m_mask.emplace(core, maskOf(core)).first;
            way_mask &= it->second;
        }
        bool inPartition(int core, uint64_t addr, uint64_t set, int way) const override
        {
            uint32_t m = 0xffffffffu;
            restrictVictims(core, set, m);
            return way >= 0 && way < 32 && (m & (1u << way));
        }
        int fillUnit(uint64_t addr, uint64_t set, int way) const override { return way; }

    protected:
        void ownershipChanged() override { m_mask.clear(); }

    private:
        uint32_t maskOf(int core) const
        {
            uint32_t m = 0;
            for (uint32_t w = 0; w < m_ways && w < 32; w++) if (m_owners->owns(core, (int)w)) m |= 1u << w;
            return m;
        }
        uint32_t m_ways;
        mutable std::map<int, uint32_t> m_mask;   // per-core mask, computed on first use
    };

    REGISTER_PARTITION_SCHEME("way", WayPartition);
}
