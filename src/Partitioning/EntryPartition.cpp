/*
 * File  :      EntryPartition.cpp
 *
 * MSHR and write-back buffer partitioning (docs/LLC_Banking_and_Partitioning_Design.md s6.10).
 *
 * mshr: the units are a cache's MSHR entries (num_mshr). A new miss takes a free unit its core
 * owns and holds it until the miss completes, so one core cannot hold every MSHR. Requests from
 * other cores that join an outstanding miss take no unit: the miss is charged to the core that
 * opened it.
 *
 * pwb: the units are the write-back buffer entries (pwb_size). An evicted line takes a unit until
 * its write-back completes. pwb_owner(s) picks the core it is charged to:
 *   requester (default)  the core whose fill evicted it. Gated at the sender, as mshr is.
 *   victim               the core that last placed the line's data here (GenericCacheLine::
 *                        placed_by). The victim is known only at fill time, so nothing can hold
 *                        it back: an entry beyond the victim core's units is a violation.
 *                        Accounting only.
 *
 * The limits are back-pressure at the sender, not a refusal at the cache: the cache's demand gate
 * (CacheController::installDemandGate) admits a core's demand request onto the bus only while the
 * core has more free MSHR units than requests already queued at the cache (each may still open a
 * miss), and, for a requester-charged pwb scheme, more free write-back units than those requests
 * plus its open misses (each may still evict a line when it fills). A cache never turns away a
 * request the bus delivered, which would break the bus order a snooping protocol relies on.
 *
 * With a disjoint ownership this is a per-core quota; with an overlapping one two cores draw on
 * their common units.
 */
#include "Partitioning.h"

#include <algorithm>
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
            return !m_gates || core < 0 || !m_owners->restricts() || freeUnit(core) >= 0;
        }
        bool gatesDemand() const override { return m_gates && m_owners->restricts(); }
        int freeUnits(int core) const override
        {
            if (core < 0 || !m_owners->restricts()) return 1 << 30;
            int n = 0;
            for (int u = 0; u < m_entries; u++)
                if (m_holder[u] == FREE && m_owners->owns(core, u)) n++;
            return n;
        }
        bool admitsDemand(int core, const std::map<int, int> &need) const override
        {
            if (core < 0 || !m_owners->restricts()) return true;
            return matchable(need, core, -1);
        }
        void setCores(int n) override { m_cores = n; m_overlap = -1; }
        bool overlaps() const override
        {
            if (m_overlap < 0)
            {
                m_overlap = 0;
                for (int u = 0; u < m_entries && !m_overlap; u++)
                {
                    int owners = 0;
                    for (int c = 0; c < m_cores; c++) owners += m_owners->owns(c, u);
                    m_overlap = owners > 1;
                }
            }
            return m_overlap == 1;
        }
        // A new entry takes the free unit of its core that the fewest other cores own (its private
        // units first). With `waiting`, the first such unit whose loss still leaves every waiting
        // request a free unit of its own core; if none does, the least shared one.
        bool allocate(int core, uint64_t key, const std::map<int, int> *waiting = nullptr) override
        {
            if (m_held.count(key)) return true;
            std::vector<std::pair<int, int>> free;   // (cores sharing it, unit) of the core's free units
            for (int v = 0; v < m_entries; v++)
                if (m_holder[v] == FREE && m_owners->owns(core, v)) free.push_back({sharers(v), v});
            std::sort(free.begin(), free.end());
            int u = free.empty() ? -1 : free.front().second;
            if (waiting)
                for (auto &f : free)
                    if (matchable(*waiting, -1, f.second)) { u = f.second; break; }
            if (u >= 0) m_holder[u] = core;
            m_held[key] = {u, core};
            if (core >= 0) { int n = ++m_count[core]; if (n > m_peak[core]) m_peak[core] = n; }
            return u >= 0;
        }
        int entryUnit(uint64_t key) const override
        {
            auto it = m_held.find(key);
            return it == m_held.end() ? -1 : it->second.unit;
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

    protected:
        bool m_gates = true;                  // the sender holds a core back while it has no free unit
        void ownershipChanged() override { m_overlap = -1; }

    private:
        static constexpr int FREE = -2;
        struct Held { int unit; int core; };
        int freeUnit(int core) const
        {
            for (int u = 0; u < m_entries; u++)
                if (m_holder[u] == FREE && m_owners->owns(core, u)) return u;
            return -1;
        }
        int sharers(int u) const
        {
            int n = 0;
            for (int c = 0; c < m_cores; c++) n += m_owners->owns(c, u);
            return n;
        }
        // Hall's condition: every set of cores can give each of its waiting requests (`need`, plus
        // one for `extra`) a distinct free unit (unit `removed` taken) that one of its cores owns.
        // Cores owning the same units form one group. Only the sets the change can affect are
        // checked: those holding `extra`'s group, or whose units include `removed`. With disjoint
        // owners this is the per-core rule: free units > waiting requests.
        bool matchable(const std::map<int, int> &need, int extra, int removed) const
        {
            int top = m_cores;
            for (auto &kv : need) top = std::max(top, kv.first + 1);
            top = std::max(top, extra + 1);
            std::map<std::vector<bool>, int> groups;   // owned units -> requests waiting
            std::vector<bool> extra_mask;
            for (int c = 0; c < top; c++)
            {
                auto it = need.find(c);
                int n = (it == need.end() ? 0 : it->second) + (c == extra);
                if (n == 0 && c != extra) continue;
                std::vector<bool> m(m_entries);
                for (int u = 0; u < m_entries; u++) m[u] = m_owners->owns(c, u);
                groups[m] += n;
                if (c == extra) extra_mask = m;
            }
            std::vector<std::pair<std::vector<bool>, int>> g(groups.begin(), groups.end());
            if (g.size() > 16)   // too many distinct owner sets to enumerate: per core only
            {
                for (auto &p : g)
                {
                    int f = 0;
                    for (int u = 0; u < m_entries; u++) f += p.first[u] && m_holder[u] == FREE && u != removed;
                    if (p.second > f) return false;
                }
                return true;
            }
            for (uint32_t s = 1; s < (1u << g.size()); s++)
            {
                bool has_extra = extra < 0, has_removed = removed < 0;
                int waiting = 0;
                std::vector<bool> uni(m_entries, false);
                for (size_t i = 0; i < g.size(); i++)
                {
                    if (!(s >> i & 1)) continue;
                    waiting += g[i].second;
                    if (extra >= 0 && g[i].first == extra_mask) has_extra = true;
                    for (int u = 0; u < m_entries; u++) uni[u] = uni[u] || g[i].first[u];
                }
                if (removed >= 0 && uni[removed]) has_removed = true;
                if (!has_extra || !has_removed) continue;
                int free = 0;
                for (int u = 0; u < m_entries; u++) free += uni[u] && m_holder[u] == FREE && u != removed;
                if (waiting > free) return false;
            }
            return true;
        }
        int m_cores = 0;
        mutable int m_overlap = -1;           // overlaps(): -1 = not computed
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

    class PwbPartition : public EntryPartition
    {
    public:
        explicit PwbPartition(const PartitionContext &c)
            : EntryPartition(Entry::PWB, "pwb", "pwb_size", c.pwb_entries), m_victim(c.pwb_owner == "victim"), m_mode(c.pwb_owner)
        { m_gates = !m_victim; }
        std::string invalid() const override
        {
            if (m_mode != "requester" && m_mode != "victim")
                return "pwb_owner(s) is '" + m_mode + "' (requester or victim)";
            return EntryPartition::invalid();
        }
        int chargedCore(int core, int placed_by) const override { return m_victim ? placed_by : core; }

    private:
        bool m_victim;
        std::string m_mode;
    };

    REGISTER_PARTITION_SCHEME("mshr", MshrPartition);
    REGISTER_PARTITION_SCHEME("pwb", PwbPartition);
}
