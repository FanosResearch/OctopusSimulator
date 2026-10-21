/*
 * File  :      Channel.cpp
 * The wire state of one channel: handshake (§4.1), the burst lock (§4.1, §5.1) and counters.
 */
#include "tilelink/Channel.h"
#include "tilelink/Link.h"

namespace octopus { namespace tl
{
    ChannelState::ChannelState(Link *link, Channel id) : m_link(link), m_id(id) {}

    const std::string &ChannelState::linkName() const { return m_link->name(); }
    std::string ChannelState::name() const { return m_link->name() + "." + channelName(m_id); }

    void ChannelState::violation(const std::string &what, uint64_t cycle)
    {
        if (onViolation) onViolation(*this, what, cycle);
        else fatal(name() + " cycle " + std::to_string(cycle) + ": " + what);
    }

    void ChannelState::beginCycle(uint64_t cycle)
    {
        // §3.2.1: outputs change only after the clock edge; each cycle starts with valid and
        // ready LOW and the sender re-offers from its state. The lock survives (§4.1: a burst in
        // progress stays in progress while valid is LOW).
        m_cycle = cycle;
        m_valid = m_ready = false;
        m_offer = Beat();
        m_accepted = Beat();
    }

    void ChannelState::offer(const MsgPtr &m, unsigned beat)
    {
        if (!m) { violation("offer of a null message", m_cycle); return; }
        if (m_valid) { violation("offered twice in one cycle (" + m->describe() + ")", m_cycle); return; }
        if (m->channel != m_id)
            violation("message for channel " + std::string(channelName(m->channel)) + " offered on " +
                      channelName(m_id) + " (" + m->describe() + ")", m_cycle);
        if (beat >= m->beats())
            violation("beat " + std::to_string(beat) + " offered but the message has " + std::to_string(m->beats()) +
                      " (" + m->describe() + ")", m_cycle);
        if (inProgress())
        {
            // §4.1: while a burst is in progress the sender may present only a beat from the
            // same message, the next one, with control signals identical to the first beat's.
            if (m != m_lock.msg)
                violation("burst in progress (" + m_lock.msg->describe() + ", beat " + std::to_string(m_lock.index) +
                          " next) but another message was offered (" + m->describe() + ") (§4.1)", m_cycle);
            else if (beat != m_lock.index)
                violation("burst in progress expects beat " + std::to_string(m_lock.index) + ", beat " +
                          std::to_string(beat) + " offered (" + m->describe() + ") (§4.1)", m_cycle);
            else if (m->header() != m_lock_header)
                violation("control fields changed during the burst (" + m->describe() + ") (§4.1)", m_cycle);
        }
        else if (beat != 0)
            violation("beat " + std::to_string(beat) + " offered with no burst in progress (" + m->describe() + ") (§4.1)", m_cycle);
        m_valid = true;
        m_offer = Beat{m, beat};
        if (beat == 0 && m != m_flight) { m_flight = m; m_flight_offer = m_cycle; }   // in flight from here (§5.1)
        if (beat == 0 && m->t_first_offer == NEVER) m->t_first_offer = m_cycle;
        if (onOffer) onOffer(*this, m_offer, m_cycle);
    }

    bool ChannelState::commit(uint64_t cycle)
    {
        if (m_valid) m_stats.busy++;
        if (m_valid && !m_ready) m_stats.stall++;
        if (!fires()) return false;
        // §4: the beat is exchanged only when valid and ready are both HIGH.
        m_stats.beats++;
        m_accepted = m_offer;
        Msg &m = *m_offer.msg;
        if (m_offer.index == 0)
        {
            if (m.t_first_accept == NEVER) m.t_first_accept = cycle;
            if (m.beats() > 1) { m_lock = Beat{m_offer.msg, 1}; m_lock_header = m.header(); }   // in progress (§5.1)
        }
        else
            m_lock.index = m_offer.index + 1;
        if (m_offer.last())
        {
            m.t_last_accept = cycle;                           // the latest link it completed on
            m_last_latency = cycle - (m_flight == m_offer.msg ? m_flight_offer : cycle);
            m_flight = nullptr;
            m_lock = Beat();                                   // the burst is complete
        }
        if (onAccept) onAccept(*this, m_accepted, cycle);
        return true;
    }
}}
