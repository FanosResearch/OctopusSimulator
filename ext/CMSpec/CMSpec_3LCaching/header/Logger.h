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

#define NUM_OF_ELEMENTS_PER_ENTRY   9

namespace ns3
{
    class Logger
    {
    public:
        enum class EntryId
        {
            CPU_ID = 0,
            REQ_ID,
            REQ_ADDRESS,
            TRACE_CYCLE,
            CPU_CHECKPOINT,
            CACHE_CHECKPOINT,
            REQ_BUS_CHECKPOINT,
            RESP_BUS_CHECKPOINT,
            CPU_RX_CHECKPOINT
        };

    protected:
        std::map<uint64_t, std::vector<uint64_t>*> log_entries; //msg_id is the key, and the value is an uint64_t array

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
        void calculateLatencies(uint64_t msg_id);

        uint64_t writeLatency(std::ofstream&, uint64_t, uint64_t); //int64_t is used instead of uint64_t to keep the sign after subtraction
        uint64_t writeLatency(std::ofstream&, uint64_t *, EntryId);

        uint64_t getEntry(uint64_t msg_id, EntryId entry_id, uint64_t entry_idx);
        void logMax(uint64_t latency, uint64_t* max_latency);

    public:
        void addRequest(uint64_t cpu_id, Message&);
        void updateRequest(uint64_t msg_id, EntryId entryId);
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