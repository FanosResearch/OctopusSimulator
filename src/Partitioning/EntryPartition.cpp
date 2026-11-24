/*
 * File  :      EntryPartition.cpp
 *
 * MSHR partitioning (docs/LLC_Banking_and_Partitioning_Design.md s6.10): the units are a cache's
 * MSHR entries (num_mshr). A new miss takes a free unit its core owns and holds it until the miss
 * completes, so one core cannot hold every MSHR. Requests from other cores that join an
 * outstanding miss take no unit: the miss is charged to the core that opened it.
 *
 * The limit is back-pressure at the sender, not a refusal at the cache: the cache's demand gate
 * (CacheController::installDemandGate) admits a core's demand request onto the bus only while the
 * core has more free units than requests already queued at the cache, each of which may still
 * open a miss. A cache never turns away a request the bus delivered, which would break the bus
 * order a snooping protocol relies on.
 *
 * With a disjoint ownership this is a per-core quota; with an overlapping one two cores draw on
 * their common units.
 */
#include "Partitioning.h"

#include <map>
#include <ostream>
#include <vector>

namespace octopus
{
    class EntryPartition : public PartitionScheme
    {
    public:
        EntryPartition(Entry kind, const char *name, const char *size_key, int entries)
            : m_kind(kind), m_name(name), m_size_key(size_key), m_entries(entries),
              m_holder(entries > 0 ? entries : 0, FREE) {}
        const char *name() const override { return m_name; }
        int units() const override { return m_entries > 0 ? m_entries : 1; }
        std::string invalid() const override
        {
            if (m_entries > 0) return "";
            return std::string("needs a bounded ") + m_size_key + " (it is " + std::to_string(m_entries) + "; -1 = unbounded)";
        }

        bool countsEntries() const override { return true; }
        Entry entryKind() const override { return m_kind; }

        // a new miss of `core` is admitted while one of its units is free
        bool admit(int core, uint64_t addr) const override
        {
            return core < 0 || !m_owners->restricts() || freeUnit(core) >= 0;
        }
        int freeUnits(int core) const override
        {
            if (core < 0 || !m_owners->restricts()) return 1 << 30;
            int n = 0;
            for (int u = 0; u < m_entries; u++)
                if (m_holder[u] == FREE && m_owners->owns(core, u)) n++;
            return n;
        }
        bool allocate(int core, uint64_t key) override
        {
            if (m_held.count(key)) return true;
            int u = freeUnit(core);
            if (u >= 0) m_holder[u] = core;
            m_held[key] = {u, core};
            if (core >= 0) { int n = ++m_count[core]; if (n > m_peak[core]) m_peak[core] = n; }
            return u >= 0;
        }
        void release(uint64_t key) override
        {
            auto it = m_held.find(key);
            if (it == m_held.end()) return;
            if (it->second.unit >= 0) m_holder[it->second.unit] = FREE;
            if (it->second.core >= 0) m_count[it->second.core]--;
            m_held.erase(it);
        }

        void summary(std::ostream &os, const std::string &cache, int index) const override
        {
            for (auto &kv : m_peak)
            {
                int owned = 0;
                for (int u = 0; u < m_entries; u++) owned += m_owners->owns(kv.first, u);
                os << cache << " partition[" << index << "] (" << m_name << "): core " << kv.first << " peak "
                   << kv.second << " of " << owned << " unit(s)" << std::endl;
            }
        }

    private:
        static constexpr int FREE = -2;
        struct Held { int unit; int core; };
        int freeUnit(int core) const
        {
            for (int u = 0; u < m_entries; u++)
                if (m_holder[u] == FREE && m_owners->owns(core, u)) return u;
            return -1;
        }
        Entry m_kind;
        const char *m_name;
        const char *m_size_key;
        int m_entries;
        std::vector<int> m_holder;            // unit -> core holding it, FREE
        std::map<uint64_t, Held> m_held;      // entry key (line address) -> its unit (-1 = none) and core
        std::map<int, int> m_count, m_peak;   // entries held per core, and the most at once
    };

    class MshrPartition : public EntryPartition
    {
    public:
        explicit MshrPartition(const PartitionContext &c) : EntryPartition(Entry::MSHR, "mshr", "num_mshr", c.mshr_entries) {}
    };

    REGISTER_PARTITION_SCHEME("mshr", MshrPartition);
}
