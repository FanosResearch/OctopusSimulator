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

#include <stdint.h>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>
#include <vector>

namespace octopus
{
    class Logger
    {
    public:
        // ---- Design B: self-describing timeline events ------------------------
        // Each checkpoint records WHICH component and WHICH phase stamped it, so
        // the per-stage decomposition is a generic walk over the event list --
        // never inferred from a vector's index/size (see docs/Logger_DesignB_plan.md).
        enum class Role : uint8_t { CPU, L1, REQ_BUS, RESP_BUS, LLC, MEM_BUS, DRAM, UNKNOWN };
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
        struct EventMeta { uint64_t req_id; uint64_t addr; uint64_t trace; };
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
        std::map<uint64_t, uint64_t> worst_case_latency;
        
        std::map<uint64_t, uint64_t> max_effective_latency; //core_id is the key, and the value is the max latency contribution
        std::map<uint64_t, uint64_t> average_latency; //core_id is the key, and the value is the average latency
        std::map<uint64_t, uint64_t> num_request; //core_id is the key, number of requests

        std::map<uint64_t, std::ofstream> report_files; //core_id is the key, and the value is the report file handler
        std::ofstream summary_file;                     //To report the worst-case values of all the cores
        
        std::map<uint64_t, uint64_t> core_clk_count; //core_id is the key, and the value is the report file handler

        std::map<uint64_t, uint64_t> last_checkpoint;   //This value is used to caculate the contribution of each request to the total program time

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
        void addRequest(uint64_t cpu_id, Message&);
        void registerReportPath(std::string file_path);
        void traceEnd(uint64_t core_id);
        void setClkCount(uint64_t core_id, uint64_t clk);

        static Logger *getLogger()
        {
            if (Logger::_logger == NULL)
                Logger::_logger = new Logger();
            return Logger::_logger;
        }
    };
}

#endif