/*
 * File  :      LoggerPredictable.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 24, 2021
 */

#ifndef LOGGERPREDICTABLE_H
#define LOGGERPREDICTABLE_H

#include "Logger.h"
#include "Arbiters/RROFArbiter.h"

#include <string>
#include <utility>

using namespace std;

#define NUM_STAGES  12
#define MAX_LATENCY_MAP_SIZE    4096

namespace ns3
{
    class LoggerPredictable : public Logger
    {
    protected:
        std::map<uint64_t, uint64_t> worst_case_combined_latency;     //Key is core_id, and value is the combined latency
        std::map<int, uint64_t> oldest_effective_latency;             //Key is the retired oldest msg_id, value is the effective latency

        std::map<uint64_t, uint64_t> num_oldest_demand; //core_id is the key, number of requests
        std::map<uint64_t, uint64_t> num_oldest_l1repl; //core_id is the key, number of requests
        std::map<uint64_t, uint64_t> num_oldest_l2repl; //core_id is the key, number of requests
        std::map<uint64_t, uint64_t> num_l2_misses; //core_id is the key, number of requests
        std::map<uint64_t, uint64_t> total_demand_latency; //core_id is the key, total latency
        std::map<uint64_t, uint64_t> total_l1repl_latency; //core_id is the key, total latency
        std::map<uint64_t, uint64_t> total_l2repl_latency; //core_id is the key, total latency

        virtual void prepareReportFile(uint64_t core_id) override;
        virtual void initializeStats(uint64_t core_id) override;
        virtual void addOldestLatency(int msg_id, uint64_t latency);

    public:
        LoggerPredictable();

        virtual void updateRequest(uint64_t msg_id, EntryId entryId) override;
        virtual void retireRequest(uint64_t msg_id, vector<pair<string, uint64_t>>& stages, 
                                   bool isDemand, bool isOldest);
        

        virtual void traceEnd(uint64_t core_id) override;
    };
}

#endif