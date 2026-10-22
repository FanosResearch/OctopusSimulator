/*
 * File  :      Pipe.h
 * What the topology agents of plan §3.5 share: the pipe stage in front of an output channel
 * (plan §3.2: one cycle in, one cycle out, no combinational pass-through), message cloning and
 * lane copying by absolute byte address (§4.5), the configuration helpers, and the base class
 * that sorts an agent's links into upstream (slave role: masters above it) and downstream
 * (master role: slaves below it) and validates the configuration at elaboration.
 */
#ifndef _TL_AGENTS_PIPE_H
#define _TL_AGENTS_PIPE_H
#include "tilelink/Agent.h"
#include <deque>
#include <string>
#include <vector>

namespace octopus { namespace tl
{
    // Plan §3.2: the pipe stage. A beat accepted at t (pushed in the agent's commit) is offered
    // at t+1 at the earliest; `depth` bounds the beats held, and ready upstream is `space()`,
    // computed from the state at the start of the cycle (never from a downstream ready).
    // Depth 1 therefore carries one beat per two cycles, depth 2 one beat per cycle.
    struct BeatQueue
    {
        ChannelState     *out = nullptr;
        unsigned          depth = 2;
        std::deque<Beat>  q;

        bool space() const { return q.size() < depth; }
        bool empty() const { return q.empty(); }
        void push(const MsgPtr &m, unsigned k) { q.push_back(Beat{m, k}); }
        void pushAll(const MsgPtr &m) { for (unsigned k = 0; k < m->beats(); k++) push(m, k); }
        // Phase 1: offer the front beat; the beats of one message are contiguous in the queue,
        // so the burst lock of the output channel (§4.1) is always satisfied.
        void offer() const { if (out && !q.empty()) out->offer(q.front().msg, q.front().index); }
        // Phase 3: the front beat fired this cycle (valid && ready).
        void commit() { if (out && out->accepted().msg) q.pop_front(); }
    };

    // A new message with the same control fields, id and timestamps as `src` (so the operation
    // stays one operation end to end), laid out for a bus of `w` bytes in src's data mode.
    // The mask is the derived one (§4.5); callers copy lanes afterwards if they carry data.
    MsgPtr cloneMsg(const Msg &src, unsigned w);

    // §4.5: lane l of beat k carries the byte at laneBase + k*w + l, where laneBase is the
    // address rounded down to the bus width (equal to the address when 2^size >= w). The
    // "geometry address" is the operation's address: the wire address on A, B, C; on D, which
    // has no address field (Table 9), the address of the request it answers.
    inline uint64_t laneBase(uint64_t geom_address, unsigned w) { return geom_address & ~uint64_t(w - 1); }
    inline uint64_t laneBase(const Msg &m) { return laneBase(m.address, m.w); }
    struct LanePos { unsigned beat, lane; };
    inline LanePos lanePos(uint64_t geom_address, unsigned w, uint64_t abs_address)
    {
        uint64_t off = abs_address - laneBase(geom_address, w);
        return LanePos{unsigned(off / w), unsigned(off % w)};
    }
    inline LanePos lanePos(const Msg &m, uint64_t abs_address) { return lanePos(m.address, m.w, abs_address); }
    // Value of the byte at that absolute address as the checker sees it (plan §3.3): the byte
    // itself in mode Full, the stamp of its 8-byte group in mode Stamp, 0 in mode None.
    uint64_t laneValue(const Msg &m, uint64_t geom_address, uint64_t abs_address);
    inline uint64_t laneValue(const Msg &m, uint64_t abs_address) { return laneValue(m, m.address, abs_address); }
    // Copies mask and data/stamp of byte x between messages of any width, each placed by its
    // own geometry address.
    void copyByte(Msg &dst, uint64_t dst_geom, const Msg &src, uint64_t src_geom, uint64_t x);
    inline void copyByte(Msg &dst, const Msg &src, uint64_t x) { copyByte(dst, dst.address, src, src.address, x); }
    // Copies a whole beat (mask, data, stamp, corrupt) between messages of the same width.
    void copyBeat(Msg &dst, unsigned dst_beat, const Msg &src, unsigned src_beat);
    // The absolute byte range of beat k of an operation (geometry address, 2^size bytes, bus
    // w), clipped to the operation: [first, last] inclusive; false when the beat carries no
    // byte of it.
    bool beatRange(uint64_t geom_address, uint64_t bytes, unsigned w, unsigned k, uint64_t &first, uint64_t &last);
    inline bool beatRange(const Msg &m, unsigned k, uint64_t &first, uint64_t &last) { return beatRange(m.address, m.bytes(), m.w, k, first, last); }

    // Configuration helpers (plan §4): integers may be decimal or 0x-hex; ConfigError names the
    // agent and the key on a bad value.
    uint64_t parseU64(const std::string &text, const std::string &what);

    // The base of the topology agents: links sorted by role, a validate() that throws
    // ConfigError for a refused configuration. The kernel calls capability() for every link at
    // elaboration inside its ConfigError guard, so validate() runs there and a refusal is
    // reported through the single fatal path; tests call validate() directly.
    class TopologyAgent : public Agent
    {
    public:
        explicit TopologyAgent(const std::string &name) : Agent(name) {}
        virtual void validate() const = 0;
        Level capability(const Link &, Role) const override { validate(); return Level::C; }

    protected:
        std::vector<Link *> upLinks() const;     // links where this agent is the slave
        std::vector<Link *> downLinks() const;   // links where this agent is the master
        // For the one-in-one-out agents: exactly one upstream and one downstream link, same
        // level; `same_w` also requires the same bus width (the width converter is the one
        // agent that does not). ConfigError otherwise.
        void requireOneEach(const char *type, bool same_w) const;
        Link *up() const { return upLinks().at(0); }
        Link *down() const { return downLinks().at(0); }
        // The sender-side channel state for the agent's own sends: A, C, E go down, B, D go up.
        ChannelState &outChannel(Channel ch) const { return masterSends(ch) ? down()->chan(ch) : up()->chan(ch); }
        ChannelState &inChannel(Channel ch) const { return masterSends(ch) ? up()->chan(ch) : down()->chan(ch); }
        std::string me() const { return m_name; }
    };
}}
#endif /* _TL_AGENTS_PIPE_H */
