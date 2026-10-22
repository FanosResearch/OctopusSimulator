/*
 * File  :      TlcSystem.h
 * Test fixture (header only, compiled into the TL-C tests): builds the TL-C systems of plan §6
 * (TL-FUZZ-C, TL-UNCACHED, TL-DENY TL-C part) programmatically:
 *   n CacheMasters -> [Throttle] -> (one link each to the manager | an Xbar) -> CoherentManager
 *   [-> Throttle] -> RamSlave or ErrorSlave, plus optional uncached FuzzMasters on their own TL-UH
 *   links to the manager;
 * with a Monitor (report mode) on every link (the Xbar's output too: the Monitor keeps the master-side
 * TL-C state per client, traced through the Xbar's source ranges), a
 * CoherenceCheck (counting), a ValueCheck on the outer link (memory sees only the manager), a
 * GraphCheck and the Watchdog with its default bound (report mode, trips counted).
 */
#ifndef _TL_TEST_TLC_SYSTEM_H
#define _TL_TEST_TLC_SYSTEM_H
#include "tilelink/CoherenceCheck.h"
#include "tilelink/GraphCheck.h"
#include "tilelink/Monitor.h"
#include "tilelink/ValueCheck.h"
#include "tilelink/Watchdog.h"
#include "tilelink/agents/CacheMaster.h"
#include "tilelink/agents/CoherentManager.h"
#include "tilelink/agents/ErrorSlave.h"
#include "tilelink/agents/FuzzMaster.h"
#include "tilelink/agents/RamSlave.h"
#include "tilelink/agents/Throttle.h"
#include "tilelink/agents/Xbar.h"
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef TL_TABLE_DIR
#define TL_TABLE_DIR "tables"
#endif

namespace tlc
{
    using namespace octopus::tl;

    inline Config cfg(const std::vector<std::string> &lines)
    {
        Config c;
        for (auto &l : lines) c.set(l);
        return c;
    }
    inline std::string table(const char *n) { return std::string(TL_TABLE_DIR) + "/" + n; }

    struct Options
    {
        unsigned    masters = 4;
        std::string protocol = "MESI";
        uint64_t    seed = 1;
        bool        xbar = false;
        bool        throttle = true;          // a Throttle on every master link and on the outer link
        double      p_ready = 0.3, p_valid = 0.2;
        unsigned    ops = 1000, span = 1024, store_pct = 40, evict_pct = 5, outstanding = 4;
        unsigned    l1_sets = 2, l1_ways = 2, llc_sets = 2, llc_ways = 4;
        unsigned    mem_latency = 3, mem_jitter = 4;
        DataMode    mode = DataMode::Full;
        bool        error_slave = false;      // the outer slave denies everything
        std::vector<int> deny;                // RamSlave denied base,size pairs
        uint64_t    error_split = 0;          // > 0: the outer link goes to an Xbar, [0, split) to the RamSlave, [split, 2*split) to an ErrorSlave
        unsigned    uncached = 0;             // FuzzMasters on their own TL-UH links to the manager
        unsigned    uncached_ops = 500;
        std::vector<std::string> uncached_cfg;   // extra FuzzMaster keys
        std::vector<std::string> cache_cfg;      // extra CacheMaster keys
        std::vector<std::string> mgr_cfg;        // extra manager keys
        unsigned    o = 3, i = 4, w = 8;
        uint64_t    max_cycles = 20000000;
        bool        cpu_script = false;
    };

    struct System
    {
        std::unique_ptr<Kernel> k;
        std::vector<CacheMaster *> cms;
        std::vector<FuzzMaster *> ums;
        CoherentManager *mgr = nullptr;
        RamSlave *mem = nullptr;
        RamSlave *err = nullptr;
        std::vector<std::unique_ptr<Monitor>> mons;
        std::unique_ptr<CoherenceCheck> cc;
        std::unique_ptr<ValueCheck> vc;
        std::unique_ptr<Watchdog> wd;
        std::unique_ptr<GraphCheck> gc;
        std::vector<CoherenceViolation> cviol;
        std::vector<ValueViolation> vviol;
        Link *outer = nullptr;
        uint64_t cycles = 0;

        ~System() { mons.clear(); vc.reset(); k.reset(); }

        template <typename T> T *add(T *a) { k->add(std::unique_ptr<Agent>(a)); return a; }

        void build(const Options &o)
        {
            k.reset(new Kernel(KernelOptions{100, o.seed, o.mode}));
            LinkParams lc; lc.level = Level::C; lc.w = o.w; lc.o = o.o; lc.i = o.i; lc.z = 3;
            LinkParams lu; lu.level = Level::UH; lu.w = o.w; lu.o = 4; lu.i = 0; lu.z = 3;
            for (unsigned m = 0; m < o.masters; m++)
            {
                std::vector<std::string> c = {"table(s)=" + table("TLC_MESI_L1.csv"), "ops(i)=" + std::to_string(o.ops),
                                              "addr_span(i)=" + std::to_string(o.span), "store_pct(i)=" + std::to_string(o.store_pct),
                                              "evict_pct(i)=" + std::to_string(o.evict_pct), "sets(i)=" + std::to_string(o.l1_sets),
                                              "ways(i)=" + std::to_string(o.l1_ways), "outstanding(i)=" + std::to_string(o.outstanding)};
                if (o.cpu_script) c.push_back("cpu(s)=script");
                for (auto &x : o.cache_cfg) c.push_back(x);
                cms.push_back(add(new CacheMaster("c" + std::to_string(m), cfg(c))));
            }
            for (unsigned u = 0; u < o.uncached; u++)
            {
                std::vector<std::string> c = {"ops(i)=" + std::to_string(o.uncached_ops), "addr_span(i)=" + std::to_string(o.span), "rate(i)=30"};
                for (auto &x : o.uncached_cfg) c.push_back(x);
                ums.push_back(add(new FuzzMaster("u" + std::to_string(u), cfg(c))));
            }
            Xbar *xb = o.xbar ? add(new Xbar("xbar", cfg({}))) : nullptr;
            std::vector<std::string> mc = {"table(s)=" + table(o.protocol == "MSI" ? "TLC_MSI_LLC.csv" : "TLC_MESI_LLC.csv"),
                                           "sets(i)=" + std::to_string(o.llc_sets), "ways(i)=" + std::to_string(o.llc_ways)};
            for (auto &x : o.mgr_cfg) mc.push_back(x);
            mgr = add(new CoherentManager("llc", cfg(mc)));
            std::vector<std::string> rc = {"latency(i)=" + std::to_string(o.mem_latency), "latency_jitter(i)=" + std::to_string(o.mem_jitter),
                                           "queue_depth(i)=8"};
            if (!o.deny.empty())
            {
                std::string d = "deny(vi)=";
                for (size_t x = 0; x < o.deny.size(); x++) d += (x ? "," : "") + std::to_string(o.deny[x]);
                rc.push_back(d);
            }
            mem = o.error_slave ? add(new ErrorSlave("mem", cfg(rc))) : add(new RamSlave("mem", cfg(rc)));
            Xbar *ox = nullptr;
            if (o.error_split)
            {
                err = add(new ErrorSlave("err", cfg(rc)));
                ox = add(new Xbar("oxbar", cfg({"routes(vs)=MA:0:" + std::to_string(o.error_split) + ",ME:" + std::to_string(o.error_split) + ":" +
                                                std::to_string(o.error_split)})));
            }
            std::vector<Link *> single;   // the monitored links: all of them
            std::string thr_p = "p_ready(d)=" + std::to_string(o.p_ready), thv_p = "p_valid(d)=" + std::to_string(o.p_valid);
            Agent *inner = xb ? static_cast<Agent *>(xb) : static_cast<Agent *>(mgr);
            LinkParams lx = lc;
            if (xb)
            {
                unsigned need = 0;
                while ((1u << need) < (o.masters << o.o)) need++;
                lx.o = need;
            }
            for (unsigned m = 0; m < o.masters; m++)
            {
                std::string n = std::to_string(m);
                if (o.throttle)
                {
                    Throttle *t = add(new Throttle("t" + n, cfg({thr_p, thv_p})));
                    single.push_back(&k->connect("L" + n, lc, *cms[m], *t));
                    single.push_back(&k->connect("T" + n, lc, *t, *inner));
                }
                else single.push_back(&k->connect("L" + n, lc, *cms[m], *inner));
            }
            if (xb) single.push_back(&k->connect("LX", lx, *xb, *mgr));   // several masters: the Monitor traces sources through the Xbar
            for (unsigned u = 0; u < o.uncached; u++)
                single.push_back(&k->connect("U" + std::to_string(u), lu, *ums[u], *mgr));
            if (ox)
            {
                outer = &k->connect("M0", lu, *mgr, *ox);
                single.push_back(outer);
                single.push_back(&k->connect("MA", lu, *ox, *mem));
                single.push_back(&k->connect("ME", lu, *ox, *err));
            }
            else if (o.throttle)
            {
                Throttle *t = add(new Throttle("tm", cfg({thr_p, thv_p})));
                outer = &k->connect("M0", lu, *mgr, *t);
                single.push_back(outer);
                single.push_back(&k->connect("M1", lu, *t, *mem));
            }
            else
            {
                outer = &k->connect("M0", lu, *mgr, *mem);
                single.push_back(outer);
            }
            MonitorOptions mo; mo.mode = MonitorOptions::Report; mo.print = false;
            for (Link *l : single)
            {
                MonitorOptions x = mo;
                if (const SlaveLimits *sl = dynamic_cast<const SlaveLimits *>(l->slave())) { x.max_size = sl->declaredMaxSize(*l); x.regions = sl->declaredRegions(*l); }
                mons.emplace_back(new Monitor(*k, *l, x));
            }
            if (o.mode != DataMode::None)
            {
                vc.reset(new ValueCheck("outer"));
                vc->onViolation = [this](const ValueViolation &v) { vviol.push_back(v); };
                vc->attach(*outer);
            }
            cc.reset(new CoherenceCheck(64));
            cc->onViolation = [this](const CoherenceViolation &v) { if (cviol.size() < 20) std::printf("    %s\n", v.line().c_str()); cviol.push_back(v); };
            k->addObserver(cc.get());
            gc.reset(new GraphCheck());
            k->addObserver(gc.get());
            WatchdogOptions wo; wo.mode = WatchdogMode::Report;
            static std::ostringstream sink;
            wo.log = &sink;
            wd.reset(new Watchdog(wo));
            k->addObserver(wd.get());
        }

        uint64_t run(uint64_t max) { cycles = k->runUntilIdle(max); Monitor::finishAll(mons, k->cycle()); return cycles; }
        uint64_t monitorViolations() const { return Monitor::totalViolations(mons); }
        std::string monitorSummary() const
        {
            std::string s;
            for (auto &m : mons) if (m->violations()) { s += m->summary() + "\n"; for (size_t i = 0; i < m->log().size() && i < 5; i++) s += "      " + m->log()[i].line() + "\n"; }
            return s;
        }
        bool allDone() const
        {
            for (auto *c : cms) if (!c->cpuDone()) return false;
            for (auto *u : ums) if (!u->idle()) return false;
            return true;
        }
        uint64_t opsDone() const
        {
            uint64_t n = 0;
            for (auto *c : cms) n += c->counters().loads + c->counters().stores + c->counters().errors + c->counters().evicts;
            return n;
        }
    };
}
#endif
