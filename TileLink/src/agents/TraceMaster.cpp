/*
 * File  :      TraceMaster.cpp
 * The Octopus trace reader and the uncached TraceMaster (plan §3.5). See TraceMaster.h.
 */
#include "tilelink/agents/TraceMaster.h"
#include "tilelink/Kernel.h"
#include <algorithm>
#include <cstdio>

namespace octopus { namespace tl
{
    // ------------------------------------------------------------------ TraceReader
    bool TraceReader::parseLine(const std::string &line, uint64_t &addr, bool &store, uint64_t &cycle)
    {
        // src/CPU.cpp CPU::readSampleFromWorkload: sscanf(line, "%llx %*d %c %lld", ...);
        // complementary_value = (type == 'R') ? READ : WRITE.
        unsigned long long a = 0;
        long long c = 0;
        char type = 'R';
        int n = std::sscanf(line.c_str(), "%llx %*d %c %lld", &a, &type, &c);
        if (n < 1) return false;
        addr = uint64_t(a);
        store = type != 'R';
        cycle = n >= 3 ? uint64_t(c) : 0;
        return true;
    }

    TraceReader::TraceReader(const std::string &path, unsigned access_bytes, bool trace_timing, uint64_t max_gap, uint64_t max_ops)
        : m_path(path), m_f(path), m_bytes(access_bytes), m_timing(trace_timing), m_max_gap(max_gap), m_max_ops(max_ops)
    {
        if (!m_f) throw ConfigError("trace " + path + ": cannot open the file");
        if (m_bytes == 0 || (m_bytes & (m_bytes - 1))) throw ConfigError("trace " + path + ": access_bytes must be a power of two");
        fetch();
    }

    void TraceReader::fetch()
    {
        m_have = false;
        if (m_eof) return;
        if (m_max_ops && m_lines >= m_max_ops) { m_eof = true; return; }
        std::string line;
        while (std::getline(m_f, line))
        {
            uint64_t a, c;
            bool st;
            if (!parseLine(line, a, st, c)) continue;   // a blank or unreadable line: CPU.cpp would read garbage; skipped
            m_op = CpuOp();
            m_op.kind = st ? CpuOp::Store : CpuOp::Load;
            m_op.bytes = m_bytes;
            m_op.addr = a & ~uint64_t(m_bytes - 1);     // naturally aligned (§4.5)
            m_prev_cycle = m_first ? c : m_cycle;
            m_cycle = c;
            m_first = false;
            m_have = true;
            m_lines++;
            return;
        }
        m_eof = true;
    }

    bool TraceReader::next(CpuOp &op, uint64_t cycle, uint64_t last_issue, std::mt19937_64 &)
    {
        if (!m_have) return false;
        op = m_op;
        op.not_before = cycle;
        if (m_timing)
        {
            // Octopus CPU: the compute time between two samples is the difference of their cycles.
            uint64_t gap = m_cycle >= m_prev_cycle ? m_cycle - m_prev_cycle : 0;
            op.not_before = std::max(cycle, last_issue + std::min(gap, m_max_gap));
        }
        fetch();
        return true;
    }

    // ------------------------------------------------------------------ TraceMaster (uncached)
    TraceMaster::TraceMaster(const std::string &name, const Config &p) : Agent(name)
    {
        std::string mode = p.get<std::string>("mode", "uncached");
        if (mode == "cpu")
            throw ConfigError(name + ": TraceMaster mode cpu is the CacheMaster's CPU side: give the CacheMaster trace(s) instead (TraceMaster.h)");
        if (mode != "uncached") throw ConfigError(name + ": mode must be uncached or cpu, got " + mode);
        std::string lv = p.get<std::string>("level", "UH");
        if (!parseLevel(lv, m_level) || m_level == Level::C) throw ConfigError(name + ": level must be UL or UH, got " + lv);
        int b = p.get<int>("access_bytes", 8);
        if (b <= 0 || (b & (b - 1))) throw ConfigError(name + ": access_bytes must be a power of two");
        m_bytes = unsigned(b);
        m_outstanding_max = unsigned(std::max(1, p.get<int>("outstanding", 1)));
        std::string timing = p.get<std::string>("timing", "asap");
        if (timing != "asap" && timing != "trace") throw ConfigError(name + ": timing must be asap or trace, got " + timing);
        if (!p.has("trace")) throw ConfigError(name + ": trace(s) is required");
        m_reader.reset(new TraceReader(p.get<std::string>("trace"), m_bytes, timing == "trace",
                                       uint64_t(std::max(0, p.get<int>("max_gap", 1000))), uint64_t(std::max(0, p.get<int>("max_ops", 0)))));
    }

    Link *TraceMaster::link() const
    {
        for (auto &lr : m_links) if (lr.role == Role::Master) return lr.link;
        return nullptr;
    }

    void TraceMaster::reset(uint64_t)
    {
        Link *l = link();
        if (!l) return;
        uint64_t pool = std::min<uint64_t>(l->params().o >= 10 ? 1024 : l->params().numSources(), m_outstanding_max);
        m_free.clear();
        for (uint64_t s = pool; s-- > 0;) m_free.push_back(s);
    }

    bool TraceMaster::idle() const { return m_reader->exhausted() && !m_have_next && !m_cur && m_outstanding == 0; }

    ProgressHints TraceMaster::hints() const
    {
        ProgressHints h;
        h.is_master = true;
        return h;
    }

    void TraceMaster::offer(uint64_t cycle)
    {
        Link *l = link();
        if (!l || kernel()->inReset()) return;
        if (!m_cur)
        {
            if (!m_have_next)
            {
                if (!m_reader->next(m_next, cycle, m_last_issue, rng())) return;
                m_have_next = true;
            }
            if (m_free.empty() || m_next.not_before > cycle) return;
            const CpuOp &op = m_next;
            const unsigned w = l->params().w;
            unsigned lg = 0;
            while ((1u << lg) < op.bytes) lg++;
            unsigned opc = op.kind == CpuOp::Store ? unsigned(OpA::PutFullData) : unsigned(OpA::Get);
            m_cur = Msg::make(Channel::A, opc, lg, w, kernel()->options().data_mode);
            m_cur->address = op.addr;
            m_cur->deriveMask();
            m_cur->source = m_free.back();
            m_free.pop_back();
            m_cur->id = kernel()->newMsgId();
            m_cur->from_agent = id();
            m_counter++;
            // Trace data carries no values (plan §3.3: the zero lines the CPU model produces); a
            // write counter instead, so a value checker can tell writes apart.
            for (auto &s : m_cur->stamp) s = (uint64_t(id() + 1) << 40) | m_counter;
            for (size_t i = 0; i < m_cur->data.size(); i++) m_cur->data[i] = uint8_t((m_counter >> (8 * (i % 8))) & 0xff);
            m_beat = 0;
            m_last_issue = cycle;
            m_have_next = false;
        }
        l->chan(Channel::A).offer(m_cur, m_beat);
    }

    void TraceMaster::accept(uint64_t)
    {
        Link *l = link();
        if (l) l->chan(Channel::D).setReady(!kernel()->inReset());
    }

    void TraceMaster::commit(uint64_t)
    {
        Link *l = link();
        if (!l) return;
        const Beat &a = l->chan(Channel::A).accepted();
        if (a.msg && a.msg == m_cur)
        {
            if (a.index == 0) m_outstanding++;
            if (a.last()) { m_issued++; m_cur = nullptr; m_beat = 0; }
            else m_beat = a.index + 1;
        }
        const Beat &d = l->chan(Channel::D).accepted();
        if (d.msg && d.last())
        {
            m_answered++;
            if (d.msg->denied) m_denied++;
            if (m_outstanding) m_outstanding--;
            m_free.push_back(d.msg->source);
        }
    }
}}
