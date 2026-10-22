/*
 * File  :      FlowTest.h
 * The directed TL-C scenarios' rig (TL-FLOWS, TL-CMO): CacheMasters with a scripted CPU side, each on
 * its own link through a Gate (a stage per channel that, while the test keeps that channel closed, holds
 * the beats it accepted: it does not offer a message whose first beat has not gone out, which §4.1 allows,
 * and never stops a burst in progress) to a CoherentManager, a RamSlave (or a test
 * memory) behind it, optional uncached Endpoint masters; every executed cell (and every Stall, once per
 * parked message and state) is logged as "<agent> <from> + <event> -> <to>" for sequence assertions.
 */
#ifndef _TL_TEST_FLOWTEST_H
#define _TL_TEST_FLOWTEST_H
#include "TestMain.h"
#include "tilelink/CoherenceCheck.h"
#include "tilelink/Monitor.h"
#include "tilelink/agents/CacheMaster.h"
#include "tilelink/agents/CoherentManager.h"
#include "tilelink/agents/Pipe.h"
#include "tilelink/agents/RamSlave.h"
#include <cstdlib>
#include <functional>
#include <set>

namespace flow
{
    using namespace octopus::tl;

    inline Config cfg(const std::vector<std::string> &lines) { Config c; for (auto &l : lines) c.set(l); return c; }
    inline std::string table(const char *n) { return std::string(TL_TABLE_DIR) + "/" + n; }

    // One upstream, one downstream link; a BeatQueue per channel; while closed(ch, cycle) a message not
    // yet started downstream is held (the master's own link has accepted it: it is "in the network").
    class Gate : public TopologyAgent
    {
    public:
        explicit Gate(const std::string &n) : TopologyAgent(n) {}
        std::function<bool(Channel, uint64_t)> closed = [](Channel, uint64_t) { return false; };
        void validate() const override { requireOneEach("Gate", true); }
        void reset(uint64_t) override
        {
            for (int c = 0; c < NUM_CHANNELS; c++)
            {
                Channel ch = static_cast<Channel>(c);
                if (!up()->has(ch)) continue;
                m_q[c].out = &outChannel(ch); m_q[c].depth = 64; m_in[c] = &inChannel(ch);
            }
        }
        void offer(uint64_t t) override
        {
            for (int c = 0; c < NUM_CHANNELS; c++)
            {
                BeatQueue &q = m_q[c];
                if (!q.out || q.empty()) continue;
                if (closed(static_cast<Channel>(c), t) && q.q.front().index == 0 && !q.out->inProgress()) continue;
                q.offer();
            }
        }
        void accept(uint64_t) override
        {
            for (int c = 0; c < NUM_CHANNELS; c++)
                if (m_in[c]) m_in[c]->setReady(m_q[c].space());
        }
        void commit(uint64_t) override
        {
            for (int c = 0; c < NUM_CHANNELS; c++)
            {
                if (!m_in[c]) continue;
                m_q[c].commit();
                const Beat &b = m_in[c]->accepted();
                if (b.msg) m_q[c].push(b.msg, b.index);
            }
        }
        bool idle() const override { for (auto &q : m_q) if (!q.empty()) return false; return true; }
    private:
        BeatQueue m_q[NUM_CHANNELS];
        ChannelState *m_in[NUM_CHANNELS] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    };

    struct Rig
    {
        std::unique_ptr<Kernel> k;
        std::vector<CacheMaster *> cms;
        std::vector<Gate *> gates;
        CoherentManager *mgr = nullptr;
        Agent *mem = nullptr;
        Link *outer = nullptr;
        std::vector<std::unique_ptr<Monitor>> mons;
        std::unique_ptr<CoherenceCheck> cc;
        std::vector<std::string> log;                 // "<agent> <from> + <event> -> <to>"
        std::vector<coh::Transition> trans;
        std::vector<std::string> cviol;
        // closed windows: (gate index, channel) -> [from, to)
        std::vector<std::tuple<unsigned, Channel, uint64_t, uint64_t>> windows;

        ~Rig() { mons.clear(); k.reset(); }

        template <typename T> T *add(T *a) { k->add(std::unique_ptr<Agent>(a)); return a; }

        // n CacheMasters (gated) -> manager -> mem; `mem` defaults to a RamSlave (latency 2).
        void build(unsigned n, const std::string &protocol = "MESI", Agent *memory = nullptr, std::vector<Agent *> extra_masters = {},
                   std::vector<std::string> mgr_cfg = {})
        {
            k.reset(new Kernel(KernelOptions{100, 1, DataMode::Full}));
            LinkParams lc; lc.level = Level::C; lc.w = 8; lc.o = 3; lc.i = 3;
            LinkParams lu; lu.level = Level::UH; lu.w = 8; lu.o = 4;
            for (unsigned m = 0; m < n; m++)
            {
                cms.push_back(add(new CacheMaster("c" + std::to_string(m), cfg({"table(s)=" + table("TLC_MESI_L1.csv"), "cpu(s)=script",
                                                                                 "outstanding(i)=1", "sets(i)=4", "ways(i)=2"}))));
                gates.push_back(add(new Gate("g" + std::to_string(m))));
            }
            for (auto *x : extra_masters) k->add(std::unique_ptr<Agent>(x));
            std::vector<std::string> mc = {"table(s)=" + table(protocol == "MSI" ? "TLC_MSI_LLC.csv" : "TLC_MESI_LLC.csv")};
            for (auto &x : mgr_cfg) mc.push_back(x);
            mgr = add(new CoherentManager("llc", cfg(mc)));
            mem = memory ? memory : add(new RamSlave("mem", cfg({"latency(i)=2"})));
            if (memory) k->add(std::unique_ptr<Agent>(memory));
            std::vector<Link *> mon;
            for (unsigned m = 0; m < n; m++)
            {
                mon.push_back(&k->connect("L" + std::to_string(m), lc, *cms[m], *gates[m]));
                mon.push_back(&k->connect("G" + std::to_string(m), lc, *gates[m], *mgr));
                unsigned gi = m;
                gates[m]->closed = [this, gi](Channel ch, uint64_t t)
                {
                    for (auto &w : windows) if (std::get<0>(w) == gi && std::get<1>(w) == ch && t >= std::get<2>(w) && t < std::get<3>(w)) return true;
                    return false;
                };
            }
            for (size_t u = 0; u < extra_masters.size(); u++) mon.push_back(&k->connect("U" + std::to_string(u), lu, *extra_masters[u], *mgr));
            outer = &k->connect("M", lu, *mgr, *mem);
            mon.push_back(outer);
            MonitorOptions mo; mo.mode = MonitorOptions::Report; mo.print = true;
            for (Link *l : mon)
            {
                MonitorOptions x = mo;
                if (auto *sl = dynamic_cast<const SlaveLimits *>(l->slave())) { x.max_size = sl->declaredMaxSize(*l); x.regions = sl->declaredRegions(*l); }
                mons.emplace_back(new Monitor(*k, *l, x));
            }
            cc.reset(new CoherenceCheck(64));
            cc->onViolation = [this](const CoherenceViolation &v) { std::printf("    %s\n", v.line().c_str()); cviol.push_back(v.line()); };
            k->addObserver(cc.get());
            auto rec = [this](const coh::Transition &t)
            {
                trans.push_back(t);
                log.push_back(t.agent + " " + t.from + " + " + t.event + " -> " + t.to);
                if (std::getenv("TL_FLOW_DEBUG")) std::printf("    %s\n", t.line().c_str());
            };
            for (auto *c : cms) c->onTransition = rec;
            mgr->onTransition = rec;
        }

        void close(unsigned gate, Channel ch, uint64_t from, uint64_t to) { windows.emplace_back(gate, ch, from, to); }
        void op(unsigned m, CpuOp::Kind kind, uint64_t addr, uint64_t at)
        {
            CpuOp o; o.kind = kind; o.addr = addr; o.bytes = 8; o.not_before = at;
            cms[m]->pushOp(o);
        }
        uint64_t runTo(uint64_t cycle) { while (k->cycle() < cycle) k->step(); return k->cycle(); }
        uint64_t settle(uint64_t max = 100000) { return k->runUntilIdle(max); }
        uint64_t violations() { Monitor::finishAll(mons, k->cycle()); return Monitor::totalViolations(mons) + cviol.size(); }

        // The expected lines appear in `log` in this order (other lines may come between).
        bool sequence(const std::vector<std::string> &want, size_t from = 0) const
        {
            size_t i = from;
            for (auto &w : want)
            {
                while (i < log.size() && log[i] != w) i++;
                if (i == log.size())
                {
                    std::printf("    missing (in order): %s\n    log:\n", w.c_str());
                    for (size_t j = from; j < log.size(); j++) std::printf("      %s\n", log[j].c_str());
                    return false;
                }
                i++;
            }
            return true;
        }
        size_t mark() const { return log.size(); }
        // The value the finished access #idx of master m returned (Full data, little-endian 8 bytes).
        uint64_t value(unsigned m, size_t idx) const
        {
            const auto &f = cms[m]->finished();
            if (idx >= f.size()) return ~uint64_t(0);
            uint64_t v = 0;
            for (size_t b = 0; b < f[idx].value.size() && b < 8; b++) v |= uint64_t(f[idx].value[b]) << (8 * b);
            return v;
        }
    };
}
#endif
