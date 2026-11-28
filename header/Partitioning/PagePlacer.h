/*
 * File  :      PagePlacer.h
 *
 * Source-side enforcement of placing partition schemes (docs/LLC_Banking_and_Partitioning_Design.md
 * s6.6), as an OS page allocator does. It sits in the trace path (CPU.cpp), in front of the L1s:
 * the first access to a page picks the core's next combination of owned units, one per placing
 * scheme (round-robin over all combinations), and gives the page a fresh frame with those units. Schemes may share address bits (P5): the
 * frame must then satisfy all of them at once, and only the core's unit combinations that some
 * frame satisfies are used; a core with none is refused. One page table
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
#include <string>
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
        // scheme on several caches (one per LLC bank) is used once. Exits on an inconsistent set,
        // and on a core in 0..num_cores-1 with no common frame (conflict()).
        void configure(bool enabled, const std::vector<const PartitionScheme *> &schemes, int num_cores = 0);

        // Why no frame can hold a page of `core` (its owned units disagree on a shared address
        // bit); empty when the core can be placed. Exposed for tests.
        std::string conflict(int core);
        bool enabled() const { return m_on; }

        uint64_t place(int core, uint64_t addr) { return m_on ? placeSlow(core, addr) : addr; }

        // The inverse of place(): the original address of a placed one, page offset kept. A front
        // end whose data lives at the original addresses (the gem5 bridge) maps every address
        // Octopus hands back to it (commit, completion, invalidation). Off or never placed =
        // unchanged.
        uint64_t unplace(uint64_t addr) const
        {
            if (!m_on) return addr;
            auto it = m_reverse.find(addr >> m_page_bits);
            return it == m_reverse.end() ? addr : (it->second << m_page_bits) | (addr & ((1ull << m_page_bits) - 1));
        }

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
        uint64_t compose(uint64_t frame, const std::vector<int> &units) const;   // chained withUnit
        bool matches(uint64_t frame, const std::vector<int> &units) const;      // every unitOf agrees
        bool satisfiable(const std::vector<int> &units) const;

        // The core's owned unit combinations that a frame can satisfy, in odometer order (the last
        // scheme steps fastest). Pages go round-robin over them, so a core uses every pair of units
        // it owns; `distinct` counts the combinations used so far (report()).
        struct CoreUnits { std::vector<std::vector<int>> tuples; size_t cursor = 0; std::vector<bool> used; size_t distinct = 0; };
        CoreUnits &coreUnits(int core);

        bool m_on = false;
        int m_page_bits = 12;
        std::vector<const PartitionScheme *> m_schemes;
        std::unordered_map<uint64_t, uint64_t> m_table;          // original page -> frame page
        std::unordered_map<uint64_t, uint64_t> m_reverse;        // frame page -> original page
        std::unordered_map<uint64_t, int> m_frame_owner;         // frame page -> core placed for
        std::map<std::vector<int>, uint64_t> m_next;             // per unit tuple: next candidate
        std::map<int, uint64_t> m_pages_by_core;
        std::map<int, CoreUnits> m_core_units;
        bool m_shared_bits = false;
    };
}

#endif /* _PagePlacer_H */
