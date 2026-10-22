/*
 * File  :      FuzzMaster.cpp
 * Random legal TL-UL/UH traffic (plan §3.5). See FuzzMaster.h.
 */
#include "tilelink/agents/FuzzMaster.h"
#include "tilelink/Kernel.h"
#include "tilelink/Monitor.h"
#include <algorithm>

namespace octopus { namespace tl
{
    FuzzMaster::FuzzMaster(const std::string &name, const Config &p) : Agent(name)
    {
        if (p.has("level"))
        {
            std::string lv = p.get<std::string>("level");
            if (!parseLevel(lv, m_level) || m_level == Level::C) throw ConfigError(name + ": level must be UL or UH, got " + lv);
            m_level_set = true;
        }
        auto nonneg = [&](const char *k, int def) { int v = p.get<int>(k, def); if (v < 0) throw ConfigError(name + ": " + k + " must be >= 0"); return unsigned(v); };
        m_ops = nonneg("ops", 1000);
        m_w_get = nonneg("w_get", 4); m_w_put = nonneg("w_put", 3); m_w_pp = nonneg("w_putpartial", 1);
        m_w_arith = nonneg("w_arith", 1); m_w_logic = nonneg("w_logic", 1); m_w_intent = nonneg("w_intent", 1);
        if (p.has("addr_span"))
        {
            m_addr_set = true;
            m_addr_base = uint64_t(unsigned(p.get<int>("addr_base", 0)));
            m_addr_span = uint64_t(unsigned(p.get<int>("addr_span")));
        }
        m_align = std::max(1u, nonneg("align", 1));
        m_size_min = int(nonneg("size_min", 0));
        m_size_max = p.get<int>("size_max", -1);
        m_atomic_max = int(nonneg("atomic_size_max", 3));
        m_sources_param = nonneg("sources", 0);
        m_rate = std::min(100u, nonneg("rate", 70));
        m_fence = nonneg("fence_every", 0);
        m_retract = std::min(100u, nonneg("retract_pct", 0));
        m_dready = std::min(100u, nonneg("d_ready_pct", 100));
    }

    Link *FuzzMaster::link() const
    {
        for (auto &lr : m_links) if (lr.role == Role::Master) return lr.link;
        return nullptr;
    }

    ProgressHints FuzzMaster::hints() const
    {
        ProgressHints h;
        h.latency = 1;
        h.queue_depth = 1;
        h.is_master = true;   // an originator of requests
        return h;
    }

    void FuzzMaster::reset(uint64_t)
    {
        Link *l = link();
        if (!l) return;
        const LinkParams &lp = l->params();
        m_traffic = m_level_set && int(m_level) < int(lp.level) ? m_level : (lp.level == Level::C ? Level::UH : lp.level);
        // The slave's declared limits (catalogue §0.1): never exceed max_size (M-ALIGN-3), stay in its region (M-ALIGN-5).
        int slave_max = -1;
        std::vector<AddrRegion> regions;
        if (const SlaveLimits *sl = dynamic_cast<const SlaveLimits *>(l->slave())) { slave_max = sl->declaredMaxSize(*l); regions = sl->declaredRegions(*l); }
        if (!m_addr_set)
        {
            if (!regions.empty()) { m_addr_base = regions[0].base; m_addr_span = regions[0].size; }
            else { m_addr_base = 0; m_addr_span = 4096; }
        }
        unsigned lgw = 0;
        while ((1u << lgw) < lp.w) lgw++;
        int cap = m_traffic == Level::UL ? int(lgw) : std::min<int>(int(lp.maxSize()), 6);   // M-LEVEL-5: TL-UL 2^size <= w
        if (m_traffic != Level::UL && slave_max >= 0) cap = std::min(cap, slave_max);
        if (m_traffic == Level::UL && slave_max >= 0) cap = std::min(cap, slave_max);
        if (m_size_max < 0 || m_size_max > cap) m_size_max = cap;
        while (m_size_max > 0 && (uint64_t(1) << m_size_max) > m_addr_span) m_size_max--;
        if (m_size_min > m_size_max) m_size_min = m_size_max;
        if (m_atomic_max > m_size_max) m_atomic_max = m_size_max;
        uint64_t pool = lp.o >= 10 ? 1024 : (uint64_t(1) << lp.o);
        if (m_sources_param && m_sources_param < pool) pool = m_sources_param;
        m_free.clear();
        for (uint64_t s = pool; s-- > 0;) m_free.push_back(s);
    }

    unsigned FuzzMaster::pickOpcode()
    {
        const bool uh = m_traffic != Level::UL;   // M-LEVEL-1: atomics and Intent from TL-UH up
        unsigned w[6] = {m_w_get, m_w_put, m_w_pp, uh ? m_w_arith : 0, uh ? m_w_logic : 0, uh ? m_w_intent : 0};
        const unsigned ops[6] = {unsigned(OpA::Get), unsigned(OpA::PutFullData), unsigned(OpA::PutPartialData),
                                 unsigned(OpA::ArithmeticData), unsigned(OpA::LogicalData), unsigned(OpA::Intent)};
        unsigned total = 0;
        for (unsigned x : w) total += x;
        if (total == 0) return unsigned(OpA::Get);
        unsigned r = unsigned(rng()() % total);
        for (int i = 0; i < 6; i++) { if (r < w[i]) return ops[i]; r -= w[i]; }
        return unsigned(OpA::Get);
    }

    MsgPtr FuzzMaster::generate()
    {
        Link *l = link();
        const unsigned w = l->params().w;
        unsigned op = pickOpcode();
        const bool atomic = op == unsigned(OpA::ArithmeticData) || op == unsigned(OpA::LogicalData);
        int hi = atomic ? m_atomic_max : m_size_max;
        int lo = std::min(m_size_min, hi);
        unsigned size = unsigned(lo + int(rng()() % uint64_t(hi - lo + 1)));
        // M-ALIGN-1, §4.5: aligned to the size; inside [addr_base, addr_base + addr_span).
        uint64_t align = std::max<uint64_t>(uint64_t(1) << size, m_align);
        uint64_t slots = std::max<uint64_t>(1, m_addr_span / align);
        uint64_t first = (m_addr_base + align - 1) / align * align;
        uint64_t address = first + (rng()() % slots) * align;
        if (address + (uint64_t(1) << size) > m_addr_base + m_addr_span) address = first;

        MsgPtr m = Msg::make(Channel::A, op, size, w, kernel()->options().data_mode);
        m->address = address;
        m->deriveMask();                                 // §4.5, M-MASK-1/2
        size_t si = size_t(rng()() % m_free.size());
        m->source = m_free[si];                          // M-ID-1: unique among the outstanding
        m_free.erase(m_free.begin() + long(si));
        m->id = kernel()->newMsgId();
        m->from_agent = id();
        if (op == unsigned(OpA::ArithmeticData)) m->param = uint8_t(rng()() % 5);   // Table 23
        else if (op == unsigned(OpA::LogicalData)) m->param = uint8_t(rng()() % 4);   // Table 25
        else if (op == unsigned(OpA::Intent)) { static const uint8_t ip[5] = {0, 1, 5, 6, 7}; m->param = ip[rng()() % 5]; }   // Table 27
        if (op == unsigned(OpA::PutPartialData))
            for (auto &b : m->mask) if (b && (rng()() & 1)) b = 0;   // M-MASK-3: any subset of the active lanes
        if (m->hasData())
        {
            if (!m->data.empty()) for (auto &b : m->data) b = uint8_t(rng()());
            for (auto &s : m->stamp) s = ++m_stamp;      // plan §3.3: a write counter
        }
        return m;
    }

    void FuzzMaster::releaseSource(uint64_t s) { m_free.push_back(s); }

    void FuzzMaster::offer(uint64_t)
    {
        Link *l = link();
        if (!l || kernel()->inReset()) return;           // §3.2.2, M-RESET-1
        if (!m_cur)
        {
            if (m_issued >= m_ops || m_free.empty()) return;
            if (m_fence && m_issued > 0 && m_issued % m_fence == 0 && m_outstanding > 0) return;   // a fence (§6.5)
            if (rng()() % 100 >= m_rate) return;
            m_cur = generate();
            m_beat = 0;
            m_started = false;
        }
        else if (!m_started && m_retract && rng()() % 100 < m_retract)
        {
            // §4.1: before its first beat is accepted a message may be retracted or replaced.
            if (rng()() & 1) { m_retractions++; return; }
            releaseSource(m_cur->source);
            m_cur = generate();
            m_beat = 0;
            m_replacements++;
        }
        l->chan(Channel::A).offer(m_cur, m_beat);
    }

    void FuzzMaster::accept(uint64_t)
    {
        Link *l = link();
        if (!l) return;
        l->chan(Channel::D).setReady(m_dready >= 100 || rng()() % 100 < m_dready);
    }

    void FuzzMaster::commit(uint64_t)
    {
        Link *l = link();
        if (!l) return;
        const Beat &a = l->chan(Channel::A).accepted();
        if (a.msg && a.msg == m_cur)
        {
            if (a.index == 0) { m_started = true; m_outstanding++; }
            if (a.last()) { m_issued++; m_by_op[m_cur->opcode & 7]++; m_cur = nullptr; m_beat = 0; m_started = false; }
            else m_beat = a.index + 1;
        }
        const Beat &d = l->chan(Channel::D).accepted();
        if (d.msg)
        {
            const Msg &r = *d.msg;
            if (r.denied && r.hasData()) { m_denied_beats++; if (r.beatCorrupt(d.index)) m_denied_corrupt++; }
            if (d.last())
            {
                m_answered++;
                if (r.denied) m_denied++;
                if (m_outstanding) m_outstanding--;
                releaseSource(r.source);                 // reusable from the next cycle (catalogue §0.3)
            }
        }
        if (!kernel()->inReset()) kernel()->stats().recordOutstanding(l->name(), "A", unsigned(m_outstanding));
    }
}}
