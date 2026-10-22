/*
 * File  :      Pipe.cpp
 * Shared helpers of the topology agents: cloning, lane copying by absolute address (§4.5),
 * configuration parsing, link sorting by role.
 */
#include "tilelink/agents/Pipe.h"
#include <cstdlib>

namespace octopus { namespace tl
{
    MsgPtr cloneMsg(const Msg &src, unsigned w)
    {
        MsgPtr m = std::make_shared<Msg>();
        static_cast<MsgHeader &>(*m) = src.header();
        m->id = src.id;
        m->from_agent = src.from_agent;
        m->t_first_offer = src.t_first_offer;
        m->t_first_accept = src.t_first_accept;
        m->t_last_accept = src.t_last_accept;
        m->layout(w, src.data_mode);
        return m;
    }

    static size_t stampIndex(const Msg &m, const LanePos &p)
    {
        // Plan §3.3: one stamp per 8 bytes; a bus narrower than 8 bytes has one stamp per beat.
        return size_t(p.beat) * m.stampsPerBeat() + (m.w >= 8 ? p.lane / 8 : 0);
    }

    uint64_t laneValue(const Msg &m, uint64_t geom, uint64_t abs_address)
    {
        LanePos p = lanePos(geom, m.w, abs_address);
        size_t i = size_t(p.beat) * m.w + p.lane;
        if (m.data_mode == DataMode::Full && i < m.data.size()) return m.data[i];
        if (m.data_mode == DataMode::Stamp && stampIndex(m, p) < m.stamp.size()) return m.stamp[stampIndex(m, p)];
        return 0;
    }

    void copyByte(Msg &dst, uint64_t dst_geom, const Msg &src, uint64_t src_geom, uint64_t x)
    {
        LanePos d = lanePos(dst_geom, dst.w, x), s = lanePos(src_geom, src.w, x);
        size_t di = size_t(d.beat) * dst.w + d.lane, si = size_t(s.beat) * src.w + s.lane;
        if (di < dst.mask.size() && si < src.mask.size()) dst.mask[di] = src.mask[si];
        if (di < dst.data.size() && si < src.data.size()) dst.data[di] = src.data[si];
        size_t dsi = stampIndex(dst, d), ssi = stampIndex(src, s);
        if (dsi < dst.stamp.size() && ssi < src.stamp.size()) dst.stamp[dsi] = src.stamp[ssi];
    }

    void copyBeat(Msg &dst, unsigned dst_beat, const Msg &src, unsigned src_beat)
    {
        unsigned w = src.w;
        for (unsigned l = 0; l < w; l++)
        {
            size_t di = size_t(dst_beat) * w + l, si = size_t(src_beat) * w + l;
            if (di < dst.mask.size() && si < src.mask.size()) dst.mask[di] = src.mask[si];
            if (!dst.data.empty() && !src.data.empty()) dst.data[di] = src.data[si];
        }
        if (!dst.stamp.empty() && !src.stamp.empty())
            for (unsigned g = 0; g < src.stampsPerBeat(); g++)
                dst.stamp[size_t(dst_beat) * dst.stampsPerBeat() + g] = src.stamp[size_t(src_beat) * src.stampsPerBeat() + g];
        if (dst_beat < dst.corrupt.size() && src_beat < src.corrupt.size()) dst.corrupt[dst_beat] = src.corrupt[src_beat];
    }

    bool beatRange(uint64_t geom, uint64_t bytes, unsigned w, unsigned k, uint64_t &first, uint64_t &last)
    {
        uint64_t lo = laneBase(geom, w) + uint64_t(k) * w, hi = lo + w - 1;   // the beat's lanes
        uint64_t a0 = geom, a1 = geom + bytes - 1;                            // the operation
        if (hi < a0 || lo > a1) return false;
        first = lo > a0 ? lo : a0;
        last = hi < a1 ? hi : a1;
        return true;
    }

    uint64_t parseU64(const std::string &text, const std::string &what)
    {
        const char *s = text.c_str();
        char *end = nullptr;
        unsigned long long v = std::strtoull(s, &end, 0);
        if (end == s || *end != '\0') throw ConfigError(what + ": bad number '" + text + "'");
        return uint64_t(v);
    }

    std::vector<Link *> TopologyAgent::upLinks() const
    {
        std::vector<Link *> v;
        for (auto &l : m_links) if (l.role == Role::Slave) v.push_back(l.link);
        return v;
    }
    std::vector<Link *> TopologyAgent::downLinks() const
    {
        std::vector<Link *> v;
        for (auto &l : m_links) if (l.role == Role::Master) v.push_back(l.link);
        return v;
    }

    void TopologyAgent::requireOneEach(const char *type, bool same_w) const
    {
        auto ups = upLinks(), downs = downLinks();
        if (ups.size() != 1 || downs.size() != 1)
            throw ConfigError(std::string(type) + " " + m_name + ": needs exactly one upstream link (as slave) and one downstream link (as master), has " +
                              std::to_string(ups.size()) + " and " + std::to_string(downs.size()));
        const Link &u = *ups[0], &d = *downs[0];
        if (u.level() != d.level())
            throw ConfigError(std::string(type) + " " + m_name + ": links " + u.name() + " (TL-" + levelName(u.level()) + ") and " +
                              d.name() + " (TL-" + levelName(d.level()) + ") must have the same level");
        if (same_w && u.params().w != d.params().w)
            throw ConfigError(std::string(type) + " " + m_name + ": links " + u.name() + " (w=" + std::to_string(u.params().w) + ") and " +
                              d.name() + " (w=" + std::to_string(d.params().w) + ") must have the same width (use a WidthWidget)");
    }
}}
