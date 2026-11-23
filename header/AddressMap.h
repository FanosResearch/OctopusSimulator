/*
 * File  :      AddressMap.h
 *
 * Cache geometry as pure functions of the address (docs/LLC_Banking_and_Partitioning_Design.md s5):
 * the set, tag and line of an address in one cache, and the inverse. A cache that is one of N
 * interleaved banks (an LLC bank) indexes its sets with the bank-select digit removed, so each
 * bank uses all of its sets; a cache that is not banked (N = 1) uses today's plain modulo split.
 * Nothing here depends on which core issued an access.
 */

#ifndef _AddressMap_H
#define _AddressMap_H

#include <cstdint>
#include <cmath>
#include <vector>

namespace octopus
{
    // Spreads lines over N targets: line -> ids[(line >> shift) % N]. strip() removes the
    // target digit from a line number and insert() puts it back; any N, not only powers of two.
    struct Interleave
    {
        std::vector<int> ids;   // target ids; empty or one entry = not interleaved
        int shift = 0;          // granularity: the target changes every 2^shift lines

        int n() const { return ids.size() > 1 ? (int)ids.size() : 1; }
        int index(uint64_t line) const { return n() == 1 ? 0 : (int)((line >> shift) % (uint64_t)n()); }
        int select(uint64_t line) const { return ids.empty() ? -1 : ids[index(line)]; }
        int indexOf(int id) const
        {
            for (size_t i = 0; i < ids.size(); i++) if (ids[i] == id) return (int)i;
            return -1;
        }
        uint64_t strip(uint64_t line) const
        {
            if (n() == 1) return line;
            uint64_t low = line & ((1ull << shift) - 1);
            return (((line >> shift) / (uint64_t)n()) << shift) | low;
        }
        uint64_t insert(uint64_t packed, int idx) const
        {
            if (n() == 1) return packed;
            uint64_t low = packed & ((1ull << shift) - 1);
            return (((packed >> shift) * (uint64_t)n() + (uint64_t)idx) << shift) | low;
        }
    };

    class AddressMap
    {
    public:
        AddressMap() = default;
        AddressMap(uint32_t block_size, uint32_t sets_count) { setGeometry(block_size, sets_count); }

        void setGeometry(uint32_t block_size, uint32_t sets_count)
        {
            m_sets = sets_count;
            m_block_bits = (int)log2(block_size);
            m_set_bits = (int)log2(sets_count);
        }
        // This cache is bank `self_idx` of `banks` (an LLC bank): its sets are indexed with the
        // bank digit removed. self_idx < 0 (or one bank) = not banked.
        void setBanks(const Interleave &banks, int self_idx)
        {
            m_banks = banks;
            m_self_idx = (banks.n() > 1 && self_idx >= 0) ? self_idx : -1;
        }

        uint64_t line(uint64_t a) const { return a >> m_block_bits; }
        uint64_t set(uint64_t a) const { return packed(a) & (m_sets - 1); }
        uint64_t tag(uint64_t a) const { return packed(a) >> m_set_bits; }
        uint64_t address(uint64_t tag, uint64_t set) const
        {
            uint64_t p = (tag << m_set_bits) | set;
            if (m_self_idx >= 0) p = m_banks.insert(p, m_self_idx);
            return p << m_block_bits;
        }
        bool banked() const { return m_self_idx >= 0; }

        // Sub-banks of this cache's data array (s5.4): lines spread over n sub-banks, the sub-bank
        // changing every 2^shift lines of this cache's own (bank-stripped) line space.
        void setSubBanks(int n, int shift) { m_sub.ids.clear(); for (int i = 0; i < n; i++) m_sub.ids.push_back(i); m_sub.shift = shift; }
        int subBanks() const { return m_sub.n(); }
        int subBank(uint64_t a) const { return m_sub.index(packed(a)); }

    private:
        uint64_t packed(uint64_t a) const { return m_self_idx >= 0 ? m_banks.strip(line(a)) : line(a); }

        uint32_t m_sets = 1;
        int m_block_bits = 0, m_set_bits = 0;
        Interleave m_banks;
        int m_self_idx = -1;
        Interleave m_sub;       // data-array sub-banks; one = the whole array
    };
}

#endif /* _AddressMap_H */
