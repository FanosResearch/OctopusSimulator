/*
 * File  :      CoherenceCheck.h
 * The TL-C system checker (docs/TileLink_Plan.md §3.6 CoherenceCheck, §6 TL-FUZZ-C):
 *  - single writer / multiple reader per block, from the permissions observed on TL-C links: at most
 *    one master holds T, and then no other master holds B. Permissions are learned from accepted beats:
 *    a Grant[Data] sets the receiving master's permission (its cap) when the master accepts it; a
 *    ProbeAck[Data] or Release[Data] lowers it to the param's target when the master sends it (first
 *    beat accepted); a denied Grant changes nothing (§9.3.7). The manager-side check: when the
 *    manager offers a Grant toT, no other master may still hold B or T as last reported (all its
 *    ProbeAcks are in).
 *  - the data-value invariant, from agents' reports: a load returns the value of the most recent
 *    store to that byte (performed loads and stores are reported with their cycle; under SWMR the
 *    global order is the cycle order, ties broken by report order).
 * API contract shared by the T6 agents (CacheMaster, CoherentManager) and the checker; see the .cpp.
 */
#ifndef _TL_COHERENCECHECK_H
#define _TL_COHERENCECHECK_H
#include "tilelink/Kernel.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace octopus { namespace tl
{
    struct CoherenceViolation
    {
        std::string rule;          // "C-SWMR", "C-GRANT", "C-VALUE"
        uint64_t cycle = 0, block = 0, address = 0;
        std::string detail;
        std::string line() const;
    };

    class CoherenceCheck : public Observer
    {
    public:
        explicit CoherenceCheck(unsigned block_bytes = 64);
        const char *name() const override { return "CoherenceCheck"; }

        // Observe a TL-C link: chains onto the accept hooks of all five channels (and the offer hook
        // of D for the manager-side Grant check). The link's master is the cache whose permission is
        // tracked. Call after any Monitor/ValueCheck is attached (it chains; earlier hooks keep firing).
        void attach(Link &l);

        // The data-value invariant. Agents report each byte range when the access is performed: a
        // CacheMaster at the hit (load: the bytes returned; store: the bytes written), a manager when
        // it executes an uncached Put/Atomic (store) or answers an uncached Get (load). `agent` is the
        // reporting agent's id, for the message only.
        void loadPerformed(unsigned agent, uint64_t addr, const uint8_t *bytes, unsigned n, uint64_t cycle);
        void storePerformed(unsigned agent, uint64_t addr, const uint8_t *bytes, unsigned n, uint64_t cycle);

        // The checker registered on a kernel (Kernel::observer("CoherenceCheck")), or nullptr: agents
        // call `if (auto *c = CoherenceCheck::of(kernel())) c->loadPerformed(...)`.
        static CoherenceCheck *of(Kernel &k);

        struct Counters { uint64_t grants = 0, probe_acks = 0, releases = 0, loads = 0, stores = 0, bytes_checked = 0, violations = 0; };
        const Counters &counters() const { return m_c; }
        std::function<void(const CoherenceViolation &)> onViolation;   // default: fatal

        // permission of a master (link name) on a block as last observed: 'N', 'B' or 'T'
        char permission(const std::string &link, uint64_t block) const;

    private:
        unsigned m_block;
        Counters m_c;
        std::map<uint64_t, std::map<std::string, char>> m_perm;    // block -> master link -> N/B/T
        std::map<uint64_t, uint8_t> m_mem;                         // byte -> last stored value
        std::map<uint64_t, bool> m_known;                          // byte -> written at least once
        void violate(const CoherenceViolation &v);
    };
}}
#endif /* _TL_COHERENCECHECK_H */
