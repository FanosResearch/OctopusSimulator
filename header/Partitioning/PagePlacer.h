/*
 * File  :      PagePlacer.h
 *
 * Source-side enforcement of placing partition schemes (docs/LLC_Banking_and_Partitioning_Design.md
 * s6.6), as an OS page allocator does. It sits in the trace path (CPU.cpp), in front of the L1s:
 * the first access to a page picks, for every placing scheme, the next unit the core owns
 * (round-robin over its units), and gives the page a fresh frame with those units. One page table
 * serves all cores, so a shared page has one frame and coherence is unaffected; its units come
 * from the first core that touches it. Off by default (page_placer(i),0): addresses pass through.
 */

#ifndef _PagePlacer_H
#define _PagePlacer_H

#include "Partitioning.h"

#include <cstdint>
#include <map>
#include <ostream>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace octopus
{
    class PagePlacer
    {
    public:
        static PagePlacer &instance() { static PagePlacer p; return p; }

        // Called once the system is built. `schemes`: every cache's placing schemes; the same
        // scheme on several caches (one per LLC bank) is used once. Exits on an inconsistent set.
        void configure(bool enabled, const std::vector<const PartitionScheme *> &schemes);
        bool enabled() const { return m_on; }

        uint64_t place(int core, uint64_t addr) { return m_on ? placeSlow(core, addr) : addr; }

        // the core a frame was placed for (its first toucher); -1 = not placed by this placer
        int frameOwner(uint64_t addr) const
        {
            if (!m_on) return -1;
            auto it = m_frame_owner.find(addr >> m_page_bits);
            return it == m_frame_owner.end() ? -1 : it->second;
        }
        void report(std::ostream &os) const;   // pages placed per core
        uint64_t pages() const { return m_table.size(); }

    private:
        uint64_t placeSlow(int core, uint64_t addr);
        uint64_t frameFor(const std::vector<int> &units);

        bool m_on = false;
        int m_page_bits = 12;
        std::vector<const PartitionScheme *> m_schemes;
        std::unordered_map<uint64_t, uint64_t> m_table;          // original page -> frame page
        std::unordered_map<uint64_t, int> m_frame_owner;         // frame page -> core placed for
        std::map<std::vector<int>, uint64_t> m_next;             // per unit tuple: next candidate
        std::map<std::pair<size_t, int>, size_t> m_cursor;       // (scheme, core): round-robin position
        std::map<int, uint64_t> m_pages_by_core;
    };
}

#endif /* _PagePlacer_H */
