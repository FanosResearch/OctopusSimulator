/*
 * File  :      RamSlave.cpp
 * The TL-UL/UH memory slave of plan §3.5. See RamSlave.h for the behaviour and parameters.
 */
#include "tilelink/Atomic.h"
#include "tilelink/agents/RamSlave.h"
#include "tilelink/Kernel.h"
#include <algorithm>

namespace octopus { namespace tl
{
    static std::vector<AddrRegion> pairsToRegions(const std::vector<int> &v, const std::string &who)
    {
        if (v.size() % 2) throw ConfigError(who + ": deny(vi) needs base,size pairs");
        std::vector<AddrRegion> r;
        for (size_t i = 0; i + 1 < v.size(); i += 2) r.push_back(AddrRegion{uint64_t(unsigned(v[i])), uint64_t(unsigned(v[i + 1]))});
        return r;
    }

    RamSlave::RamSlave(const std::string &name, const Config &p) : Agent(name)
    {
        latency        = unsigned(std::max(1, p.get<int>("latency", 1)));
        latency_jitter = unsigned(std::max(0, p.get<int>("latency_jitter", 0)));
        queue_depth    = unsigned(std::max(1, p.get<int>("queue_depth", 8)));
        fifo           = p.get<int>("fifo", 0) != 0;
        refresh_period = unsigned(std::max(0, p.get<int>("refresh_period", 0)));
        refresh_cycles = unsigned(std::max(0, p.get<int>("refresh_cycles", 0)));
        max_size       = p.get<int>("max_size", -1);
        for (int l : p.get<std::vector<int>>("latencies", {})) latencies.push_back(unsigned(std::max(1, l)));
        deny = pairsToRegions(p.get<std::vector<int>>("deny", {}), name);
        if (p.has("addr_size"))
            region = AddrRegion{uint64_t(unsigned(p.get<int>("addr_base", 0))), uint64_t(unsigned(p.get<int>("addr_size")))};
        std::string lv = p.get<std::string>("level", "UH");
        if (!parseLevel(lv, m_level) || m_level == Level::C) throw ConfigError(name + ": level must be UL or UH, got " + lv);
        m_cur = m_in = m_queue.end();
    }

    Link *RamSlave::link() const
    {
        for (auto &lr : m_links) if (lr.role == Role::Slave) return lr.link;
        return nullptr;
    }

    int RamSlave::declaredMaxSize(const Link &) const { return max_size; }
    std::vector<AddrRegion> RamSlave::declaredRegions(const Link &) const
    {
        if (region.size == 0) return {};
        return {region};
    }

    ProgressHints RamSlave::hints() const
    {
        ProgressHints h;
        uint64_t lat = latency;
        for (unsigned l : latencies) lat = std::max<uint64_t>(lat, l);
        h.latency = lat + latency_jitter + refresh_cycles;
        uint64_t beats = 1;
        if (Link *l = link())
        {
            unsigned ms = max_size >= 0 ? unsigned(max_size) : l->params().maxSize();
            beats = l->params().beatsOf(true, ms);
        }
        h.queue_depth = uint64_t(queue_depth) * beats;
        h.is_master = false;
        return h;
    }

    bool RamSlave::refreshing(uint64_t cycle) const
    {
        // §5.2 example 2 (waiting for a refresh, conforming): ready LOW and no response for
        // refresh_cycles of every refresh_period.
        return refresh_period > 0 && refresh_cycles > 0 && (cycle % refresh_period) < refresh_cycles;
    }

    bool RamSlave::isDenied(uint64_t address, unsigned size) const
    {
        if (deny_all) return true;
        uint64_t end = address + (uint64_t(1) << size);
        for (auto &r : deny)
            if (address < r.base + r.size && r.base < end) return true;
        return false;
    }

    // ------------------------------------------------------------------ the memory
    uint8_t RamSlave::readByte(uint64_t x) const
    {
        auto it = m_pages.find(x >> 12);
        return it == m_pages.end() ? 0 : it->second[size_t(x & 0xfff)];
    }
    void RamSlave::writeByte(uint64_t x, uint8_t v)
    {
        auto &pg = m_pages[x >> 12];
        if (pg.empty()) pg.assign(4096, 0);
        pg[size_t(x & 0xfff)] = v;
    }
    uint64_t RamSlave::readWord(uint64_t address, unsigned bytes) const
    {
        uint64_t v = 0;
        for (unsigned i = 0; i < bytes && i < 8; i++) v |= uint64_t(readByte(address + i)) << (8 * i);
        return v;
    }
    uint64_t RamSlave::readStamp(uint64_t address) const
    {
        auto it = m_stamps.find(address >> 3);
        return it == m_stamps.end() ? 0 : it->second;
    }
    void RamSlave::readBytes(uint64_t address, uint8_t *out, size_t n) const
    {
        for (size_t i = 0; i < n; i++) out[i] = readByte(address + i);
    }

    // the atomics (plan §1.3, Tables 23 and 25): tl::atomicOp in tilelink/Atomic.h, shared with ValueCheck

    // ------------------------------------------------------------------ execution
    void RamSlave::execute(Pending &p, uint64_t cycle)
    {
        const Msg &r = *p.req;
        Link *l = link();
        const unsigned w = l->params().w;
        // Plan §3.2: the response is offered `latency` (>= 1) cycles after the last accepted beat.
        uint64_t lat = p.order < latencies.size() ? latencies[size_t(p.order)] : latency;
        if (latency_jitter) lat += rng()() % (uint64_t(latency_jitter) + 1);
        p.ready_at = cycle + std::max<uint64_t>(1, lat);

        unsigned op = r.opcode;
        unsigned dop = unsigned(OpD::AccessAckData);
        if (op == unsigned(OpA::PutFullData) || op == unsigned(OpA::PutPartialData)) dop = unsigned(OpD::AccessAck);
        else if (op == unsigned(OpA::Intent)) dop = unsigned(OpD::HintAck);   // §8.2.4
        MsgPtr d = Msg::make(Channel::D, dop, r.size, w, r.data_mode);
        d->source = r.source;          // §6.4: d_source copied from a_source
        d->address = r.address;        // D has no address field (Table 9): the operation's, for the transcript and checkers
        d->id = kernel()->newMsgId();
        d->from_agent = id();
        p.resp = d;

        if (isDenied(r.address, r.size))
        {
            // §4.4: a denied operation has no effect; a denied data response marks every beat corrupt.
            d->denied = true;
            if (d->hasData()) for (auto &c : d->corrupt) c = 1;
            m_denied++;
            return;
        }
        if (op == unsigned(OpA::Intent)) return;   // a hint may be ignored (§8); a CBO has nothing to do here

        const uint64_t bytes = uint64_t(1) << r.size;
        const uint64_t base = r.address & ~uint64_t(w - 1);   // §4.5: lane l of beat k is base + k*w + l
        const unsigned spb = r.stampsPerBeat();
        auto at = [&](uint64_t x, unsigned &beat, unsigned &lane) { uint64_t off = x - base; beat = unsigned(off / w); lane = unsigned(off % w); };
        auto stampIdx = [&](unsigned beat, unsigned lane) { return size_t(beat) * spb + (w >= 8 ? lane / 8 : 0); };
        const bool full = r.data_mode == DataMode::Full, stamp = r.data_mode == DataMode::Stamp;

        if (op == unsigned(OpA::Get))
        {
            for (uint64_t x = r.address; x < r.address + bytes; x++)
            {
                unsigned b, ln; at(x, b, ln);
                if (full) d->data[size_t(b) * w + ln] = readByte(x);
                else if (stamp) d->stamp[stampIdx(b, ln)] = readStamp(x);
                if (m_poison.count(x)) d->corrupt[b] = 1;   // §4.4: stored corrupt data is returned corrupt
            }
            return;
        }
        if (op == unsigned(OpA::PutFullData) || op == unsigned(OpA::PutPartialData))
        {
            for (uint64_t x = r.address; x < r.address + bytes; x++)
            {
                unsigned b, ln; at(x, b, ln);
                size_t i = size_t(b) * w + ln;
                if (!p.wmask[i]) continue;                         // PutPartialData: only the masked bytes
                if (full) writeByte(x, p.wdata[i]);
                else if (stamp) m_stamps[x >> 3] = p.wstamp[stampIdx(b, ln)];
                if (p.wcorrupt[b]) m_poison[x] = true; else m_poison.erase(x);
            }
            return;
        }
        // Atomics (§8.2.1, §8.2.2): read the old value, write the result, return the old value.
        const size_t nb = size_t(bytes);
        std::vector<uint8_t> old(nb), opnd(nb), res(nb);
        bool bad = false;
        for (uint64_t x = r.address; x < r.address + bytes; x++)
        {
            unsigned b, ln; at(x, b, ln);
            size_t i = size_t(b) * w + ln, k = size_t(x - r.address);
            old[k] = readByte(x);
            opnd[k] = full ? p.wdata[i] : 0;
            if (p.wcorrupt[b] || m_poison.count(x)) bad = true;
            if (full) d->data[i] = old[k];
            else if (stamp) d->stamp[stampIdx(b, ln)] = readStamp(x);
        }
        if (full)
        {
            atomicOp(op, r.param, old.data(), opnd.data(), res.data(), size_t(bytes));
            for (uint64_t x = r.address; x < r.address + bytes; x++) writeByte(x, res[size_t(x - r.address)]);
        }
        else if (stamp)
            for (uint64_t x = r.address; x < r.address + bytes; x++)
            {
                unsigned b, ln; at(x, b, ln);
                m_stamps[x >> 3] = p.wstamp[stampIdx(b, ln)];
            }
        if (bad)
        {
            for (auto &c : d->corrupt) c = 1;
            for (uint64_t x = r.address; x < r.address + bytes; x++) m_poison[x] = true;
        }
        else
            for (uint64_t x = r.address; x < r.address + bytes; x++) m_poison.erase(x);
    }

    // ------------------------------------------------------------------ the three phases
    void RamSlave::offer(uint64_t cycle)
    {
        Link *l = link();
        if (!l || kernel()->inReset() || refreshing(cycle)) return;
        if (!m_have_cur)
        {
            // Pick the next response: FIFO domain = request order (§6.3); otherwise the earliest
            // service time, ties in request order. Once offered it is never retracted.
            auto best = m_queue.end();
            for (auto it = m_queue.begin(); it != m_queue.end(); ++it)
            {
                bool ready = it->resp && it->ready_at <= cycle;
                if (fifo) { if (ready) best = it; break; }
                if (ready && (best == m_queue.end() || it->ready_at < best->ready_at)) best = it;
            }
            if (best == m_queue.end()) return;
            m_cur = best;
            m_have_cur = true;
        }
        l->chan(Channel::D).offer(m_cur->resp, m_cur->beat);
    }

    void RamSlave::accept(uint64_t cycle)
    {
        Link *l = link();
        if (!l) return;
        // Ready LOW when the queue is full (rule 3 holds: the queue drains through D) and during a
        // refresh; a request burst in progress is always accepted to its end.
        bool r = !kernel()->inReset() && !refreshing(cycle) && (m_have_in || m_queue.size() < queue_depth);
        l->chan(Channel::A).setReady(r);
    }

    void RamSlave::commit(uint64_t cycle)
    {
        Link *l = link();
        if (!l) return;
        const Beat &db = l->chan(Channel::D).accepted();
        if (db.msg && m_have_cur)
        {
            m_cur->beat = db.index + 1;
            if (db.last())
            {
                m_responses++;
                if (m_have_in && m_in == m_cur) m_have_in = false;
                m_queue.erase(m_cur);
                m_cur = m_queue.end();
                m_have_cur = false;
            }
        }
        const Beat &ab = l->chan(Channel::A).accepted();
        if (!ab.msg) return;
        const Msg &m = *ab.msg;
        const unsigned w = l->params().w;
        if (ab.index == 0)
        {
            Pending p;
            p.req = ab.msg;
            p.order = m_order++;
            unsigned n = m.beats();
            p.wmask.assign(size_t(n) * w, 0);
            p.wcorrupt.assign(n, 0);
            if (m.data_mode == DataMode::Full) p.wdata.assign(size_t(n) * w, 0);
            if (m.data_mode == DataMode::Stamp) p.wstamp.assign(size_t(n) * m.stampsPerBeat(), 0);
            m_queue.push_back(std::move(p));
            m_in = std::prev(m_queue.end());
            m_have_in = true;
            m_requests++;
        }
        if (!m_have_in) return;
        // §4.1: the data of a beat is what was on the wire when it was accepted.
        Pending &p = *m_in;
        unsigned k = ab.index;
        for (unsigned ln = 0; ln < w && size_t(k) * w + ln < m.mask.size(); ln++) p.wmask[size_t(k) * w + ln] = m.mask[size_t(k) * w + ln];
        if (!p.wdata.empty() && !m.data.empty()) std::copy_n(m.beatData(k), w, &p.wdata[size_t(k) * w]);
        if (!p.wstamp.empty() && !m.stamp.empty())
            for (unsigned i = 0; i < m.stampsPerBeat(); i++) p.wstamp[size_t(k) * m.stampsPerBeat() + i] = m.stamp[size_t(k) * m.stampsPerBeat() + i];
        p.wcorrupt[k] = m.beatCorrupt(k) ? 1 : 0;
        if (ab.last())
        {
            execute(p, cycle);
            m_have_in = false;
        }
    }
}}
