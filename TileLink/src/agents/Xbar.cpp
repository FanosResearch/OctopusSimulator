/*
 * File  :      Xbar.cpp
 * Xbar (plan §3.5): routing (§6.3, §6.4 Table 14), ID remapping (§6.4), per-output arbitration
 * with the grant held for a burst (§4.1), independent channel stages (§4.1, §5).
 */
#include "tilelink/agents/Xbar.h"
#include <algorithm>
#include <sstream>

namespace octopus { namespace tl
{
    Xbar::Xbar(const std::string &name, const Config &cfg) : TopologyAgent(name)
    {
        std::string pol = cfg.get<std::string>("arbitration", "rr");
        if (pol == "rr" || pol == "round-robin") m_policy = Policy::RoundRobin;
        else if (pol == "fixed") m_policy = Policy::Fixed;
        else if (pol == "tdm") m_policy = Policy::TDM;
        else throw ConfigError("Xbar " + name + ": arbitration must be rr, fixed or tdm, got " + pol);
        int slot = cfg.get<int>("tdm_slot", 1);
        if (slot <= 0) throw ConfigError("Xbar " + name + ": tdm_slot must be >= 1");
        m_slot = unsigned(slot);
        int depth = cfg.get<int>("depth", 2);
        if (depth <= 0) throw ConfigError("Xbar " + name + ": depth must be >= 1");
        m_depth = unsigned(depth);
        m_default_name = cfg.get<std::string>("default", "");
        int bb = cfg.get<int>("block_bytes", 64);
        if (bb <= 0 || (bb & (bb - 1))) throw ConfigError("Xbar " + name + ": block_bytes must be a power of two, got " + std::to_string(bb));
        m_lg_block = 0;
        while ((1u << m_lg_block) < unsigned(bb)) m_lg_block++;
        for (auto &e : cfg.get<std::vector<std::string>>("routes", std::vector<std::string>()))
        {
            // "link:base:size", or "link:interleave:<shift>:<count>:<index>"
            std::vector<std::string> f;
            for (size_t at = 0;;)
            {
                size_t c = e.find(':', at);
                f.push_back(e.substr(at, c == std::string::npos ? std::string::npos : c - at));
                if (c == std::string::npos) break;
                at = c + 1;
            }
            Route r;
            r.link = f[0];
            if (f.size() == 5 && f[1] == "interleave")
            {
                // Octopus's line-interleaved banks (plan §5.3 I-A): ((address / block) >> shift) % count == index
                r.interleave = true;
                uint64_t shift = parseU64(f[2], "Xbar " + name + " routes interleave shift");
                uint64_t count = parseU64(f[3], "Xbar " + name + " routes interleave count");
                uint64_t index = parseU64(f[4], "Xbar " + name + " routes interleave index");
                if (shift >= 58 || count == 0 || count > 4096 || index >= count)
                    throw ConfigError("Xbar " + name + ": routes entry '" + e + "' needs shift < 58, 1 <= count <= 4096 and index < count");
                r.shift = unsigned(shift); r.count = unsigned(count); r.index = unsigned(index);
                m_routes_cfg.push_back(r);
                continue;
            }
            if (f.size() != 3)
                throw ConfigError("Xbar " + name + ": routes entry '" + e + "' must be link:base:size or link:interleave:<shift>:<count>:<index>");
            r.base = parseU64(f[1], "Xbar " + name + " routes base");
            r.size = parseU64(f[2], "Xbar " + name + " routes size");
            if (r.size == 0) throw ConfigError("Xbar " + name + ": routes entry '" + e + "' has size 0");
            m_routes_cfg.push_back(r);
        }
    }

    void Xbar::build() const
    {
        if (m_built) return;
        m_up = upLinks();
        m_down = downLinks();
        if (m_up.empty() || m_down.empty())
            throw ConfigError("Xbar " + m_name + ": needs at least one upstream link (as slave) and one downstream link (as master), has " +
                              std::to_string(m_up.size()) + " and " + std::to_string(m_down.size()));
        // One width and one level on every port: the crossbar forwards beats unchanged
        // (a WidthWidget converts widths, plan §3.5).
        const Link &ref = *m_up[0];
        std::vector<Link *> all = m_up;
        all.insert(all.end(), m_down.begin(), m_down.end());
        for (auto *l : all)
            if (l->params().w != ref.params().w || l->level() != ref.level())
                throw ConfigError("Xbar " + m_name + ": every link must have the width and level of " + ref.name() + " (" +
                                  ref.params().describe() + "); " + l->name() + " is " + l->params().describe());
        auto downIndex = [&](const std::string &n) -> int
        {
            for (size_t j = 0; j < m_down.size(); j++) if (m_down[j]->name() == n) return int(j);
            return -1;
        };
        // §6.3: the address map, each range to exactly one slave.
        m_routes = m_routes_cfg;
        for (auto &r : m_routes)
        {
            r.out = downIndex(r.link);
            if (r.out < 0) throw ConfigError("Xbar " + m_name + ": routes names " + r.link + ", which is not a downstream link of this crossbar");
        }
        // The interleave form: one (shift, count) for the whole map and one link per index, so
        // that every address has exactly one slot; never mixed with ranges (an overlap between
        // a range and a residue class could not be ruled out here).
        unsigned inter = 0;
        for (auto &r : m_routes) if (r.interleave) inter++;
        if (inter > 0 && inter != m_routes.size())
            throw ConfigError("Xbar " + m_name + ": routes mixes link:base:size and link:interleave entries (§6.3: every address routes to exactly one slave)");
        for (size_t x = 0; inter > 0 && x < m_routes.size(); x++)
            for (size_t y = x + 1; y < m_routes.size(); y++)
            {
                const Route &a = m_routes[x], &b = m_routes[y];
                if (a.shift != b.shift || a.count != b.count)
                    throw ConfigError("Xbar " + m_name + ": interleave routes " + a.link + " and " + b.link + " differ in shift or count");
                if (a.index == b.index)
                    throw ConfigError("Xbar " + m_name + ": interleave routes " + a.link + " and " + b.link + " take the same index " +
                                      std::to_string(a.index) + " (§6.3: every address routes to exactly one slave)");
            }
        for (size_t x = 0; inter == 0 && x < m_routes.size(); x++)
            for (size_t y = x + 1; y < m_routes.size(); y++)
            {
                const Route &a = m_routes[x], &b = m_routes[y];
                if (a.base < b.base + b.size && b.base < a.base + a.size)
                {
                    std::ostringstream o;
                    o << "Xbar " << m_name << ": routes overlap: " << a.link << " 0x" << std::hex << a.base << "+0x" << a.size
                      << " and " << b.link << " 0x" << b.base << "+0x" << b.size << " (§6.3: every address routes to exactly one slave)";
                    throw ConfigError(o.str());
                }
            }
        m_default = -1;
        if (!m_default_name.empty())
        {
            m_default = downIndex(m_default_name);
            if (m_default < 0) throw ConfigError("Xbar " + m_name + ": default names " + m_default_name + ", which is not a downstream link of this crossbar");
        }
        if (m_routes.empty() && m_default < 0 && m_down.size() > 1)
            throw ConfigError("Xbar " + m_name + ": more than one downstream link but no routes(vs) and no default(s)");
        if (m_routes.empty() && m_default < 0) m_default = 0;   // one slave: everything goes there
        // §6.4: one source namespace toward the slaves, one sink namespace toward the masters.
        m_src_base.assign(m_up.size(), 0);
        uint64_t total_src = 0;
        for (size_t m = 0; m < m_up.size(); m++) { m_src_base[m] = total_src; total_src += m_up[m]->params().numSources(); }
        for (auto *d : m_down)
            if (total_src > d->params().numSources())
                throw ConfigError("Xbar " + m_name + ": the masters' sources need " + std::to_string(total_src) + " ids in one namespace but downstream link " +
                                  d->name() + " has o=" + std::to_string(d->params().o) + " (" + std::to_string(d->params().numSources()) + " ids) (§6.4)");
        m_sink_base.assign(m_down.size(), 0);
        if (ref.level() == Level::C)
        {
            uint64_t total_sink = 0;
            for (size_t j = 0; j < m_down.size(); j++) { m_sink_base[j] = total_sink; total_sink += m_down[j]->params().numSinks(); }
            for (auto *u : m_up)
                if (total_sink > u->params().numSinks())
                    throw ConfigError("Xbar " + m_name + ": the slaves' sinks need " + std::to_string(total_sink) + " ids in one namespace but upstream link " +
                                      u->name() + " has i=" + std::to_string(u->params().i) + " (" + std::to_string(u->params().numSinks()) + " ids) (§6.4)");
        }
        m_built = true;
    }

    void Xbar::validate() const { build(); }

    uint64_t Xbar::sourceBase(const Link &up) const
    {
        build();
        for (size_t m = 0; m < m_up.size(); m++) if (m_up[m] == &up) return m_src_base[m];
        fatal("Xbar " + m_name + ": " + up.name() + " is not an upstream link");
    }
    uint64_t Xbar::sinkBase(const Link &down) const
    {
        build();
        for (size_t j = 0; j < m_down.size(); j++) if (m_down[j] == &down) return m_sink_base[j];
        fatal("Xbar " + m_name + ": " + down.name() + " is not a downstream link");
    }
    int Xbar::routeIndex(uint64_t address) const
    {
        for (auto &r : m_routes) if (r.covers(address, m_lg_block)) return r.out;
        return m_default;
    }
    Link *Xbar::routeOf(uint64_t address) const
    {
        build();
        int j = routeIndex(address);
        return j >= 0 ? m_down[size_t(j)] : nullptr;
    }

    void Xbar::reset(uint64_t)
    {
        build();
        m_planes.clear();
        for (int c = 0; c < NUM_CHANNELS; c++)
        {
            Channel ch = static_cast<Channel>(c);
            if (!m_up[0]->has(ch)) continue;
            Plane p;
            p.ch = ch;
            // §4: A, C, E are sent by masters (our inputs are the upstream links, our outputs
            // the downstream ones); B, D by slaves (the reverse).
            const std::vector<Link *> &ins = masterSends(ch) ? m_up : m_down;
            const std::vector<Link *> &outs = masterSends(ch) ? m_down : m_up;
            for (auto *l : ins) { In i; i.ch = &l->chan(ch); p.in.push_back(i); }
            for (auto *l : outs) { Out o; o.ch = &l->chan(ch); p.out.push_back(o); }
            m_planes.push_back(p);
        }
    }

    int Xbar::decode(const Plane &p, const Msg &m, int in) const
    {
        switch (p.ch)
        {
            case Channel::A: case Channel::C:
            {
                // Table 14: A and C route by address (§6.3: exactly one slave per address).
                return routeIndex(m.address);
            }
            case Channel::B: case Channel::D:
            {
                // Table 14: B and D route by source; the source is in the downstream namespace,
                // the base tells which master owns it (§6.4).
                for (size_t k = 0; k < m_up.size(); k++)
                    if (m.source >= m_src_base[k] && m.source - m_src_base[k] < m_up[k]->params().numSources()) return int(k);
                return -1;
            }
            case Channel::E:
            {
                // Table 14: E routes by sink, in the upstream namespace (§6.4).
                for (size_t j = 0; j < m_down.size(); j++)
                    if (m.sink >= m_sink_base[j] && m.sink - m_sink_base[j] < m_down[j]->params().numSinks()) return int(j);
                return -1;
            }
        }
        (void)in;
        return -1;
    }

    MsgPtr Xbar::remap(const Plane &p, const Msg &m, int in, int out) const
    {
        MsgPtr c = cloneMsg(m, m.w);
        switch (p.ch)
        {
            case Channel::A: case Channel::C:
                c->source = m.source + m_src_base[size_t(in)];          // into the downstream namespace (§6.4)
                break;
            case Channel::B:
                c->source = m.source - m_src_base[size_t(out)];         // back into the master's own namespace
                break;
            case Channel::D:
                c->source = m.source - m_src_base[size_t(out)];
                // §6.4: d_sink identifies an unanswered Grant; other D messages carry an
                // arbitrary sink that is ignored, left as is.
                if (m.opcode == unsigned(OpD::Grant) || m.opcode == unsigned(OpD::GrantData)) c->sink = m.sink + m_sink_base[size_t(in)];
                break;
            case Channel::E:
                c->sink = m.sink - m_sink_base[size_t(out)];            // into the slave's own namespace
                break;
        }
        return c;
    }

    void Xbar::offer(uint64_t)
    {
        for (auto &p : m_planes)
            for (auto &o : p.out)
                if (!o.q.empty()) o.ch->offer(o.q.front().msg, o.q.front().index);
    }

    int Xbar::pick(const Plane &p, Out &o, int j, uint64_t cycle) const
    {
        // The candidates: inputs offering a beat 0 that decodes to output j. An input with a
        // burst in progress is already granted somewhere (its control fields are constant,
        // §4.1, so it decodes to the same output), hence never a candidate here.
        int n = int(p.in.size());
        auto candidate = [&](int i) -> bool
        {
            const In &in = p.in[size_t(i)];
            if (!in.ch->valid() || in.ch->offered().index != 0 || in.ch->inProgress()) return false;
            return decode(p, *in.ch->offered().msg, i) == j;
        };
        switch (m_policy)
        {
            case Policy::Fixed:
                for (int i = 0; i < n; i++) if (candidate(i)) return i;
                return -1;
            case Policy::RoundRobin:
                for (int k = 1; k <= n; k++) { int i = (o.rr + k) % n; if (candidate(i)) return i; }
                return -1;
            case Policy::TDM:
            {
                // Plan §9: TDM in the crossbar. Slot s belongs to input s mod n; only the owner
                // may start a message in its slot (a burst already granted continues).
                int owner = int((cycle / m_slot) % uint64_t(n));
                return candidate(owner) ? owner : -1;
            }
        }
        return -1;
    }

    void Xbar::accept(uint64_t cycle)
    {
        for (auto &p : m_planes)
        {
            for (size_t j = 0; j < p.out.size(); j++)
            {
                Out &o = p.out[j];
                // Ready from this output's own stage at the start of the cycle (plan §3.2), on
                // this plane only: D's acceptance never depends on A's stage (§4.1).
                if (o.q.size() >= m_depth) continue;
                if (o.grant >= 0)
                {
                    // §4.1: the output is held for the burst in progress; only its next beat
                    // (the input's burst lock guarantees it is that message) is accepted.
                    In &in = p.in[size_t(o.grant)];
                    if (in.ch->valid()) in.ch->setReady(true);
                    continue;
                }
                int i = pick(p, o, int(j), cycle);
                if (i >= 0) p.in[size_t(i)].ch->setReady(true);
            }
            // Ready may depend on the offered beat (§4.1 allows it): an input whose beat
            // decodes nowhere is rejected and reported, never accepted and dropped.
            for (size_t i = 0; i < p.in.size(); i++)
            {
                In &in = p.in[i];
                if (in.ch->valid() && !in.ch->ready() && in.ch->offered().index == 0 && !in.ch->inProgress() &&
                    decode(p, *in.ch->offered().msg, int(i)) < 0)
                    fatal("Xbar " + m_name + " cycle " + std::to_string(cycle) + ": no route for " + in.ch->offered().msg->describe() +
                          " offered on " + in.ch->name() + " (A/C by address, B/D by source, E by sink: Table 14)");
            }
        }
    }

    void Xbar::commit(uint64_t)
    {
        for (auto &p : m_planes)
        {
            for (auto &o : p.out) if (o.ch->accepted().msg) o.q.pop_front();   // the front beat left
            for (size_t i = 0; i < p.in.size(); i++)
            {
                In &in = p.in[i];
                const Beat &b = in.ch->accepted();
                if (!b.msg) continue;
                if (b.index == 0)
                {
                    in.out = decode(p, *b.msg, int(i));
                    in.clone = remap(p, *b.msg, int(i), in.out);
                    Out &o = p.out[size_t(in.out)];
                    o.rr = int(i);
                    if (!b.last()) o.grant = int(i);              // hold the output for the burst (§4.1)
                }
                copyBeat(*in.clone, b.index, *b.msg, b.index);   // this beat's lanes, as the wire carried them
                Out &o = p.out[size_t(in.out)];
                o.q.push_back(Beat{in.clone, b.index});
                if (b.last()) { o.grant = -1; in.clone = nullptr; in.out = -1; }
            }
        }
    }

    bool Xbar::idle() const
    {
        for (auto &p : m_planes)
        {
            for (auto &o : p.out) if (!o.q.empty()) return false;
            for (auto &i : p.in) if (i.clone) return false;
        }
        return true;
    }

    ProgressHints Xbar::hints() const
    {
        // Plan §3.6: one cycle through plus the arbitration wait. Round-robin and TDM serve
        // every other input once before a waiting one, each for at most a largest burst
        // (beats of the largest encodable size); TDM adds up to a full slot rotation. Fixed
        // priority has no bound for the lowest input; the round-robin figure is declared.
        ProgressHints h;
        build();
        uint64_t n = std::max(m_up.size(), m_down.size());
        const LinkParams &p = m_up[0]->params();
        uint64_t burst = p.beatsOf(true, p.maxSize());
        h.latency = 1 + (n > 0 ? n - 1 : 0) * burst + (m_policy == Policy::TDM ? n * m_slot : 0);
        h.queue_depth = m_depth;
        return h;
    }
}}
