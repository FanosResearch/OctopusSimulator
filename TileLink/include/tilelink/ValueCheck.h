/*
 * File  :      ValueCheck.h
 * ValueCheck (plan §3.6): the admissible-set data checker for TL-UL/UH traffic on a link, in
 * data modes Full (the bytes) and Stamp (the stamp of a byte's 8-byte group is its value).
 * Acknowledgement order is not write order: §6.5 lets a slave serialize two outstanding
 * writes either way, so with W1 = 1 and W2 = 2 both outstanding and W2 acknowledged first a
 * later read may legally return 1 or 2. Per byte the checker keeps the set of admissible
 * values, as rocket-chip's TLRAMModel does:
 *   - a write adds its byte value to the set when it is issued (masked bytes only; the value
 *     of a beat is known when that beat is accepted, the issue time is the first beat's);
 *   - when a write is acknowledged and no other write to that byte was outstanding during its
 *     whole issue-to-acknowledgement interval, the set collapses to that write's value;
 *   - otherwise the set keeps every value of the writes whose intervals overlapped, until a
 *     later write collapses it (a write acknowledged before this one was issued is ordered
 *     before it, §6.5, and its value is dropped);
 *   - a read's returned byte must be in the set as it stood at the read's issue, plus the
 *     values of writes outstanding during the read's interval;
 *   - an atomic's returned old value is checked the same way, and its written value is
 *     computed from the returned old value (plan §1.3: MIN/MAX/MINU/MAXU/ADD on the
 *     2^size-byte little-endian integer, XOR/OR/AND/SWAP per byte), so a wrong old value is
 *     caught once, not twice; until it is acknowledged the atomic's bytes admit any value;
 *   - a denied or corrupt beat is not checked, a denied write adds nothing, a corrupt write
 *     beat makes its bytes admit any value;
 *   - a byte never written admits any value.
 * It observes accepted beats through the channels' onAccept hooks (chained after whatever
 * was installed before, e.g. the Monitor) and never changes behaviour. Requests are matched
 * to responses by source on the link (Table 14). Violations go to onViolation, by default
 * fatal with the rule (V-READ, V-ATOMIC), cycle, link, address, value and admissible set.
 * Several links may share one checker when they see one memory (e.g. all the masters' links
 * of a crossbar): the byte model is the checker's, the outstanding table is per link.
 */
#ifndef _TL_VALUECHECK_H
#define _TL_VALUECHECK_H
#include "tilelink/Link.h"
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace octopus { namespace tl
{
    struct ValueViolation
    {
        std::string           rule;        // "V-READ" or "V-ATOMIC"
        uint64_t              cycle = 0;
        std::string           link;
        uint64_t              address = 0; // the byte
        uint64_t              got = 0;
        std::vector<uint64_t> admissible;  // sorted
        std::string           request;     // Msg::describe() of the request
        std::string           describe() const;
    };

    class ValueCheck
    {
    public:
        explicit ValueCheck(const std::string &name = "ValueCheck") : m_name(name) {}

        // Chains itself onto the A and D onAccept hooks of the link.
        void attach(Link &l);
        std::function<void(const ValueViolation &)> onViolation;   // default: fatal

        struct Counters
        {
            uint64_t writes = 0, writes_acked = 0, reads = 0, atomics = 0;
            uint64_t bytes_checked = 0, bytes_skipped = 0;   // skipped: denied or corrupt
            uint64_t denied = 0, violations = 0;
        };
        const Counters &counters() const { return m_c; }
        const std::string &name() const { return m_name; }

        // The set of a byte now, for tests: false when any value is admissible.
        bool admissible(uint64_t address, std::vector<uint64_t> &values) const;
        size_t outstanding() const { return m_ops.size(); }

    private:
        struct WriteRec
        {
            uint64_t id = 0, issue = 0, ack = NEVER, value = 0;
            bool unknown = false;   // value not known (atomic before its ack, corrupt beat)
        };
        struct ByteState
        {
            bool initial = true;                 // never written: any value
            std::vector<WriteRec> writes;        // outstanding, or acked and not yet overwritten
        };
        struct Admissible { std::set<uint64_t> values; bool any = false; };
        struct Op
        {
            const Link *link = nullptr;
            MsgPtr      req;
            uint64_t    issue = 0, wid = 0;
            std::map<uint64_t, Admissible> snap;   // reads and atomics: per byte, at issue + accumulated
            std::map<uint64_t, uint64_t>   old;    // atomics: the returned old value per byte
            std::set<uint64_t>             old_unknown;
        };
        using Key = std::pair<const Link *, uint64_t>;   // (link, source)

        void onA(const ChannelState &c, const Beat &b, uint64_t t);
        void onD(const ChannelState &c, const Beat &b, uint64_t t);
        void addWrite(Op &op, uint64_t x, uint64_t value, bool unknown);
        void ackWrite(Op &op, uint64_t t, bool denied);
        void checkByte(const Op &op, uint64_t x, uint64_t got, uint64_t t, const char *rule);
        void finishAtomic(Op &op, uint64_t t);
        Admissible current(uint64_t x) const;

        std::string m_name;
        Counters    m_c;
        std::map<uint64_t, ByteState> m_bytes;
        std::map<Key, Op> m_ops;
        uint64_t    m_next_wid = 0;
    };
}}
#endif /* _TL_VALUECHECK_H */
