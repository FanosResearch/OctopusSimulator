/*
 * File  :      TopoTest.h
 * Test agents for TL-TOPO (plan §6): a scripted endpoint (either side of a link: offers
 * scripted messages on the channels its side sends, with a valid schedule, and records what it
 * receives under a ready schedule) and a memory slave (TL-UL/UH: Get, PutFull/PartialData,
 * Arithmetic/LogicalData, Intent from a byte array, Full or Stamp data, configurable latency,
 * response order, denied ranges and corrupt injection). They are the tests' own, independent
 * of T2's agents.
 */
#ifndef _TL_TOPO_TEST_H
#define _TL_TOPO_TEST_H
#include "TestMain.h"
#include "tilelink/Kernel.h"
#include "tilelink/agents/Pipe.h"
#include <deque>
#include <functional>
#include <map>
#include <set>

namespace topo
{
    using namespace octopus::tl;

    // Builds a message on a bus of w bytes; data filled by fill(byte address) for a data
    // message (Full: the byte; Stamp: the stamp of the byte's 8-byte group).
    inline MsgPtr make(Channel ch, unsigned op, unsigned size, uint64_t addr, uint64_t source, unsigned w,
                       DataMode mode = DataMode::Full, std::function<uint64_t(uint64_t)> fill = nullptr)
    {
        MsgPtr m = Msg::make(ch, op, size, w, mode);
        m->address = addr; m->source = source;
        m->deriveMask();
        if (m->hasData() && fill)
        {
            for (uint64_t x = addr; x < addr + m->bytes(); x++)
            {
                LanePos p = lanePos(*m, x);
                if (mode == DataMode::Full) m->data[size_t(p.beat) * w + p.lane] = uint8_t(fill(x));
                else if (mode == DataMode::Stamp) m->stamp[size_t(p.beat) * m->stampsPerBeat() + (w >= 8 ? p.lane / 8 : 0)] = fill(x);
            }
        }
        return m;
    }
    inline uint64_t lowByte(uint64_t x) { return x & 0xff; }   // Figure 11's device: the byte is its address

    // Either end of one link.
    class Endpoint : public Agent
    {
    public:
        explicit Endpoint(const std::string &n) : Agent(n) {}
        Level capability(const Link &, Role) const override { return Level::C; }

        struct Item { MsgPtr m; uint64_t at; };
        struct Got { uint64_t cycle; Channel ch; MsgPtr m; unsigned beat; };
        std::deque<Item> send[NUM_CHANNELS];
        unsigned beat[NUM_CHANNELS] = {0, 0, 0, 0, 0};
        std::set<uint64_t> no_valid;                                   // cycles a NEW message is not offered
        std::function<bool(Channel, uint64_t)> ready = [](Channel, uint64_t) { return true; };
        std::vector<Got> got;
        std::vector<std::pair<uint64_t, Channel>> sent_beats;          // (cycle, channel) of accepted sends

        void push(MsgPtr m, uint64_t at = 0) { send[static_cast<int>(m->channel)].push_back({m, at}); }
        Link &link() const { return *m_links.at(0).link; }
        bool sends(Channel ch) const { return masterSends(ch) == (m_links.at(0).role == Role::Master); }

        void offer(uint64_t t) override
        {
            for (int c = 0; c < NUM_CHANNELS; c++)
            {
                Channel ch = static_cast<Channel>(c);
                if (!link().has(ch) || !sends(ch) || send[c].empty() || send[c].front().at > t) continue;
                if (beat[c] == 0 && no_valid.count(t)) continue;
                link().chan(ch).offer(send[c].front().m, beat[c]);
            }
        }
        void accept(uint64_t t) override
        {
            for (int c = 0; c < NUM_CHANNELS; c++)
            {
                Channel ch = static_cast<Channel>(c);
                if (link().has(ch) && !sends(ch)) link().chan(ch).setReady(ready(ch, t));
            }
        }
        void commit(uint64_t t) override
        {
            for (int c = 0; c < NUM_CHANNELS; c++)
            {
                Channel ch = static_cast<Channel>(c);
                if (!link().has(ch)) continue;
                const Beat &b = link().chan(ch).accepted();
                if (!b.msg) continue;
                if (sends(ch))
                {
                    sent_beats.push_back({t, ch});
                    if (b.last()) { send[c].pop_front(); beat[c] = 0; } else beat[c] = b.index + 1;
                }
                else got.push_back({t, ch, b.msg, b.index});
            }
        }
        bool idle() const override
        {
            for (auto &q : send) if (!q.empty()) return false;
            return true;
        }
        // Messages received on ch (one entry per message, at its last beat).
        std::vector<Got> messages(Channel ch) const
        {
            std::vector<Got> v;
            for (auto &g : got) if (g.ch == ch && g.beat + 1 == g.m->beats()) v.push_back(g);
            return v;
        }
        std::vector<Got> beats(Channel ch) const
        {
            std::vector<Got> v;
            for (auto &g : got) if (g.ch == ch) v.push_back(g);
            return v;
        }
    };

    // Plan §1.3 atomics, written independently of ValueCheck's.
    inline uint64_t atomicResult(unsigned opcode, unsigned param, uint64_t old, uint64_t operand, unsigned n)
    {
        uint64_t mask = n >= 8 ? ~uint64_t(0) : (uint64_t(1) << (8 * n)) - 1;
        auto s = [&](uint64_t v) { int64_t r = int64_t(v << (64 - 8 * n)) >> (64 - 8 * n); return r; };
        old &= mask; operand &= mask;
        if (opcode == unsigned(OpA::LogicalData))
            switch (param) { case 0: return old ^ operand; case 1: return old | operand; case 2: return old & operand; default: return operand; }
        switch (param)
        {
            case 0: return s(old) < s(operand) ? old : operand;
            case 1: return s(old) > s(operand) ? old : operand;
            case 2: return old < operand ? old : operand;
            case 3: return old > operand ? old : operand;
            default: return (old + operand) & mask;
        }
    }

    // A TL-UL/UH memory on one link (slave side).
    class MemSlave : public Agent
    {
    public:
        explicit MemSlave(const std::string &n) : Agent(n) {}
        Level capability(const Link &, Role) const override { return Level::UH; }

        uint64_t latency = 1;                         // cycles from the request's last beat to the response offer
        bool     lifo = false;                        // answer the newest pending request first (§6.5: any order)
        unsigned max_pending = 64;                    // A ready LOW beyond this
        std::vector<std::pair<uint64_t, uint64_t>> deny;   // [base, base+size): denied, no effect (§4.4)
        std::set<uint64_t> corrupt_at;                // a data response beat carrying this byte is corrupt
        std::function<bool(uint64_t)> a_ready = [](uint64_t) { return true; };
        std::function<void(Msg &)> mutate;            // a mutant slave: alters a response before it is sent
        std::map<uint64_t, uint8_t> mem;              // Full
        std::map<uint64_t, uint64_t> stamps;          // Stamp: by 8-byte group address
        std::vector<MsgPtr> requests;                 // accepted requests, in order
        std::vector<std::pair<uint64_t, uint64_t>> sources_seen;   // (cycle, source) at each request's first beat
        std::set<uint64_t> outstanding;               // sources awaiting their response
        uint64_t max_outstanding = 0;

        uint64_t byteAt(uint64_t x) const { auto it = mem.find(x); return it == mem.end() ? 0 : it->second; }

        void offer(uint64_t t) override
        {
            if (!cur)
            {
                int pick = -1;
                for (int i = 0; i < int(pending.size()); i++)
                    if (pending[size_t(i)].at <= t) { pick = i; if (!lifo) break; }
                if (pick < 0) return;
                cur = pending[size_t(pick)].resp;
                pending.erase(pending.begin() + pick);
                beat = 0;
            }
            link().chan(Channel::D).offer(cur, beat);
        }
        void accept(uint64_t t) override
        {
            link().chan(Channel::A).setReady(pending.size() < max_pending && a_ready(t));
        }
        void commit(uint64_t t) override
        {
            const Beat &d = link().chan(Channel::D).accepted();
            if (d.msg)
            {
                if (d.last()) { outstanding.erase(cur->source); cur = nullptr; } else beat = d.index + 1;
            }
            const Beat &a = link().chan(Channel::A).accepted();
            if (!a.msg) return;
            if (a.index == 0)
            {
                CHECK(!outstanding.count(a.msg->source));   // §6.4: unique while outstanding
                outstanding.insert(a.msg->source);
                if (outstanding.size() > max_outstanding) max_outstanding = outstanding.size();
                sources_seen.push_back({t, a.msg->source});
                requests.push_back(a.msg);
            }
            if (a.last()) execute(*a.msg, t);
        }
        bool idle() const override { return pending.empty() && !cur; }

    private:
        struct Pending { MsgPtr resp; uint64_t at; };
        std::vector<Pending> pending;
        MsgPtr cur;
        unsigned beat = 0;

        Link &link() const { return *m_links.at(0).link; }
        bool denied(const Msg &m) const
        {
            for (auto &r : deny) if (m.address < r.first + r.second && r.first < m.address + m.bytes()) return true;
            return false;
        }
        uint64_t readVal(const Msg &m, uint64_t x) const
        {
            if (m.data_mode == DataMode::Stamp) { auto it = stamps.find(x & ~uint64_t(7)); return it == stamps.end() ? 0 : it->second; }
            return byteAt(x);
        }
        void writeVal(const Msg &m, uint64_t x, uint64_t v)
        {
            if (m.data_mode == DataMode::Stamp) stamps[x & ~uint64_t(7)] = v; else mem[x] = uint8_t(v);
        }
        void execute(const Msg &m, uint64_t t)
        {
            unsigned w = m.w;
            unsigned op = m.opcode;
            unsigned dop = (op == 0 || op == 1) ? 0u : op == 5 ? 2u : 1u;
            MsgPtr r = Msg::make(Channel::D, dop, m.size, w, m.data_mode);
            r->source = m.source;
            r->address = m.address;   // simulator-only on D: the request's address (no wire field)
            bool den = denied(m);
            r->denied = den;
            auto put = [&](uint64_t x, uint64_t v)
            {
                LanePos p = lanePos(m.address, w, x);
                if (m.data_mode == DataMode::Full) r->data[size_t(p.beat) * w + p.lane] = uint8_t(v);
                else if (m.data_mode == DataMode::Stamp) r->stamp[size_t(p.beat) * r->stampsPerBeat() + (w >= 8 ? p.lane / 8 : 0)] = v;
            };
            auto masked = [&](uint64_t x) { LanePos p = lanePos(m, x); return m.mask[size_t(p.beat) * w + p.lane] != 0; };
            if (!den)
            {
                if (op == unsigned(OpA::Get))
                    for (uint64_t x = m.address; x < m.address + m.bytes(); x++) put(x, readVal(m, x));
                else if (op == unsigned(OpA::PutFullData) || op == unsigned(OpA::PutPartialData))
                {
                    for (uint64_t x = m.address; x < m.address + m.bytes(); x++) if (masked(x)) writeVal(m, x, laneValue(m, x));
                }
                else if (op == unsigned(OpA::ArithmeticData) || op == unsigned(OpA::LogicalData))
                {
                    if (m.data_mode == DataMode::Stamp)
                        for (uint64_t g = m.address; g < m.address + m.bytes(); g += 8)
                        {
                            uint64_t old = readVal(m, g);
                            put(g, old);
                            writeVal(m, g, atomicResult(op, m.param, old, laneValue(m, g), 8));
                        }
                    else
                    {
                        uint64_t n = m.bytes(), group = op == unsigned(OpA::LogicalData) ? 1 : (n < 8 ? n : 8);
                        for (uint64_t g = m.address; g < m.address + n; g += group)
                        {
                            uint64_t old = 0, operand = 0;
                            for (uint64_t k = 0; k < group; k++) { old |= byteAt(g + k) << (8 * k); operand |= laneValue(m, g + k) << (8 * k); }
                            uint64_t nv = atomicResult(op, m.param, old, operand, unsigned(group));
                            for (uint64_t k = 0; k < group; k++) { put(g + k, (old >> (8 * k)) & 0xff); mem[g + k] = uint8_t(nv >> (8 * k)); }
                        }
                    }
                }
            }
            if (r->hasData())
            {
                for (unsigned k = 0; k < r->beats(); k++)
                {
                    uint64_t f, l;
                    if (!beatRange(m.address, m.bytes(), w, k, f, l)) continue;
                    if (den) r->corrupt[k] = 1;   // §4.4: a denied data response is corrupt on every beat
                    for (uint64_t x = f; x <= l; x++) if (corrupt_at.count(x)) r->corrupt[k] = 1;
                }
            }
            if (mutate) mutate(*r);
            pending.push_back({r, t + latency});
        }
    };

    inline Endpoint *endpoint(Kernel &k, const std::string &n) { return static_cast<Endpoint *>(&k.add(std::unique_ptr<Agent>(new Endpoint(n)))); }
    inline MemSlave *memslave(Kernel &k, const std::string &n) { return static_cast<MemSlave *>(&k.add(std::unique_ptr<Agent>(new MemSlave(n)))); }
    template <typename T> T *addAgent(Kernel &k, T *a) { k.add(std::unique_ptr<Agent>(a)); return a; }
    inline LinkParams lp(unsigned w, Level lvl = Level::UH, unsigned o = 4, unsigned i = 0, unsigned z = 3)
    {
        LinkParams p; p.w = w; p.level = lvl; p.o = o; p.i = i; p.z = z;
        return p;
    }
    inline KernelOptions opts(uint64_t seed = 1) { return KernelOptions{0, seed, DataMode::Full}; }
    inline Config cfg(std::initializer_list<const char *> lines)
    {
        Config c;
        for (auto l : lines) c.set(l);
        return c;
    }
    // Expects fn to throw ConfigError whose message contains `what`.
    template <typename F> bool throwsConfig(F fn, const std::string &what)
    {
        try { fn(); }
        catch (ConfigError &e)
        {
            if (std::string(e.what()).find(what) == std::string::npos) { std::printf("    message was: %s\n", e.what()); return false; }
            return true;
        }
        return false;
    }
}
#endif /* _TL_TOPO_TEST_H */
