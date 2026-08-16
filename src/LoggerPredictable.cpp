/*
 * File  :      LoggerPredictable.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 24, 2021
 */

#include "../header/LoggerPredictable.h"

using namespace std;
namespace ns3
{

    LoggerPredictable::LoggerPredictable()
    {
    }

    void LoggerPredictable::retireRequest(uint64_t msg_id, vector<pair<string, uint64_t>>& stages,
                                          bool isDemand, bool isOldest)
    {
        if (log_entries.find(msg_id) == log_entries.end())
        {
            cout << "LoggerPredictable: Error can't find the logger entry" << endl;
            exit(0);
        }

        uint64_t core_id = log_entries[msg_id][(int)EntryId::CPU_ID][0];
        prepareReportFile(core_id);

        report_files[core_id] << getEntry(msg_id, EntryId::REQ_ID, 0) << ",";
        report_files[core_id] << std::hex << getEntry(msg_id, EntryId::REQ_ADDRESS, 0) << "," << std::dec;
        report_files[core_id] << getEntry(msg_id, EntryId::TRACE_CYCLE, 0) << ",";
        
        writeLatency(report_files[core_id], getEntry(msg_id, EntryId::CPU_CHECKPOINT, 0),
                        getEntry(msg_id, EntryId::TRACE_CYCLE, 0)); // CPU Latency

        if(!stages.empty())
        {
            int i;
            writeLatency(report_files[core_id], stages[0].second, getEntry(msg_id, EntryId::CPU_CHECKPOINT, 0)); // Stage 0
            for(i = 1; i < stages.size(); i++)
                writeLatency(report_files[core_id], stages[i].second, stages[i-1].second); // Stage i
            for(; i < NUM_STAGES; i++) //Write zeros for the unvisited stages
                writeLatency(report_files[core_id], 0, 0); // Stage i
            
            //Names
            report_files[core_id] << "{";
            for(int i = 0; i < stages.size(); i++)
                report_files[core_id] << stages[i].first << "-";
            report_files[core_id] << "},";
            
            report_files[core_id] << core_clk_count[core_id] << ",";
            
            if(isDemand && isOldest) //Type
                report_files[core_id] << "Oldest Demand,";
            else if(isDemand)
                report_files[core_id] << "Demand,";
            else if(!isDemand && isOldest)
                report_files[core_id] << "Oldest Replacement,";
            else
                report_files[core_id] << "Replacement,";

            auto itr = find_if(RROFArbiter::getQueues()->at(core_id).begin(), RROFArbiter::getQueues()->at(core_id).end(),
                                [=](RROFArbiter::QEntry entry) -> bool {
                                    return entry.msg_id == msg_id;
                                });
            if(itr == RROFArbiter::getQueues()->at(core_id).end())
            {
                cout << "LoggerPredictable: Error can't find RROF entry" << endl;
                exit(0);
            }
            report_files[core_id] << itr->created_by << ","; //Created By

            logMax(writeLatency(report_files[core_id], core_clk_count[core_id], getEntry(msg_id, EntryId::CPU_CHECKPOINT, 0)), 
                    &worst_case_latency[core_id]);// Total Latency (Current clk - CPU_Checkpoint)
            
            uint64_t effective_latency;
            if(isOldest)
            {
                effective_latency = writeLatency(report_files[core_id], core_clk_count[core_id],
                                                 max(this->last_checkpoint[core_id], getEntry(msg_id, EntryId::CPU_CHECKPOINT, 0))); // Effective Latency
                
                if(isDemand)
                {
                    report_files[core_id] << 0 << ",";  //combined latency
                    num_oldest_demand[core_id]++;
                    total_demand_latency[core_id] += effective_latency;
                    auto itr = find_if(stages.begin(), stages.end(), [=] (auto e) -> bool
                                        { return e.first == "memory_100";});
                    if(itr != stages.end())
                        num_l2_misses[core_id]++;
                }
                else
                {
                    uint64_t added_latency = 0;
                    if(oldest_effective_latency.find(itr->created_by) != oldest_effective_latency.end())
                    {
                        added_latency = oldest_effective_latency[itr->created_by];
                        oldest_effective_latency[itr->created_by] = effective_latency + added_latency; //Add the effective latency to the oldest demand latency, so next time we will have (R1+R2+D)
                    }
                    report_files[core_id] << effective_latency + added_latency << ",";  //combined latency
                    logMax(effective_latency + added_latency, &worst_case_combined_latency[core_id]);
                    
                    if(stages[0].first == "cache_64")
                    {
                        num_oldest_l2repl[core_id]++;
                        total_l2repl_latency[core_id] += effective_latency;
                    }
                    else
                    {
                        num_oldest_l1repl[core_id]++;
                        total_l1repl_latency[core_id] += effective_latency;
                    }
                }

                addOldestLatency(msg_id, effective_latency);
            }
            else
            {
                effective_latency = 0;
                report_files[core_id] << effective_latency << ",";  //effective latency
                report_files[core_id] << 0 << ",";  //combined latency
            }             

            logMax(effective_latency, &max_effective_latency[core_id]);
            
            average_latency[core_id] += effective_latency;
            num_request[core_id]++;

            report_files[core_id] << endl;
            if(isOldest)
                this->last_checkpoint[core_id] = core_clk_count[core_id];

            delete[] log_entries[msg_id];
            log_entries.erase(msg_id);
        }
        else
        {
            cout << "LoggerPredictable: Error no stages for the retired request" << endl;
            exit(0);
        }
    }

    void LoggerPredictable::updateRequest(uint64_t msg_id, EntryId entryId)
    {
        if(entryId == EntryId::CPU_CHECKPOINT)
            Logger::updateRequest(msg_id, entryId);

        //Do nothing
    }

    void LoggerPredictable::prepareReportFile(uint64_t core_id)
    {
        // if (!this->report_files[core_id].is_open())
        // {
        //     this->report_files[core_id].open(report_file_path + string("/LatencyReport_C") + to_string(core_id) + string(".csv"));
        //     this->report_files[core_id] << "RequstID,Request Address,Trace Cycle,";
        //     this->report_files[core_id] << "CPU Latency,";
            
        //     for(int i = 0; i < NUM_STAGES; i++)
        //         this->report_files[core_id] << "Stage " << i << ",";
            
        //     this->report_files[core_id] << "Stages,End Cycle,Type,Created By,Total Latency,Effective Latency,"; 
        //     this->report_files[core_id] << "Combined Latency" << endl;
        // }
    }

    void LoggerPredictable::initializeStats(uint64_t core_id)
    {
        if (worst_case_req_bus_latency.find(core_id) != worst_case_req_bus_latency.end())
            return;
        else
        {
            worst_case_combined_latency[core_id] = 0;
            num_oldest_demand[core_id] = 0;
            num_oldest_l1repl[core_id] = 0;
            num_oldest_l2repl[core_id] = 0;
            num_l2_misses[core_id] = 0;
            total_demand_latency[core_id] = 0;
            total_l1repl_latency[core_id] = 0;
            total_l2repl_latency[core_id] = 0;
            Logger::initializeStats(core_id);
        }
    }

    void LoggerPredictable::addOldestLatency(int msg_id, uint64_t latency)
    {
        if(oldest_effective_latency.size() >= MAX_LATENCY_MAP_SIZE)
            oldest_effective_latency.erase(oldest_effective_latency.begin());

        oldest_effective_latency[msg_id] = latency;
    }

    void LoggerPredictable::traceEnd(uint64_t core_id)
    {
        stringstream stream;     

        stream << "Worst-case Total Latency,";
        stream << "Worst-case Effective Latency,";
        stream << "Worst-case Combined Latency,";
        stream << "Average Latency,";
        stream << "# Oldest Demand,";
        stream << "# Oldest L1 Replacement,";
        stream << "# Oldest L2 Replacement,";
        stream << "Total #,";
        stream << "Total Demand Latency,";
        stream << "Total L1 Replacement Latency,";
        stream << "Total L2 Replacement Latency,";
        stream << "# L2 Misses";

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
        stream << worst_case_latency[core_id] << ",";
        stream << max_effective_latency[core_id] << ",";
        stream << worst_case_combined_latency[core_id] << ",";
        stream << 1.0 * average_latency[core_id] / num_request[core_id] << ",";
        stream << num_oldest_demand[core_id] << ",";
        stream << num_oldest_l1repl[core_id] << ",";
        stream << num_oldest_l2repl[core_id] << ",";
        stream << num_request[core_id] << ",";
        stream << total_demand_latency[core_id] << ",";
        stream << total_l1repl_latency[core_id] << ",";
        stream << total_l2repl_latency[core_id] << ",";
        stream << num_l2_misses[core_id];

        report_files[core_id] << stream.str() << endl;
        report_files[core_id].close();
        report_files.erase(core_id);

        summary_file << core_id << ",";
        summary_file << stream.str();
        summary_file << "," << last_checkpoint[core_id] << endl;
        last_checkpoint.erase(core_id);

        if (report_files.empty())
            summary_file.close();
    }
}