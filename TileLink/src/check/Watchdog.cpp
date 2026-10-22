/*
 * File  :      Watchdog.cpp
 * The per-message timers of plan §3.6 for §5's rules 2, 3i and 3ii. See Watchdog.h for the
 * operational definitions.
 */
#include "tilelink/Watchdog.h"
#include <algorithm>
#include <iostream>
#include <sstream>

namespace octopus { namespace tl
{
    const char *ruleName(ProgressRule r)
    {
        switch (r)
        {
            case ProgressRule::Rule2:   return "2";
            case ProgressRule::Rule3i:  return "3i";
            case ProgressRule::Rule3ii: return "3ii";
        }
        return "?";
    }

    std::string headerText(const MsgHeader &h)
    {
        std::ostringstream o;
        o << opcodeName(h.channel, h.opcode);
        // §6.4 / Table 14: the fields that identify a message on its channel.
        if (h.channel != Channel::E) o << " src=" << h.source;
        if (h.channel == Channel::D || h.channel == Channel::E) o << " sink=" << h.sink;
        if (h.channel == Channel::A || h.channel == Channel::B || h.channel == Channel::C) o << " addr=0x" << std::hex << h.address << std::dec;
        if (h.channel != Channel::E) o << " size=" << unsigned(h.size);
        return o.str();
    }

    std::string Trip::line() const
    {
        std::ostringstream o;
        o << "WATCHDOG rule " << ruleName(rule) << " link=" << link->name() << " channel=" << channelName(channel)
          << " agent=" << agent->name() << " cycle=" << cycle << " bound=" << bound << " msg=" << headerText(*msg)
          << " since=" << since << " timer=" << timer << " " << text;
        return o.str();
    }

    Watchdog::Watchdog(const WatchdogOptions &o) : m_opts(o), m_bound(o.bound) {}

    uint64_t Watchdog::defaultBound(const Kernel &k, std::string *formula)
    {
        // Plan §3.6: the sum of the slowest slave's latency, the deepest queue and the largest
        // burst, times the number of masters. Latency and queue come from Agent::hints().
        uint64_t latency = 1, queue = 1, burst = 1, masters = 0;
        for (auto &a : k.agents())
        {
            ProgressHints h = a->hints();
            latency = std::max(latency, h.latency);
            queue = std::max(queue, h.queue_depth);
            if (h.is_master) masters++;
        }
        for (auto &l : k.links()) burst = std::max<uint64_t>(burst, l->params().beatsOf(true, l->params().maxSize()));
        if (masters == 0) masters = 1;
        uint64_t bound = (latency + queue + burst) * masters;
        if (formula)
        {
            std::ostringstream o;
            o << "default formula (plan §3.6): (latency " << latency << " + queue " << queue << " + burst " << burst
              << " beats) x " << masters << " master" << (masters == 1 ? "" : "s") << " = " << bound;
            *formula = o.str();
        }
        return bound;
    }

    void Watchdog::onElaborate(Kernel &k)
    {
        if (m_bound == 0) m_bound = defaultBound(k, &m_formula);
        else m_formula = "explicit progress_bound";
        if (m_bound == 0) throw ConfigError("watchdog: progress_bound is 0");
        m_chan.clear();
        m_pending.clear();
        int idx = 0;
        for (auto &l : k.links())
        {
            l->forEachChannel([&](ChannelState &c) { m_chan[&c].link_index = idx; });
            idx++;
        }
        if (m_opts.info)
            *m_opts.info << "TileLink watchdog: progress_bound=" << m_bound << " (" << m_formula << "), mode="
                         << (m_opts.mode == WatchdogMode::Fatal ? "fatal" : "report") << std::endl;
    }

    size_t Watchdog::presented() const
    {
        size_t n = 0;
        for (auto &kv : m_chan) if (kv.second.presented_since != NEVER) n++;
        return n;
    }

    bool Watchdog::sameBeat(const ChanTimers &t, const Beat &b) const
    {
        // §5.1 "present": valid HIGH with every control and data signal held constant. The
        // control fields are the header; the data signals of beat k are its mask, data and
        // corrupt (Table 3).
        if (!t.prev_valid || t.prev_msg != b.msg || t.prev_index != b.index) return false;
        const Msg &m = *b.msg;
        if (m.header() != t.prev_header) return false;
        if (m.beatCorrupt(b.index) != t.prev_corrupt) return false;
        if (!m.mask.empty() && !std::equal(t.prev_mask.begin(), t.prev_mask.end(), m.beatMask(b.index), m.beatMask(b.index) + m.w)) return false;
        if (m.beatData(b.index) && !std::equal(t.prev_data.begin(), t.prev_data.end(), m.beatData(b.index), m.beatData(b.index) + m.w)) return false;
        return true;
    }

    void Watchdog::remember(ChanTimers &t, const Beat &b)
    {
        const Msg &m = *b.msg;
        t.prev_valid = true;
        t.prev_msg = b.msg;
        t.prev_index = b.index;
        t.prev_header = m.header();
        t.prev_corrupt = m.beatCorrupt(b.index);
        t.prev_mask.assign(m.mask.empty() ? nullptr : m.beatMask(b.index), m.mask.empty() ? nullptr : m.beatMask(b.index) + m.w);
        t.prev_data.assign(m.beatData(b.index) ? m.beatData(b.index) : nullptr, m.beatData(b.index) ? m.beatData(b.index) + m.w : nullptr);
    }

    bool Watchdog::answers(const Pending &p, const Msg &r, Channel rc) const
    {
        // Table 12: the response opcode must be one allowed for the request; plan §1.4: the
        // response is matched by source (D to an A request, D ReleaseAck to a C request: two
        // namespaces told apart by the opcode), by address (C to a B request) or by sink (E
        // GrantAck to a D Grant).
        bool allowed = false;
        for (const Response &x : responseOf(p.msg->channel, p.msg->opcode)) if (x.ch == rc && x.opcode == r.opcode) allowed = true;
        if (!allowed) return false;
        switch (p.msg->channel)
        {
            case Channel::A: case Channel::C: return r.source == p.msg->source;
            case Channel::B: return r.address == p.msg->address;
            case Channel::D: return r.sink == p.msg->sink;
            case Channel::E: return false;
        }
        return false;
    }

    void Watchdog::onCycleEnd(Kernel &k, uint64_t cycle)
    {
        if (k.inReset()) return;   // §3.2.2: nothing is offered during reset
        // 1. update every channel's timers from what happened this cycle
        for (auto &l : k.links())
            l->forEachChannel([&](ChannelState &c)
            {
                ChanTimers &t = m_chan[&c];
                const Beat &acc = c.accepted();
                if (acc.msg)
                {
                    const Msg &m = *acc.msg;
                    if (acc.index == 0)
                    {
                        // (c) a response's first beat answers the oldest matching request
                        for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
                            if (it->link == l.get() && answers(*it, m, c.id())) { m_pending.erase(it); break; }
                        // (c) a request is received: its 3ii timer starts (Grants are both)
                        if (m.isRequest()) m_pending.push_back({l.get(), c.id(), acc.msg, cycle, false});
                        // (b) a burst is in progress from its first accepted beat (§5.1)
                        if (m.beats() > 1) { t.burst = acc.msg; t.burst_accepted = 0; t.tripped_2 = false; }
                    }
                    if (t.burst) { t.burst_accepted = acc.index + 1; t.last_progress = cycle; }
                    if (acc.last()) t.burst = nullptr;
                    // (a) the presentation ended with the accept; the next offer starts afresh
                    t.presented_since = NEVER;
                    t.tripped_3i = false;
                    t.prev_valid = false;
                }
                else if (c.valid())
                {
                    // rejected: presented since its first unchanged offer; the sender of a
                    // burst makes progress for rule 2 by holding the beat stable
                    const Beat &b = c.offered();
                    bool stable = sameBeat(t, b);
                    if (!stable) { t.presented_since = cycle; t.tripped_3i = false; }
                    else if (t.burst && b.msg == t.burst) t.last_progress = cycle;
                    remember(t, b);
                }
                else
                {
                    // valid LOW: a retraction, or nothing to send (§5.1 "retract")
                    t.prev_valid = false;
                    t.presented_since = NEVER;
                    t.tripped_3i = false;
                }
            });
        // 2. the timers that reached the bound this cycle, in rule order 2, 3i, 3ii, then link
        //    and channel order, so that the verdict is deterministic
        std::vector<Trip> past;
        for (auto &l : k.links())
            l->forEachChannel([&](ChannelState &c)
            {
                ChanTimers &t = m_chan[&c];
                if (t.burst && !t.tripped_2 && cycle - t.last_progress >= m_bound)
                {
                    Trip tr{ProgressRule::Rule2, cycle, l.get(), c.id(), l->sender(c.id()), t.burst, t.last_progress,
                            cycle - t.last_progress, m_bound, t.burst_accepted, ""};
                    tr.text = std::to_string(t.burst_accepted) + " of " + std::to_string(t.burst->beats()) + " beats accepted, no further beat presented since " +
                              std::to_string(t.last_progress) + " (rule 2, §5: present all beats of a received message)";
                    t.tripped_2 = true;
                    past.push_back(tr);
                }
            });
        for (auto &l : k.links())
            l->forEachChannel([&](ChannelState &c)
            {
                ChanTimers &t = m_chan[&c];
                if (t.presented_since != NEVER && !t.tripped_3i && cycle - t.presented_since >= m_bound)
                {
                    const Beat &b = c.offered();
                    Trip tr{ProgressRule::Rule3i, cycle, l.get(), c.id(), l->receiver(c.id()), b.msg, t.presented_since,
                            cycle - t.presented_since, m_bound, b.index, ""};
                    tr.text = "beat " + std::to_string(b.index) + " of " + std::to_string(b.msg->beats()) + " presented since " +
                              std::to_string(t.presented_since) + " and not accepted (rule 3i, §5: accept a presented beat)";
                    t.tripped_3i = true;
                    past.push_back(tr);
                }
            });
        for (auto &p : m_pending)
            if (!p.tripped && cycle - p.received >= m_bound)
            {
                Trip tr{ProgressRule::Rule3ii, cycle, p.link, p.channel, p.link->receiver(p.channel), p.msg, p.received,
                        cycle - p.received, m_bound, 0, ""};
                tr.text = "request received at " + std::to_string(p.received) + " and not answered (rule 3ii, §5: answer a received request)";
                p.tripped = true;
                past.push_back(tr);
            }
        if (past.empty()) return;
        // 3. the verdict. Fatal: the dump, then exit 2 (plan §3.6: it never recovers).
        //    Report: one line per trip, the callback gets the dump.
        if (m_opts.mode == WatchdogMode::Fatal) fatal(dump(k, cycle, past));
        std::ostream &log = m_opts.log ? *m_opts.log : std::cerr;
        std::string d = dump(k, cycle, past);
        for (auto &tr : past)
        {
            log << "TileLink: " << tr.line() << std::endl;
            m_trips.push_back(tr);
            if (onTrip) onTrip(tr, d);
        }
    }

    std::string Watchdog::dump(const Kernel &k, uint64_t cycle, const std::vector<Trip> &past) const
    {
        std::ostringstream o;
        if (!past.empty()) o << past[0].line() << "\n";
        o << "  bound: progress_bound=" << m_bound << " (" << m_formula << "); a trip means longer than the declared bound, not non-conformance (plan §3.6)\n";
        for (size_t i = 1; i < past.size(); i++) o << "  also past the bound: " << past[i].line() << "\n";
        // every unanswered request in the system (rule 3's exemption needs them all)
        o << "  unanswered requests at cycle " << cycle << " (" << m_pending.size() << "):\n";
        for (auto &p : m_pending)
            o << "    link=" << p.link->name() << " channel=" << channelName(p.channel) << " agent=" << p.link->receiver(p.channel)->name()
              << " msg=" << headerText(*p.msg) << " received=" << p.received << " age=" << cycle - p.received << "\n";
        // every offered beat, and every burst in progress
        size_t offered = 0, bursts = 0;
        for (auto &kv : m_chan) { if (kv.first->valid()) offered++; if (kv.second.burst) bursts++; }
        o << "  offered beats (" << offered << "):\n";
        for (auto &l : k.links())
            l->forEachChannel([&](ChannelState &c)
            {
                if (!c.valid()) return;
                auto it = m_chan.find(&c);
                const Beat &b = c.offered();
                o << "    link=" << l->name() << " channel=" << channelName(c.id()) << " sender=" << l->sender(c.id())->name()
                  << " msg=" << headerText(*b.msg) << " beat=" << b.index << "/" << b.msg->beats() << " ready=" << (c.ready() ? 1 : 0);
                if (it != m_chan.end() && it->second.presented_since != NEVER)
                    o << " presented_since=" << it->second.presented_since << " age=" << cycle - it->second.presented_since;
                o << "\n";
            });
        o << "  bursts in progress (" << bursts << "):\n";
        for (auto &l : k.links())
            l->forEachChannel([&](ChannelState &c)
            {
                auto it = m_chan.find(&c);
                if (it == m_chan.end() || !it->second.burst) return;
                const ChanTimers &t = it->second;
                o << "    link=" << l->name() << " channel=" << channelName(c.id()) << " sender=" << l->sender(c.id())->name()
                  << " msg=" << headerText(*t.burst) << " accepted=" << t.burst_accepted << "/" << t.burst->beats()
                  << " last_progress=" << t.last_progress << " age=" << cycle - t.last_progress << "\n";
            });
        std::string s = o.str();
        if (!s.empty() && s.back() == '\n') s.pop_back();
        return s;
    }
}}
