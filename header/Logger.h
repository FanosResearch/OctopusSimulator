/*
 * File  :      Logger.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Sep 17, 2021
 */

#ifndef LOGGER_H
#define LOGGER_H

#include "CommunicationInterface.h"
#include "FSMReader.h"

#include <stdint.h>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>
#include <vector>
#include <deque>

namespace octopus
{
    class Logger
    {
    public:
        // ---- Design B: self-describing timeline events ------------------------
        // Each checkpoint records WHICH component and WHICH phase stamped it, so
        // the per-stage decomposition is a generic walk over the event list --
        // never inferred from a vector's index/size (see docs/Logger_DesignB_plan.md).
        enum class Role : uint8_t { CPU, L1, REQ_BUS, RESP_BUS, LLC, MEM_BUS, DRAM, UNKNOWN, SVC_BUS, ARRAY, LLC_QUEUE, FSM };
        enum class Phase : uint8_t { ENTER, SERVICE, EXIT };
        struct LogEvent
        {
            uint32_t comp_id;   // stable per-component id (m_id / core id / bus id)
            Role     role;
            Phase    phase;
            uint64_t cycle;     // originating core's clock (same clock trick as legacy)
        };

    protected:
        // Design B: event timeline per in-flight message (msg_id -> events),
        // plus msg_id -> core binding and identity, so the decomposition is fully
        // self-contained.
        // Per-request mechanism trackers (annotations set by the components, reported as
        // columns; see docs/Logger.md S5):
        //   llc_state  line state at LLC arrival (-2 never reached the LLC, -1 line absent)
        //   llc_gate   an older demand request to the same line was already queued at arrival
        //   array_ahead / array_ahead_writes  accesses served at the LLC array port between this
        //              request's park and its grant (and how many of them were data writes)
        struct EventMeta { uint64_t req_id; uint64_t addr; uint64_t trace;
                           int llc_state = -2; int llc_gate = 0;
                           int llc_stall_state = -2;   // line state at the FIRST NonReady verdict at the LLC (-2 = never stalled by the FSM)
                           uint32_t array_ahead = 0; uint32_t array_ahead_writes = 0; };
        // Response-bus grant history per core (issue, emit, grant, miss) to count, for a
        // request X, the younger own-core responses granted between X's emit and X's grant.
        struct GrantRec { uint64_t issue, emit, grant; bool miss; };
        std::map<uint64_t, std::deque<GrantRec>> grant_hist;
        std::map<uint64_t, std::vector<LogEvent>> event_log;
        std::map<uint64_t, uint64_t> event_core;
        std::map<uint64_t, EventMeta> event_meta;

        //core_id is the key, and the value is the latency
        std::map<uint64_t, uint64_t> worst_case_l1_stall;
        std::map<uint64_t, uint64_t> worst_case_req_bus_latency;
        std::map<uint64_t, uint64_t> worst_case_l2_stall;
        std::map<uint64_t, uint64_t> worst_case_l2_access;
        std::map<uint64_t, uint64_t> worst_case_resp_bus_latency;
        std::map<uint64_t, uint64_t> worst_case_l2_dram_bus;
        std::map<uint64_t, uint64_t> worst_case_dram_latency;
        std::map<uint64_t, uint64_t> worst_case_l1_access;   // return path: last upstream milestone -> CPU delivery
        std::map<uint64_t, uint64_t> worst_case_latency;
        
        std::map<uint64_t, uint64_t> max_effective_latency; //core_id is the key, and the value is the max latency contribution
        std::map<uint64_t, uint64_t> average_latency; //core_id is the key, and the value is the average latency
        std::map<uint64_t, uint64_t> num_request; //core_id is the key, number of requests

        std::map<uint64_t, std::ofstream> report_files; //core_id is the key, and the value is the report file handler
        std::map<uint64_t, std::ofstream> job_files;    //JobReport_C<n>.csv of the cores running a periodic task
        std::ofstream summary_file;                     //To report the worst-case values of all the cores
        
        std::map<uint64_t, uint64_t> core_clk_count; //core_id is the key, and the value is the report file handler

        std::map<uint64_t, uint64_t> last_checkpoint;   //This value is used to caculate the contribution of each request to the total program time

        // Head-of-queue ("oldest request") latency, PCC's per-request quantity: per core,
        // outstanding requests in ISSUE order (msg_id, retired?). The front is the oldest
        // request in the system; it became oldest at oldest_start (its issue cycle if
        // nothing older was outstanding, else the retire cycle of the previous oldest).
        // Oldest Latency = retire - oldest_start for the request at the front when it
        // retires; a request retiring behind an older one was never oldest (reported 0).
        std::map<uint64_t, std::deque<std::pair<uint64_t, bool>>> issue_order;
        std::map<uint64_t, uint64_t> oldest_start;
        std::map<uint64_t, uint64_t> worst_case_oldest_latency;

        std::string report_file_path;
        static Logger *_logger;

        Logger();
        void prepareReportFile(uint64_t core_id);
        void initializeStats(uint64_t core_id);
        void logMax(uint64_t latency, uint64_t* max_latency);

        // Build the per-request report row from the message's event timeline:
        // recover each stage as a difference of named milestones (self-describing,
        // no positional inference), update the worst-case stats, and validate that
        // the stages tile to Total (OCTOPUS_EVENT_DEBUG dumps any mismatch).
        void finalizeEvents(uint64_t msg_id);

    public:
        // Design B: append a self-describing event to a tracked message's timeline.
        void event(uint64_t msg_id, Role role, uint32_t comp_id, Phase phase);
        enum class Annot : uint8_t { LLC_STATE, LLC_GATE, ARRAY_AHEAD, ARRAY_AHEAD_WRITES, LLC_STALL_STATE };
        void annotate(uint64_t msg_id, Annot key, int64_t value);

        // ---- Phase-3 raw event trace (OCTOPUS_TRACE=<file>): every resource event of EVERY
        // message (requests and non-request traffic alike), independent of the per-request
        // report. Fixed 32-byte records, 64K-record chunks, chunk index at the end
        // (docs/Trace.md). Zero cost when the variable is unset. Cycle = global core cycle
        // from the ClockManager (so the 2x-clocked buses log in core-cycle units).
        // Role::FSM records are coherence transitions: phase = FSM event id,
        // flags = (old_state << 8) | new_state, comp = the controller; the state/event
        // names of every controller go to the "<file>.names" sidecar at close.
        struct TraceRec { uint64_t cycle; uint64_t msg_id; uint64_t addr; uint8_t resource; uint8_t phase; uint8_t kind; uint8_t core; uint16_t comp; uint16_t flags; };
        void trace(const Message &m, Role role, uint32_t comp, Phase phase, uint16_t flags = 0);
        void traceFsm(const Message &m, uint32_t comp, int old_state, int new_state, int event_id, FSMReader *fsm);
        bool tracing() const { return m_trace != nullptr; }
    protected:
        FILE *m_trace = nullptr; bool m_trace_checked = false; std::string m_trace_path;
        uint64_t m_trace_t0 = 0, m_trace_t1 = UINT64_MAX;   // OCTOPUS_TRACE_WINDOW=t0:t1 (core cycles)
        std::vector<TraceRec> m_trace_buf;
        struct TraceChunk { uint64_t offset, count, cmin, cmax, imin, imax; };
        std::vector<TraceChunk> m_trace_index;
        std::map<uint32_t, std::pair<std::vector<std::string>, std::vector<std::string>>> m_trace_names;   // comp -> (states, events)
        void traceOpen(); void traceFlush(); void traceClose();
    public:
        static void traceAtExit();
        void addRequest(uint64_t cpu_id, Message&);
        void registerReportPath(std::string file_path);
        void traceEnd(uint64_t core_id);
        void setClkCount(uint64_t core_id, uint64_t clk);
        // periodic task mode (docs/Tasks.md): one row per job in JobReport_C<n>.csv
        void jobReport(uint64_t core_id, uint64_t job, uint64_t release, uint64_t finish, uint64_t deadline,
                       bool miss, uint32_t n_accesses, uint64_t skipped_periods,
                       uint64_t mean_access_lat, uint64_t max_access_lat);

        static Logger *getLogger()
        {
            if (Logger::_logger == NULL)
                Logger::_logger = new Logger();
            return Logger::_logger;
        }
    };
}

#endif