/*
 * File  :      CoherenceCheck.h
 * The TL-C system checker (docs/TileLink_Plan.md §3.6 CoherenceCheck, §6 TL-FUZZ-C):
 *  - single writer / multiple reader per block, from the permissions observed on TL-C links: at most
 *    one master holds T, and then no other master holds B. Permissions are learned from accepted beats:
 *    a Grant[Data] sets the receiving master's permission (its cap) when the master accepts it (first
 *    beat accepted); a ProbeAck[Data] or Release[Data] lowers it to the param's target when the master
 *    sends it (first beat accepted); a denied Grant changes nothing (§9.3.7). Rule C-SWMR. The
 *    manager-side check, rule C-GRANT: when the manager offers a Grant toT, no other master may still
 *    hold B or T as last reported (all its ProbeAcks are in); a Grant toB, no other master at T.
 *  - the data-value invariant, from agents' reports: a load returns the value of the most recent
 *    store to that byte (performed loads and stores are reported with their cycle; under SWMR the
 *    global order is the cycle order, ties broken by report order). Rule C-VALUE.
 * Violations go to onViolation; by default fatal: "TileLink: coherence <rule>: cycle=<n>
 * block=<hex> addr=<hex>: <detail>", exit 2.
 * API contract shared by the T6 agents (CacheMaster, CoherentManager) and the checker; see the .cpp.
 *
 * Where it attaches: on the masters' own TL-C links, upstream of any Xbar. A permission is attributed
 * to the link's master agent; a link that carries several masters (the output of an Xbar) cannot say
 * which of them a Grant or a ProbeAck belongs to, so attach() refuses (ConfigError) a link whose
 * master is not a leaf (an agent that is a slave on some other link), and a link below TL-C.
 * attachAll() (and onElaborate, when nothing was attached by hand) attaches every TL-C link whose
 * master is a leaf. A "block" is the block's base address (address & ~(block_bytes - 1));
 * permission() accepts any byte address of the block.
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

        // Observe a TL-C link: chains onto the accept hooks of the channels that carry permission
        // changes (A: the Acquire a Grant answers; C: ProbeAck[Data], Release[Data]; D: Grant[Data])
        // and the offer hook of D for the manager-side Grant check. The link's master is the cache
        // whose permission is tracked. Call after any Monitor/ValueCheck is attached (it chains;
        // earlier hooks keep firing) and after the system is connected (the leaf check reads the
        // master's links; onElaborate checks again). Attaching a link twice is a no-op.
        void attach(Link &l);
        // Attaches every TL-C link of the kernel whose master is a leaf; returns how many.
        unsigned attachAll(Kernel &k);
        const std::vector<Link *> &attached() const { return m_links; }
        // Observer: remembers the kernel (agent names in the messages); attaches every eligible
        // link when auto_attach is set and none was attached by hand; re-checks the leaf rule.
        void onElaborate(Kernel &k) override;
        bool auto_attach = true;

        // The data-value invariant. Agents report each byte range when the access is performed: a
        // CacheMaster at the hit (load: the bytes returned; store: the bytes written), a manager when
        // it executes an uncached Put/Atomic (store) or answers an uncached Get (load). `agent` is the
        // reporting agent's id, for the message only.
        void loadPerformed(unsigned agent, uint64_t addr, const uint8_t *bytes, unsigned n, uint64_t cycle);
        void storePerformed(unsigned agent, uint64_t addr, const uint8_t *bytes, unsigned n, uint64_t cycle);
        // The initial memory image: those bytes are known from the start (as if stored before
        // cycle 0), so a load of them is checked before any store. Without it a byte never stored
        // admits any value.
        void setInitial(uint64_t addr, const uint8_t *bytes, unsigned n);

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

        struct Writer { int agent = -1; uint64_t cycle = 0; bool initial = false; };
        using Key = std::pair<const Link *, uint64_t>;          // (link, A source)
        std::map<Key, uint64_t> m_acq;                         // outstanding Acquire -> block
        std::map<uint64_t, Writer> m_writer;                   // byte -> who stored it last
        std::vector<Link *> m_links;                           // attached
        std::map<uint64_t, bool> m_grant_reported;             // Grant msg key -> C-GRANT reported
        Kernel *m_kernel = nullptr;
        uint64_t blockOf(uint64_t address) const { return address & ~uint64_t(m_block - 1); }
        char &permRef(uint64_t block, const std::string &link);
        void checkLeaf(const Link &l) const;
        void onA(const ChannelState &c, const Beat &b, uint64_t t);
        void onC(const ChannelState &c, const Beat &b, uint64_t t);
        void onDOffer(const ChannelState &c, const Beat &b, uint64_t t);
        void onDAccept(const ChannelState &c, const Beat &b, uint64_t t);
        std::string agentName(int id) const;
    };
}}
#endif /* _TL_COHERENCECHECK_H */
