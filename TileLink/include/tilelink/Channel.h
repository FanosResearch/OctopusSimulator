/*
 * File  :      Channel.h
 * One channel of one link: the wire of one direction (§4: A, C, E master to slave; B, D slave
 * to master). It holds the offered beat, valid, ready, the burst lock and the statistics.
 * Plan §3.2 (three phases), §3.4.
 *
 * The enum tl::Channel names the five channels; this class, ChannelState, is the wire state
 * of one of them on one link.
 *
 * Cycle protocol (driven by the Kernel):
 *   beginCycle(t)  valid and ready go LOW; the burst lock survives.
 *   offer(m, k)    phase 1, the sender: valid HIGH with beat k of message m. Checked against
 *                  the burst lock (§4.1): once beat 0 of a message is accepted, only that
 *                  message's next beat may be offered, with identical control fields, until
 *                  its last beat is accepted. Not offering (valid LOW) is always allowed.
 *   setReady(r)    phase 2, the receiver. It may look at offered() (§4.1 allows ready to
 *                  depend on valid and the control signals) but never at another channel's
 *                  ready.
 *   commit(t)      phase 3, the kernel: the beat transfers iff valid && ready.
 * A violation goes to onViolation(this, what, cycle), which defaults to fatal(); T2's Monitor
 * installs itself there so that it can report instead of exit.
 */
#ifndef _TL_CHANNEL_H
#define _TL_CHANNEL_H
#include "Msg.h"
#include <functional>
#include <string>

namespace octopus { namespace tl
{
    class Link;

    // What is on the wire while valid is HIGH.
    struct Beat
    {
        MsgPtr   msg;
        unsigned index = 0;   // 0 = first beat
        bool last() const { return msg && index + 1 == msg->beats(); }
    };

    // Plan §4: per-channel counters for TLStats.csv.
    struct ChannelStats
    {
        uint64_t beats = 0;   // accepted beats
        uint64_t busy  = 0;   // cycles with valid HIGH
        uint64_t stall = 0;   // cycles with valid HIGH and ready LOW
    };

    class ChannelState
    {
    public:
        ChannelState(Link *link, Channel id);

        Channel            id() const { return m_id; }
        Link              *link() const { return m_link; }
        const std::string &linkName() const;
        std::string        name() const;    // "<link>.A"

        // ---- phase 1, the sender
        void offer(const MsgPtr &m, unsigned beat);
        bool valid() const { return m_valid; }
        const Beat &offered() const { return m_offer; }

        // ---- phase 2, the receiver
        void setReady(bool r) { m_ready = r; }
        bool ready() const { return m_ready; }

        // ---- phase 3 (kernel only)
        void beginCycle(uint64_t cycle);
        bool fires() const { return m_valid && m_ready; }
        bool commit(uint64_t cycle);         // true iff a beat transferred; updates the lock and stats
        // The beat that transferred in the last commit (empty if none): what the receiver's
        // commit() hook consumes.
        const Beat &accepted() const { return m_accepted; }

        // ---- the burst lock (§4.1 "in progress", §5.1)
        bool          inProgress() const { return m_lock.msg != nullptr; }
        const MsgPtr &inProgressMsg() const { return m_lock.msg; }
        unsigned      nextBeat() const { return m_lock.index; }

        const ChannelStats &stats() const { return m_stats; }
        // The transfer latency on THIS channel of the message whose last beat the last commit
        // accepted: its first offer of beat 0 here to that accept. Tracked per channel, since a
        // Msg object may travel several links (its own timestamps are end to end).
        uint64_t lastLatency() const { return m_last_latency; }

        // ---- observer hooks (the Monitor of T2; empty by default)
        std::function<void(const ChannelState &, const Beat &, uint64_t cycle)> onOffer;
        std::function<void(const ChannelState &, const Beat &, uint64_t cycle)> onAccept;
        // Default: fatal("<link>.<ch> cycle <t>: <what>").
        std::function<void(const ChannelState &, const std::string &what, uint64_t cycle)> onViolation;

    private:
        void violation(const std::string &what, uint64_t cycle);

        Link        *m_link;
        Channel      m_id;
        Beat         m_offer;
        Beat         m_accepted;
        bool         m_valid = false, m_ready = false;
        Beat         m_lock;             // msg = the in-progress message, index = the beat expected next
        MsgHeader    m_lock_header;      // control fields at beat 0, to check they stay constant (§4.1)
        MsgPtr       m_flight;           // the message last offered at beat 0 here (in flight, §5.1)
        uint64_t     m_flight_offer = 0; // the cycle it was first offered here
        uint64_t     m_last_latency = 0;
        uint64_t     m_cycle = 0;
        ChannelStats m_stats;
    };
}}
#endif /* _TL_CHANNEL_H */
