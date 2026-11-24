/*
 * File  :      CatOwnership.cpp
 *
 * Overlapping ownership in the style of Intel Cache Allocation Technology
 * (docs/LLC_Banking_and_Partitioning_Design.md s6.11), registered as `cat`.
 *
 *   owners(s)      "<cores>:<mask>;..." with a hex capacity bit mask per core group, bit u = unit u,
 *                  e.g. "0:0x00f;1:0x0f0;2-3:0xff0". Masks may overlap. Cores not listed own every
 *                  unit (CAT's default class of service owns the whole cache), unlike `static`.
 *   contiguous(i)  1 (default): the set bits of every mask must be contiguous, as CAT requires;
 *                  0 lifts the rule (Arm MPAM and AMD allow any mask).
 *   schedule(s)    "<cycle>@<owners>|<cycle>@<owners>...": from each cycle (the cache's) the masks
 *                  are those owners, as system software reprogramming the masks while tasks run.
 *                  Lines in units a core loses stay and still hit; new fills and new entries
 *                  follow the new masks. Cache-enforced schemes only (way, mshr, pwb): a placing
 *                  scheme places each page once, at its first touch.
 *
 * A mask with no bit set, a bit past the scheme's units, or (with contiguous) a gap is refused at
 * startup. A run with no schedule behaves as `static` with the same units per listed core.
 */
#include "Partitioning.h"

#include <cstdlib>
#include <map>
#include <sstream>
#include <vector>

namespace octopus
{
    class CatOwnership : public Ownership
    {
    public:
        void setContext(const PartitionContext &c) override { m_contiguous = c.contiguous; m_schedule_spec = c.schedule; }

        void configure(const std::string &spec, int units) override
        {
            m_units = units;
            m_core = parse(spec, "owners");
            // the schedule: cycles strictly increasing; each owner set checked like owners(s)
            uint64_t last = 0;
            size_t p = 0;
            while (p < m_schedule_spec.size() && m_invalid.empty())
            {
                size_t e = m_schedule_spec.find('|', p); if (e == std::string::npos) e = m_schedule_spec.size();
                std::string phase = m_schedule_spec.substr(p, e - p); p = e + 1;
                size_t at = phase.find('@');
                if (at == std::string::npos || at == 0)
                {
                    m_invalid = "schedule entry '" + phase + "' is not <cycle>@<owners>";
                    break;
                }
                char *end = nullptr;
                uint64_t cycle = strtoull(phase.c_str(), &end, 10);
                if (end != phase.c_str() + at)
                {
                    m_invalid = "schedule entry '" + phase + "': '" + phase.substr(0, at) + "' is not a cycle";
                    break;
                }
                if (!m_phases.empty() && cycle <= last)
                {
                    m_invalid = "schedule cycles must increase (" + std::to_string(cycle) + " after " + std::to_string(last) + ")";
                    break;
                }
                last = cycle;
                m_phases.push_back({cycle, parse(phase.substr(at + 1), "schedule at cycle " + std::to_string(cycle))});
            }
            m_restricts = limits(m_core);
            for (auto &ph : m_phases) m_restricts = m_restricts || limits(ph.owners);
        }

        bool owns(int core, int unit) const override
        {
            if (core < 0 || unit < 0 || unit >= m_units) return core < 0;
            auto it = m_core.find(core);
            return it == m_core.end() || ((it->second >> unit) & 1);
        }
        bool restricts() const override { return m_restricts; }
        std::string invalid() const override { return m_invalid; }

        bool ticks() const override { return !m_phases.empty(); }
        bool schedules() const override { return true; }
        bool tick(uint64_t cycle) override
        {
            bool changed = false;
            while (m_next < m_phases.size() && m_phases[m_next].cycle <= cycle)
            {
                m_core = m_phases[m_next++].owners;
                changed = true;
            }
            return changed;
        }

    private:
        using Masks = std::map<int, uint64_t>;   // core -> capacity bit mask
        struct Phase { uint64_t cycle; Masks owners; };

        Masks parse(const std::string &spec, const std::string &where)
        {
            Masks m;
            auto fail = [&](const std::string &why) { if (m_invalid.empty()) m_invalid = where + ": " + why; };
            if (m_units > 64) { fail("cat masks cover at most 64 units (the scheme has " + std::to_string(m_units) + ")"); return m; }
            uint64_t all = (m_units >= 64) ? ~0ull : ((1ull << m_units) - 1);
            size_t p = 0;
            while (p < spec.size())
            {
                size_t e = spec.find(';', p); if (e == std::string::npos) e = spec.size();
                std::string entry = spec.substr(p, e - p); p = e + 1;
                if (entry.empty()) continue;
                size_t c = entry.find(':');
                if (c == std::string::npos) { fail("'" + entry + "' is not <cores>:<mask>"); continue; }
                std::string cores = entry.substr(0, c), hex = entry.substr(c + 1);
                int c0 = atoi(cores.c_str()), c1 = c0;
                size_t d = cores.find('-'); if (d != std::string::npos) c1 = atoi(cores.c_str() + d + 1);
                char *end = nullptr;
                uint64_t mask = strtoull(hex.c_str(), &end, 16);
                if (hex.empty() || *end != '\0') { fail("mask '" + hex + "' is not hex"); continue; }
                if (mask == 0) { fail("cores " + cores + " have an empty mask; such a core could never fill"); continue; }
                if (mask & ~all)
                {
                    std::ostringstream o; o << "mask " << hex << " sets a bit past unit " << m_units - 1;
                    fail(o.str()); continue;
                }
                uint64_t low = mask & (~mask + 1);   // lowest set bit
                if (m_contiguous && ((mask + low) & mask))
                {
                    fail("mask " + hex + " is not contiguous (CAT requires it; contiguous(i),0 lifts the rule)");
                    continue;
                }
                for (int core = c0; core <= c1; core++) m[core] = mask;
            }
            return m;
        }
        bool limits(const Masks &m) const
        {
            uint64_t all = (m_units >= 64) ? ~0ull : ((1ull << m_units) - 1);
            for (auto &kv : m) if (kv.second != all) return true;
            return false;
        }

        int m_units = 0;
        bool m_contiguous = true;
        std::string m_schedule_spec;
        std::string m_invalid;
        Masks m_core;                 // the masks in force
        std::vector<Phase> m_phases;  // the schedule
        size_t m_next = 0;            // the next phase to apply
        bool m_restricts = false;
    };

    REGISTER_OWNERSHIP("cat", CatOwnership);
}
