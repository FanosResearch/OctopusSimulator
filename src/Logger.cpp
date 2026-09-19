/*
 * File  :      Logger.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Sep 17, 2021
 */

#include "../header/Logger.h"

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
        event_meta[entry.msg_id] = EventMeta{entry.msg_id, entry.addr, entry.cycle};

        initializeStats(cpu_id);
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
            this->report_files[core_id] << "RequstID,Request Address,Trace Cycle,";
            this->report_files[core_id] << "CPU Latency,L1 Stall Latency,Requst Bus Latency,";
            this->report_files[core_id] << "L2 Stall Latency,L2 Access Latency,";
            this->report_files[core_id] << "Response Bus Latency,L2-DRAM Bus Latency,DRAM latency,";
            this->report_files[core_id] << "Total Latency,Effective Latency" << endl;
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
            worst_case_latency[core_id] = 0;

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
            l2d = cd(dre, llca) + cd(memin, drx);   // LLC->membus handoff + membus transfers
            drm = cd(drx, dre);                      // pure DRAM service
            l2a = cd(llcs, memin);                   // refill wait (data back -> array service)
        }
        else
        {
            l2d = 0; drm = 0;
            l2a = cd(llcs, llca);                    // data-array wait (admit -> access grant)
        }
        uint64_t rsb = cd(respb, llce);              // CLEAN response bus (grant - LLC emit)
        uint64_t tot = (uint64_t)total;
        uint64_t eff = (uint64_t)(rx - std::max((long long)last_checkpoint[core_id], issue));

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
        f << tot << ","; logMax(tot, &worst_case_latency[core_id]);
        f << eff;        logMax(eff, &max_effective_latency[core_id]);
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
        stream << "Worst-case Total Latency,";
        stream << "Worst-case Effective Latency,";
        stream << "Average Latency";

        report_files[core_id] << endl;
        report_files[core_id] << endl;
        report_files[core_id] << endl;
        report_files[core_id] << stream.str() << endl;

        if (!this->summary_file.is_open())
        {
            summary_file.open(report_file_path + string("/Summary.csv"));
            summary_file << "Core Id,";
            summary_file << stream.str();
            summary_file << ",Finish Cycle" << endl;
        }

        stream.str("");
        stream << worst_case_l1_stall[core_id] << ",";
        stream << worst_case_req_bus_latency[core_id] << ",";
        stream << worst_case_l2_stall[core_id] << ",";
        stream << worst_case_l2_access[core_id] << ",";
        stream << worst_case_resp_bus_latency[core_id] << ",";
        stream << worst_case_l2_dram_bus[core_id] << ",";
        stream << worst_case_dram_latency[core_id] << ",";
        stream << worst_case_latency[core_id] << ",";
        stream << max_effective_latency[core_id] << ",";
        stream << 1.0 * average_latency[core_id] / num_request[core_id];

        report_files[core_id] << stream.str() << endl;
        report_files[core_id].close();
        report_files.erase(core_id);

        summary_file << core_id << ",";
        summary_file << stream.str();
        summary_file << "," << last_checkpoint[core_id] << endl;
        last_checkpoint.erase(core_id);

        if (report_files.empty())
        {
            summary_file.close();
            // Event-path self-check: report stage-tiling only if something failed
            // (a bug), or when explicitly debugging. Quiet in normal runs.
            if (g_evt_fail > 0 || std::getenv("OCTOPUS_EVENT_DEBUG") != nullptr)
                fprintf(stderr, "[EVENT-PATH] tiling ok=%llu fail=%llu\n",
                        (unsigned long long)g_evt_ok, (unsigned long long)g_evt_fail);
        }
    }

    void Logger::setClkCount(uint64_t core_id, uint64_t clk)
    {
        core_clk_count[core_id] = clk;
    }
}