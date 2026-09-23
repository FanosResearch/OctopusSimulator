/*
 * File  :      Logger.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Sep 17, 2021
 */

#include "../header/Logger.h"
#include "../header/ClockManager.h"
#include <cstdio>
#include <cstring>

#include <cstdlib>

using namespace std;
namespace octopus
{
    // Set OCTOPUS_NO_LOG=1 to skip per-request latency logging (the heap alloc + report
    // write on every request). Completion is still recorded via traceEnd()/Summary.csv.
    // Used for correctness sweeps where only successful termination matters, not latency.
    static const bool g_no_log = (std::getenv("OCTOPUS_NO_LOG") != nullptr);

    Logger *Logger::_logger = NULL;

    Logger::Logger()
    {
    }

    void Logger::addRequest(uint64_t cpu_id, Message &entry)
    {
        if (g_no_log)
        {
            // still register the core so traceEnd() can emit its Summary.csv row
            initializeStats(cpu_id);
            return;
        }

        // Design B: bind msg -> core + identity; the event timeline (built by
        // event()/finalizeEvents) is the sole per-request decomposition.
        event_core[entry.msg_id] = cpu_id;
        // entry.cycle carries the READY cycle (set by CPU::processLogic), not the raw
        // trace timestamp -- see prepareReportFile.
        event_meta[entry.msg_id] = EventMeta{entry.msg_id, entry.addr, entry.cycle};

        initializeStats(cpu_id);

        // Oldest-request tracking: append in issue order; if nothing older is in the
        // system this request is oldest from its issue cycle.
        auto &q = issue_order[cpu_id];
        q.emplace_back(entry.msg_id, false);
        if (q.size() == 1)
            oldest_start[cpu_id] = core_clk_count[cpu_id];
    }


    void Logger::registerReportPath(string file_path)
    {
        this->report_file_path = file_path;
    }

    void Logger::prepareReportFile(uint64_t core_id)
    {
        if (!this->report_files[core_id].is_open())
        {
            this->report_files[core_id].open(report_file_path + string("/LatencyReport_C") + to_string(core_id) + string(".csv"));
            // "Ready Cycle" = sim cycle at which the CPU could first issue the request
            // (its trace compute gap elapsed and the sample was loaded); "CPU Latency"
            // = issue - ready = own-core wait for a slot in the OoO window. So
            // Ready + CPU = issue cycle on every row (time-resolvable rows).
            this->report_files[core_id] << "RequstID,Request Address,Ready Cycle,";
            this->report_files[core_id] << "CPU Latency,L1 Stall Latency,Requst Bus Latency,";
            this->report_files[core_id] << "L2 Stall Latency,L2 Access Latency,";
            this->report_files[core_id] << "Response Bus Latency,L2-DRAM Bus Latency,DRAM latency,";
            this->report_files[core_id] << "L1 Access Latency,Total Latency,Effective Latency,Oldest Latency,"
                                        "LLC Arrival State,LLC Gate,Resp Ahead,Resp Ahead Refills,Array Ahead,Array Ahead Writes,LLC Stall State" << endl;
        }
    }

    void Logger::initializeStats(uint64_t core_id)
    {
        if (worst_case_req_bus_latency.find(core_id) != worst_case_req_bus_latency.end())
            return;
        else
        {
            worst_case_l1_stall[core_id] = 0;
            worst_case_req_bus_latency[core_id] = 0;
            worst_case_l2_stall[core_id] = 0;
            worst_case_l2_access[core_id] = 0;
            worst_case_resp_bus_latency[core_id] = 0;
            worst_case_l2_dram_bus[core_id] = 0;
            worst_case_dram_latency[core_id] = 0;
            worst_case_l1_access[core_id] = 0;
            worst_case_latency[core_id] = 0;
            worst_case_oldest_latency[core_id] = 0;

            max_effective_latency[core_id] = 0;
            average_latency[core_id] = 0;
            num_request[core_id] = 0;
            last_checkpoint[core_id] = 0;
        }
    }

    void Logger::logMax(uint64_t latency, uint64_t* max_latency)
    {
        *max_latency = max(latency, *max_latency);
    }

    // ---- Design B: event path (Phase 1 = record + validate, reports still legacy) ----
    static uint64_t g_evt_ok = 0;    // requests whose event timeline tiled to Total
    static uint64_t g_evt_fail = 0;  // requests that did not (gap / non-monotone)
    static uint64_t g_evt_noresp = 0; // requests with no responder SERVICE/EXIT (gap folded into L2-Access)

    void Logger::event(uint64_t msg_id, Role role, uint32_t comp_id, Phase phase)
    {
        if (g_no_log)
            return;

        // event_core is bound in addRequest (at issue). Untracked traffic (service
        // messages, replacements) never had an addRequest, so it is skipped here.
        auto it = event_core.find(msg_id);
        if (it == event_core.end())
            return;

        event_log[msg_id].push_back(LogEvent{comp_id, role, phase, core_clk_count[it->second]});

        if (role == Role::CPU && phase == Phase::EXIT)
        {
            finalizeEvents(msg_id);
            event_log.erase(msg_id);
            event_core.erase(msg_id);
            event_meta.erase(msg_id);
        }
    }

    // ---- phase-3 raw event trace ----
    static const char TRACE_MAGIC[8] = {'O','C','T','V','2',0,0,0};
    void Logger::traceOpen()
    {
        m_trace_checked = true;
        const char *f = std::getenv("OCTOPUS_TRACE");
        if (!f || !*f) return;
        m_trace_path = f; m_trace = std::fopen(f, "wb");
        if (!m_trace) { fprintf(stderr, "Logger: cannot open OCTOPUS_TRACE file %s\n", f); return; }
        std::fwrite(TRACE_MAGIC, 1, 8, m_trace);
        m_trace_buf.reserve(65536);
        // OCTOPUS_TRACE_WINDOW=t0:t1 keeps only events in that core-cycle window, so a
        // billion-cycle run can trace just the region of interest (32 B/event otherwise)
        if (const char *w = std::getenv("OCTOPUS_TRACE_WINDOW"))
        {
            unsigned long long a = 0, b = 0;
            if (sscanf(w, "%llu:%llu", &a, &b) == 2 && b > a) { m_trace_t0 = a; m_trace_t1 = b; }
            else fprintf(stderr, "Logger: ignoring malformed OCTOPUS_TRACE_WINDOW '%s' (want t0:t1)\n", w);
        }
        std::atexit(Logger::traceAtExit);
    }
    void Logger::traceFlush()
    {
        if (!m_trace || m_trace_buf.empty()) return;
        TraceChunk c{(uint64_t)std::ftell(m_trace), (uint64_t)m_trace_buf.size(), UINT64_MAX, 0, UINT64_MAX, 0};
        for (auto &r : m_trace_buf) { if (r.cycle < c.cmin) c.cmin = r.cycle; if (r.cycle > c.cmax) c.cmax = r.cycle; if (r.msg_id < c.imin) c.imin = r.msg_id; if (r.msg_id > c.imax) c.imax = r.msg_id; }
        std::fwrite(m_trace_buf.data(), sizeof(TraceRec), m_trace_buf.size(), m_trace);
        m_trace_index.push_back(c); m_trace_buf.clear();
    }
    void Logger::traceClose()
    {
        if (!m_trace) return;
        traceFlush();
        uint64_t idx_off = (uint64_t)std::ftell(m_trace);
        std::fwrite(m_trace_index.data(), sizeof(TraceChunk), m_trace_index.size(), m_trace);
        uint64_t n = m_trace_index.size();
        std::fwrite(&idx_off, 8, 1, m_trace); std::fwrite(&n, 8, 1, m_trace); std::fwrite(TRACE_MAGIC, 1, 8, m_trace);
        std::fclose(m_trace); m_trace = nullptr;
        if (!m_trace_names.empty())   // sidecar: "comp <id> states|events name,name,..."
        {
            FILE *f = std::fopen((m_trace_path + ".names").c_str(), "w");
            if (f)
            {
                for (auto &kv : m_trace_names)
                {
                    fprintf(f, "comp %u states", kv.first); for (size_t i = 0; i < kv.second.first.size(); i++) fprintf(f, "%s%s", i ? "," : " ", kv.second.first[i].c_str()); fprintf(f, "\n");
                    fprintf(f, "comp %u events", kv.first); for (size_t i = 0; i < kv.second.second.size(); i++) fprintf(f, "%s%s", i ? "," : " ", kv.second.second[i].c_str()); fprintf(f, "\n");
                }
                std::fclose(f);
            }
        }
    }
    void Logger::traceAtExit() { if (Logger::_logger) Logger::_logger->traceClose(); }
    void Logger::trace(const Message &m, Role role, uint32_t comp, Phase phase, uint16_t flags)
    {
        if (!m_trace_checked) traceOpen();
        if (!m_trace) return;
        // global core cycle: ClockManager time in ticks / the core period (100), +1 to match m_clk_cycle
        uint64_t cyc = ClockManager::getClockManager()->getCurrentTime() / 100 + 1;
        if (cyc < m_trace_t0 || cyc > m_trace_t1) return;
        m_trace_buf.push_back(TraceRec{cyc, m.msg_id, m.addr, (uint8_t)role, (uint8_t)phase, m.kind, (uint8_t)m.owner, (uint16_t)comp, flags});
        if (m_trace_buf.size() >= 65536) traceFlush();
    }
    void Logger::traceFsm(const Message &m, uint32_t comp, int old_state, int new_state, int event_id, FSMReader *fsm)
    {
        if (!m_trace_checked) traceOpen();
        if (!m_trace) return;
        if (fsm && m_trace_names.find(comp) == m_trace_names.end())
        {
            // harvest this controller's state/event names once (ids are dense from 0; a
            // missing id echoes its number, which ends the scan)
            auto &nm = m_trace_names[comp];
            for (int i = 0; i < 256; i++) { std::string s = fsm->getStateName(i); if (s == std::to_string(i)) break; nm.first.push_back(s); }
            for (int i = 0; i < 256; i++) { std::string s = fsm->getEventName(i); if (s == std::to_string(i)) break; nm.second.push_back(s); }
        }
        uint64_t cyc = ClockManager::getClockManager()->getCurrentTime() / 100 + 1;
        if (cyc < m_trace_t0 || cyc > m_trace_t1) return;
        m_trace_buf.push_back(TraceRec{cyc, m.msg_id, m.addr, (uint8_t)Role::FSM, (uint8_t)event_id, m.kind, (uint8_t)m.owner, (uint16_t)comp,
                                       (uint16_t)(((old_state & 0xff) << 8) | (new_state & 0xff))});
        if (m_trace_buf.size() >= 65536) traceFlush();
    }

    void Logger::annotate(uint64_t msg_id, Annot key, int64_t value)
    {
        if (g_no_log) return;
        auto it = event_meta.find(msg_id);
        if (it == event_meta.end()) return;   // untracked traffic
        switch (key)
        {
            case Annot::LLC_STATE:          it->second.llc_state = (int)value; break;
            case Annot::LLC_GATE:           it->second.llc_gate = (int)value; break;
            case Annot::ARRAY_AHEAD:        it->second.array_ahead = (uint32_t)value; break;
            case Annot::ARRAY_AHEAD_WRITES: it->second.array_ahead_writes = (uint32_t)value; break;
            case Annot::LLC_STALL_STATE:    if (it->second.llc_stall_state == -2) it->second.llc_stall_state = (int)value; break;  // first verdict only
        }
    }

    void Logger::finalizeEvents(uint64_t msg_id)
    {
        static const bool dbg = (std::getenv("OCTOPUS_EVENT_DEBUG") != nullptr);
        auto lit = event_log.find(msg_id);
        if (lit == event_log.end() || lit->second.empty())
            return;
        std::vector<LogEvent> &ev = lit->second;

        auto find = [&](Role r, Phase p, bool last) -> long long {
            long long found = -1;
            for (auto &e : ev)
                if (e.role == r && e.phase == p) { found = (long long)e.cycle; if (!last) break; }
            return found;
        };
        auto nth = [&](Role r, Phase p, int idx) -> long long {
            int c = 0;
            for (auto &e : ev)
                if (e.role == r && e.phase == p) { if (c == idx) return (long long)e.cycle; c++; }
            return -1;
        };

        long long issue = find(Role::CPU, Phase::ENTER, false);
        long long rx    = find(Role::CPU, Phase::EXIT,  true);
        long long l1a   = find(Role::L1,  Phase::ENTER, false);
        long long reqb  = find(Role::REQ_BUS,  Phase::EXIT, false);
        long long llca  = find(Role::LLC, Phase::ENTER, false);
        long long llcs  = find(Role::LLC, Phase::SERVICE, false);
        long long llce  = find(Role::LLC, Phase::EXIT,  true);
        long long respb = find(Role::RESP_BUS, Phase::EXIT, true);

        // Cache-to-cache supply: the line's owner (another core's L1) answered instead
        // of the LLC, so the responder's SERVICE/EXIT carry the L1 role and strictly
        // precede the response-bus grant (the requester's own L1 stamps come only after
        // it). Use the last such L1 SERVICE/EXIT as the responder milestones, and let
        // the responder's SERVICE replace LLC.ENTER as the admission anchor: the owner
        // snoops the request straight off the bus and may answer before (or after) the
        // LLC's own admission of the same request, which is then irrelevant to the data
        // path. L2-Stall = wait for the responder, L2-Access = its service, Response-Bus
        // = grant - its emit.
        if (llce < 0 && respb >= 0)
        {
            for (auto &e : ev)
                if (e.role == Role::L1 && (long long)e.cycle < respb)
                {
                    if (e.phase == Phase::SERVICE) llcs = (long long)e.cycle;
                    if (e.phase == Phase::EXIT)    llce = (long long)e.cycle;
                }
            if (llcs >= 0)
                llca = llcs;
        }
        long long memout= nth(Role::MEM_BUS, Phase::EXIT, 0);
        long long memin = nth(Role::MEM_BUS, Phase::EXIT, 1);
        long long dre   = find(Role::DRAM, Phase::ENTER, false);
        long long drx   = find(Role::DRAM, Phase::EXIT,  true);

        if (issue < 0 || rx < 0)
            return;
        long long total = rx - issue;

        // Forward milestones actually present, in canonical order. Consecutive
        // differences are the stages; their sum must equal Total (tiling).
        std::vector<long long> mil;
        mil.push_back(issue);
        if (l1a   >= 0) mil.push_back(l1a);
        if (reqb  >= 0) mil.push_back(reqb);
        if (llca  >= 0) mil.push_back(llca);
        if (memout>= 0) mil.push_back(memout);
        if (dre   >= 0) mil.push_back(dre);
        if (drx   >= 0) mil.push_back(drx);
        if (memin >= 0) mil.push_back(memin);
        if (llcs  >= 0) mil.push_back(llcs);
        if (llce  >= 0) mil.push_back(llce);
        if (respb >= 0) mil.push_back(respb);
        mil.push_back(rx);

        long long sum = 0; bool monotone = true;
        for (size_t i = 1; i < mil.size(); i++)
        {
            long long d = mil[i] - mil[i - 1];
            if (d < 0) monotone = false;
            sum += d;
        }
        bool tiled = monotone && (sum == total);
        if (tiled) g_evt_ok++; else g_evt_fail++;

        if (dbg && !tiled)
            fprintf(stderr, "EVTILE FAIL msg=%llu total=%lld sum=%lld | issue=%lld l1a=%lld reqb=%lld llca=%lld memout=%lld dre=%lld drx=%lld memin=%lld llcs=%lld llce=%lld respb=%lld rx=%lld\n",
                    (unsigned long long)msg_id, total, sum, issue, l1a, reqb, llca, memout, dre, drx, memin, llcs, llce, respb, rx);

        // OCTOPUS_EVENT_DUMP=<N>: print the raw timeline of the first N requests whose
        // Total exceeds OCTOPUS_EVENT_DUMP_MIN (default 100) cycles or, when the
        // threshold is left at its default, that touched the memory bus (diagnostic only).
        static const char *dump_env = std::getenv("OCTOPUS_EVENT_DUMP");
        static int dump_left = dump_env ? std::atoi(dump_env) : 0;
        static const char *dump_min_env = std::getenv("OCTOPUS_EVENT_DUMP_MIN");
        static const long long dump_min = dump_min_env ? std::atoll(dump_min_env) : 100;
        if (dump_left > 0 && (total > dump_min || (!dump_min_env && memout >= 0)))
        {
            dump_left--;
            static const char *rn[] = {"CPU", "L1", "REQ_BUS", "RESP_BUS", "LLC", "MEM_BUS", "DRAM", "?", "SVC_BUS", "ARRAY", "LLC_QUEUE"};
            static const char *pn[] = {"ENTER", "SERVICE", "EXIT"};
            fprintf(stderr, "EVDUMP msg=%llu total=%lld :", (unsigned long long)msg_id, total);
            for (auto &e : ev)
                fprintf(stderr, " %s.%s@%u(c%u)", rn[(int)e.role], pn[(int)e.phase], (unsigned)e.cycle, (unsigned)e.comp_id);
            fprintf(stderr, "\n");
        }

        // ---- Report: the event timeline is the sole decomposition ----
        auto mit = event_meta.find(msg_id);
        if (mit == event_meta.end())
            return;
        uint64_t core_id = event_core[msg_id];
        uint64_t req_id  = mit->second.req_id;
        uint64_t addr    = mit->second.addr;
        uint64_t trace   = mit->second.trace;

        // Clamped difference of two milestones (0 when either is absent or negative).
        auto cd = [](long long a, long long b) -> uint64_t {
            return (a >= 0 && b >= 0 && a > b) ? (uint64_t)(a - b) : 0;
        };

        bool miss = (memout >= 0);
        uint64_t cpu_lat = cd(issue, (long long)trace);
        uint64_t l1s = cd(l1a, issue);
        uint64_t rqb = cd(reqb, l1a);
        uint64_t l2s = cd(llca, reqb);
        uint64_t l2a, l2d, drm;
        if (miss)
        {
            if (dre >= 0 && drx >= 0)
            {
                // Explicit DRAM component events (MainMemoryController): split the
                // excursion into mem-bus legs and pure DRAM service.
                l2d = cd(dre, llca) + cd(memin, drx);   // LLC->membus handoff + membus transfers
                drm = cd(drx, dre);                      // pure DRAM service
            }
            else
            {
                // No DRAM component events (e.g. MCsim, which is not instrumented):
                // fall back to bounding the excursion by the two mem-bus crossings, as
                // the legacy decomposition did. Nothing of the round trip is lost.
                l2d = cd(memout, llca);                  // LLC admit -> request leaves on mem bus
                drm = cd(memin, memout);                 // mem bus out -> data back (incl. DRAM)
            }
            l2a = cd(llcs, memin);                       // refill wait (data back -> array service)
        }
        else
        {
            l2d = 0; drm = 0;
            l2a = cd(llcs, llca);                    // data-array wait (admit -> access grant)
        }
        uint64_t rsb = cd(respb, llce);              // CLEAN response bus (grant - LLC emit)
        if (llce < 0 && llca >= 0 && respb >= 0)
        {
            // No responder emit at all (no LLC and no owner-L1 SERVICE/EXIT): the whole
            // admit->grant interval is unattributable; keep the columns tiling by folding
            // it into L2-Access and count it so the gap is visible.
            l2a = cd(respb, llca); rsb = 0; g_evt_noresp++;
        }
        // L1 return path: from the last upstream milestone to delivery at the CPU. For an
        // L1 hit that is the L1's own service after admission; for anything served from
        // the bus it is transfer after the response-bus grant + the L1 fill + hand-off
        // to the CPU. With this the seven stage columns tile exactly to Total.
        uint64_t l1acc = cd(rx, std::max(l1a, respb));
        uint64_t tot = (uint64_t)total;
        uint64_t eff = (uint64_t)(rx - std::max((long long)last_checkpoint[core_id], issue));

        // Head-of-queue latency (see Logger.h): only the request at the front of its
        // core's issue-order queue was the oldest in the system; retire - oldest_start.
        // Behind an older outstanding request -> never oldest -> 0.
        uint64_t oldest = 0;
        {
            auto &q = issue_order[core_id];
            if (!q.empty() && q.front().first == msg_id)
            {
                oldest = (uint64_t)(rx - (long long)oldest_start[core_id]);
                q.pop_front();
                while (!q.empty() && q.front().second)   // drop requests that retired while not oldest
                    q.pop_front();
                if (!q.empty())
                    oldest_start[core_id] = (uint64_t)rx;  // next oldest starts at this retire
            }
            else
            {
                for (auto &e : q) if (e.first == msg_id) { e.second = true; break; }
            }
        }

        prepareReportFile(core_id);
        std::ofstream &f = report_files[core_id];
        f << req_id << "," << std::hex << addr << std::dec << "," << trace << ",";
        f << cpu_lat << ",";
        f << l1s << ","; logMax(l1s, &worst_case_l1_stall[core_id]);
        f << rqb << ","; logMax(rqb, &worst_case_req_bus_latency[core_id]);
        f << l2s << ","; logMax(l2s, &worst_case_l2_stall[core_id]);
        f << l2a << ","; logMax(l2a, &worst_case_l2_access[core_id]);
        f << rsb << ","; logMax(rsb, &worst_case_resp_bus_latency[core_id]);
        f << l2d << ","; logMax(l2d, &worst_case_l2_dram_bus[core_id]);
        f << drm << ","; logMax(drm, &worst_case_dram_latency[core_id]);
        f << l1acc << ","; logMax(l1acc, &worst_case_l1_access[core_id]);
        f << tot << ","; logMax(tot, &worst_case_latency[core_id]);
        f << eff << ","; logMax(eff, &max_effective_latency[core_id]);
        f << oldest << ","; logMax(oldest, &worst_case_oldest_latency[core_id]);

        // Resp Ahead: younger own-core responses (issued after this one, bus-served) that were
        // granted on the response bus between this request's emit and its grant -- the
        // own-slot queue it found in front of it; Refills = those that were DRAM misses.
        uint32_t ahead = 0, ahead_ref = 0;
        {
            auto &gh = grant_hist[core_id];
            if (respb >= 0 && llce >= 0)
                for (auto &g : gh)
                    if (g.issue > (uint64_t)issue && g.grant > (uint64_t)llce && g.grant < (uint64_t)respb)
                    { ahead++; if (g.miss) ahead_ref++; }
            if (respb >= 0)
            {
                gh.push_back(GrantRec{(uint64_t)issue, (uint64_t)(llce >= 0 ? llce : respb), (uint64_t)respb, miss});
                if (gh.size() > 512) gh.pop_front();
            }
        }
        f << mit->second.llc_state << "," << mit->second.llc_gate << ","
          << ahead << "," << ahead_ref << ","
          << mit->second.array_ahead << "," << mit->second.array_ahead_writes << "," << mit->second.llc_stall_state;
        f << std::endl;

        average_latency[core_id] += eff;
        num_request[core_id]++;
        last_checkpoint[core_id] = (uint64_t)rx;
    }

    void Logger::traceEnd(uint64_t core_id)
    {
        stringstream stream;     

        stream << "Worst-case L1 Stall Latency,";
        stream << "Worst-case Requst Bus Latency,";
        stream << "Worst-case L2 Stall Latency,";
        stream << "Worst-case L2 Access Latency,";
        stream << "Worst-case Response Bus Latency,";
        stream << "Worst-case L2-DRAM Bus Latency,";
        stream << "Worst-case DRAM Latency,";
        stream << "Worst-case L1 Access Latency,";
        stream << "Worst-case Total Latency,";
        stream << "Worst-case Effective Latency,";
        stream << "Average Latency";

        report_files[core_id] << endl;
        report_files[core_id] << endl;
        report_files[core_id] << endl;
        report_files[core_id] << stream.str() << ",Worst-case Oldest Latency" << endl;

        if (!this->summary_file.is_open())
        {
            summary_file.open(report_file_path + string("/Summary.csv"));
            summary_file << "Core Id,";
            summary_file << stream.str();
            summary_file << ",Finish Cycle,Worst-case Oldest Latency" << endl;
        }

        stream.str("");
        stream << worst_case_l1_stall[core_id] << ",";
        stream << worst_case_req_bus_latency[core_id] << ",";
        stream << worst_case_l2_stall[core_id] << ",";
        stream << worst_case_l2_access[core_id] << ",";
        stream << worst_case_resp_bus_latency[core_id] << ",";
        stream << worst_case_l2_dram_bus[core_id] << ",";
        stream << worst_case_dram_latency[core_id] << ",";
        stream << worst_case_l1_access[core_id] << ",";
        stream << worst_case_latency[core_id] << ",";
        stream << max_effective_latency[core_id] << ",";
        stream << 1.0 * average_latency[core_id] / num_request[core_id];

        report_files[core_id] << stream.str() << "," << worst_case_oldest_latency[core_id] << endl;
        report_files[core_id].close();
        report_files.erase(core_id);

        summary_file << core_id << ",";
        summary_file << stream.str();
        summary_file << "," << last_checkpoint[core_id] << "," << worst_case_oldest_latency[core_id] << endl;
        last_checkpoint.erase(core_id);

        if (report_files.empty())
        {
            summary_file.close();
            // Event-path self-check: report stage-tiling only if something failed
            // (a bug), or when explicitly debugging. Quiet in normal runs.
            if (g_evt_fail > 0 || std::getenv("OCTOPUS_EVENT_DEBUG") != nullptr)
                fprintf(stderr, "[EVENT-PATH] tiling ok=%llu fail=%llu noresp=%llu\n",
                        (unsigned long long)g_evt_ok, (unsigned long long)g_evt_fail,
                        (unsigned long long)g_evt_noresp);
        }
    }

    void Logger::setClkCount(uint64_t core_id, uint64_t clk)
    {
        core_clk_count[core_id] = clk;
    }
}