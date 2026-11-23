/*
 * File  :      ColourPartition.cpp
 *
 * Set partitioning by page colour (docs/LLC_Banking_and_Partitioning_Design.md s6.5): the units
 * are the colours of the cache, the set-index bits above the page offset (AddressMap::colour).
 * A cache cannot choose a line's set, so the PagePlacer enforces it by giving each page a frame
 * of a colour the core owns; the cache counts fills of colours the requester does not own.
 */
#include "Partitioning.h"

#include <sstream>

namespace octopus
{
    class ColourPartition : public PartitionScheme
    {
    public:
        explicit ColourPartition(const PartitionContext &c) : m_map(c.map), m_page(c.page_bytes), m_sets(c.sets) {}
        const char *name() const override { return "colour"; }
        int units() const override { return m_map->colours(m_page); }

        bool inPartition(int core, uint64_t addr, uint64_t set, int way) const override
        {
            return m_owners->owns(core, unitOf(addr));
        }

        bool     placesAddresses() const override { return true; }
        int      unitOf(uint64_t addr) const override { return m_map->colour(addr, m_page); }
        uint64_t withUnit(uint64_t addr, int unit) const override { return m_map->withColour(addr, unit, m_page); }
        uint64_t pageBytes() const override { return m_page; }

        std::string signature() const override
        {
            std::ostringstream s;
            s << "colour lo=" << m_map->colourLo(m_page) << " colours=" << units() << " page=" << m_page << " owners=" << m_owner_spec;
            return s.str();
        }
        std::string invalid() const override
        {
            if (m_page == 0 || (m_page & (m_page - 1)))
                return "page_size(i) must be a power of two";
            if (m_map->colourLo(m_page) < 0)
                return "line-interleaved LLC banks must be a power of two in number for page colours (or interleave by page)";
            if (units() < 2)
            {
                std::ostringstream s;
                s << "no set-index bits above the page offset (" << m_sets << " sets, page " << m_page
                  << " B): the cache has one colour; use more sets or smaller pages";
                return s.str();
            }
            return "";
        }

    private:
        const AddressMap *m_map;
        uint64_t m_page;
        uint64_t m_sets;
    };

    REGISTER_PARTITION_SCHEME("colour", ColourPartition);
}
