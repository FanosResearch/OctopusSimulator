/*
 * File  :      AddressMap.h
 *
 * Cache geometry as pure functions of the address (docs/LLC_Banking_and_Partitioning_Design.md s5):
 * the set, tag and line of an address in one cache, and the inverse. A cache that is one of N
 * interleaved banks (an LLC bank) indexes its sets with the bank-select digit removed, so each
 * bank uses all of its sets; a cache that is not banked (N = 1) uses today's plain modulo split.
 * The map also holds the home map (which next-level cache serves a line), so one object answers
 * every address question a controller and its protocol ask.
 * Nothing here depends on which core issued an access.
 */

#ifndef _AddressMap_H
#define _AddressMap_H

#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

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
        uint64_t blockBytes() const { return 1ull << m_block_bits; }
        uint64_t set(uint64_t a) const { return packed(a) & (m_sets - 1); }
        uint64_t tag(uint64_t a) const { return packed(a) >> m_set_bits; }
        uint64_t address(uint64_t tag, uint64_t set) const
        {
            uint64_t p = (tag << m_set_bits) | set;
            if (m_self_idx >= 0) p = m_banks.insert(p, m_self_idx);
            return p << m_block_bits;
        }
        bool banked() const { return m_self_idx >= 0; }
        int bankIndex() const { return m_self_idx; }   // this cache's index in its bank group; -1 = not a bank
        const Interleave &banks() const { return m_banks; }   // this cache's bank group (empty = not a bank)

        // Home map (s4.2): which cache of the NEXT level serves a line. An L1's home map lists the
        // LLC banks; an LLC bank's is empty, so every line goes to `fallback` (memory). Kept apart
        // from the bank group above: an LLC bank is one of the banks but its home is memory.
        void setHome(const Interleave &home, int fallback) { m_home = home; m_home_fallback = fallback; }
        const Interleave &homeMap() const { return m_home; }
        int  home(uint64_t a) const { return m_home.ids.empty() ? m_home_fallback : m_home.select(line(a)); }
        bool isHome(int id) const { return m_home.ids.empty() ? id == m_home_fallback : m_home.indexOf(id) >= 0; }

        // Sub-banks of this cache's data array (s5.4): lines spread over n sub-banks, the sub-bank
        // changing every 2^shift lines of this cache's own (bank-stripped) line space.
        void setSubBanks(int n, int shift) { m_sub.ids.clear(); for (int i = 0; i < n; i++) m_sub.ids.push_back(i); m_sub.shift = shift; }
        int subBanks() const { return m_sub.n(); }
        int subBank(uint64_t a) const { return m_sub.index(packed(a)); }

        // Page colours (s5.1, s6.5): the set-index bits that lie above the page offset, so every
        // line of a page has the same colour and moving a page to another frame can change it.
        // `page_bytes` is the page size. colourLo() is the lowest such bit of the set index (in
        // this cache's own line space); -1 when no layout of colours exists: line-interleaved banks
        // whose count is not a power of two scatter a page's lines over non-contiguous set bits.
        int colourLo(uint64_t page_bytes) const
        {
            int P = (int)log2((double)page_bytes) - m_block_bits;   // page offset, in line bits
            if (P < 0) P = 0;
            if (m_self_idx < 0) return P;
            int n = m_banks.n(), s = m_banks.shift;
            if (s >= P) return P;
            if (n & (n - 1)) return -1;
            int nb = (int)log2((double)n);
            return std::max(s, P - nb);
        }
        int colours(uint64_t page_bytes) const
        {
            int lo = colourLo(page_bytes);
            return (lo < 0 || lo >= m_set_bits) ? 1 : 1 << (m_set_bits - lo);
        }
        int colour(uint64_t a, uint64_t page_bytes) const
        {
            int lo = colourLo(page_bytes), k = colours(page_bytes);
            return k == 1 ? 0 : (int)((packed(a) >> lo) & (uint64_t)(k - 1));
        }
        // `a` moved to the frame whose colour is `c`: only bits above the page offset change, and
        // the bank (for a banked cache) is kept.
        uint64_t withColour(uint64_t a, int c, uint64_t page_bytes) const
        {
            int lo = colourLo(page_bytes), k = colours(page_bytes);
            if (k == 1) return a;
            uint64_t mask = (uint64_t)(k - 1) << lo;
            uint64_t p = (packed(a) & ~mask) | (((uint64_t)c << lo) & mask);
            uint64_t l = m_self_idx >= 0 ? m_banks.insert(p, m_banks.index(line(a))) : p;
            return (l << m_block_bits) | (a & ((1ull << m_block_bits) - 1));
        }

    private:
        uint64_t packed(uint64_t a) const { return m_self_idx >= 0 ? m_banks.strip(line(a)) : line(a); }

        uint32_t m_sets = 1;
        int m_block_bits = 0, m_set_bits = 0;
        Interleave m_banks;
        int m_self_idx = -1;
        Interleave m_sub;       // data-array sub-banks; one = the whole array
        Interleave m_home;      // next-level caches serving this cache's lines
        int m_home_fallback = -1;
    };
}

#endif /* _AddressMap_H */
