/*
 * File  :      Xbar.h
 * Xbar (plan §3.5): N upstream links (masters) x M downstream links (slaves), all of one width
 * and level. Routing per Table 14 (§6.4): A and C by address through the address map, B and D
 * by source range, E by sink range. ID remapping per §6.4: each upstream link's sources
 * [0, 2^o) are placed at a base in one downstream namespace (bases are assigned in link order,
 * packed), each downstream link's sinks likewise in one upstream namespace; the downstream o
 * and the upstream i must hold the totals (elaboration error otherwise). Arbitration per
 * output channel (round-robin, fixed priority, or TDM slots), and the grant is held for the
 * whole burst (§4.1: bursts are not interleaved). Every channel has its own output stage, so
 * the C, D and E paths accept beats whether or not A and B are backed up: a high-priority ready
 * never depends on a low-priority ready (§4.1), which is what makes the crossbar conforming
 * under rule 3 of §5. Every path adds exactly one cycle when its stage is empty (plan §3.2).
 *
 * Config keys (agent[k].*):
 *   routes(vs)       entries "link:base:size" (hex or decimal): A/C messages whose address is
 *                    in [base, base+size) go to that downstream link; a link may appear in
 *                    several entries; ranges must not overlap (§6.3: exactly one slave).
 *                    Or entries "link:interleave:<shift>:<count>:<index>": line-interleaved
 *                    banks, the address goes to that link when
 *                    ((address / block_bytes) >> shift) % count == index, Octopus's home map
 *                    (BaseController::homeOf with home_ids / home_interleave_shift). Every
 *                    interleave entry of one crossbar has the same shift and count and its own
 *                    index; the two forms are not mixed (only then is "exactly one slave per
 *                    address" checkable at elaboration)
 *   block_bytes(i)   the line the interleave form counts in (default 64: (address >> 6 >> shift))
 *   default(s)       the downstream link for an unmapped address (optional; without it an
 *                    unmapped address is a fatal error at run time)
 *   arbitration(s)   rr (default) | fixed | tdm
 *   tdm_slot(i)      TDM slot length in cycles (default 1); slot s belongs to upstream link
 *                    s mod N for A/C/E, to downstream link s mod M for B/D
 *   depth(i)         output stage depth per channel (default 2; 1 halves the throughput)
 */
#ifndef _TL_AGENTS_XBAR_H
#define _TL_AGENTS_XBAR_H
#include "tilelink/agents/Pipe.h"

namespace octopus { namespace tl
{
    class Xbar : public TopologyAgent
    {
    public:
        enum class Policy { RoundRobin, Fixed, TDM };

        Xbar(const std::string &name, const Config &cfg);
        void validate() const override;
        void reset(uint64_t reset_cycles) override;
        void offer(uint64_t cycle) override;
        void accept(uint64_t cycle) override;
        void commit(uint64_t cycle) override;
        bool idle() const override;
        ProgressHints hints() const override;

        // The remapping (§6.4), for tests and reports: the base of an upstream link's sources
        // in the downstream namespace, and of a downstream link's sinks in the upstream one.
        uint64_t sourceBase(const Link &up) const;
        uint64_t sinkBase(const Link &down) const;
        // The downstream link an address routes to (nullptr when unmapped and no default).
        Link *routeOf(uint64_t address) const;
        Policy policy() const { return m_policy; }

    private:
        // A range [base, base+size), or an interleave slot (plan §5.3 I-A: Octopus's line-
        // interleaved LLC banks).
        struct Route
        {
            std::string link; uint64_t base = 0, size = 0; int out = -1;
            bool interleave = false; unsigned shift = 0, count = 1, index = 0;
            bool covers(uint64_t address, unsigned lg_block) const
            {
                if (interleave) return ((address >> lg_block) >> shift) % count == index;
                return address >= base && address - base < size;
            }
        };
        // One output channel of one plane: its pipe stage and its arbitration state.
        struct Out
        {
            ChannelState    *ch = nullptr;
            std::deque<Beat> q;
            int              grant = -1;   // the input holding the output for a burst in progress
            int              rr = -1;      // the input granted last (round-robin pointer)
        };
        // One input channel of one plane: the clone being built for the message in progress.
        struct In
        {
            ChannelState *ch = nullptr;
            MsgPtr        clone;
            int           out = -1;
        };
        // One channel across all links: A, C, E go upstream->downstream, B, D the other way.
        struct Plane
        {
            Channel          ch = Channel::A;
            std::vector<In>  in;
            std::vector<Out> out;
        };

        void   build() const;                                       // resolves routes and bases once
        int    decode(const Plane &p, const Msg &m, int in) const;  // the output index, -1 if none
        int    routeIndex(uint64_t address) const;                  // A/C: the output an address routes to, -1 if none
        MsgPtr remap(const Plane &p, const Msg &m, int in, int out) const;
        int    pick(const Plane &p, Out &o, int j, uint64_t cycle) const;

        std::vector<Route>    m_routes_cfg;
        std::string           m_default_name;
        Policy                m_policy = Policy::RoundRobin;
        unsigned              m_slot = 1, m_depth = 2;
        unsigned              m_lg_block = 6;                       // block_bytes(i), for the interleave form

        // Built by build() (validate is const, so these are mutable and built once).
        mutable bool                  m_built = false;
        mutable std::vector<Link *>   m_up, m_down;
        mutable std::vector<Route>    m_routes;
        mutable int                   m_default = -1;
        mutable std::vector<uint64_t> m_src_base, m_sink_base;
        std::vector<Plane>            m_planes;
    };
}}
#endif /* _TL_AGENTS_XBAR_H */
