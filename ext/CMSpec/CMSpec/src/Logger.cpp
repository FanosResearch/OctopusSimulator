/*
 * File  :      Logger.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Sep 17, 2021
 */

#include "../header/Logger.h"

using namespace std;
namespace ns3
{

    Logger *Logger::_logger = NULL;

    Logger::Logger()
    {
    }

    void Logger::addRequest(uint64_t cpu_id, Message &entry)
    {
        log_entries[entry.msg_id] = new vector<uint64_t>[NUM_OF_ELEMENTS_PER_ENTRY];
        // memset(log_entries[entry.msgId], 0, NUM_OF_ELEMENTS_PER_ENTRY * sizeof(uint64_t));

        log_entries[entry.msg_id][(int)EntryId::CPU_ID].push_back(cpu_id);
        log_entries[entry.msg_id][(int)EntryId::REQ_ID].push_back(entry.msg_id);
        log_entries[entry.msg_id][(int)EntryId::REQ_ADDRESS].push_back(entry.addr);
        log_entries[entry.msg_id][(int)EntryId::TRACE_CYCLE].push_back(entry.cycle);
        log_entries[entry.msg_id][(int)EntryId::CPU_CHECKPOINT].push_back(core_clk_count[cpu_id]);

        initializeStats(cpu_id);
    }

    void Logger::updateRequest(uint64_t msg_id, EntryId entryId)
    {
        if (log_entries.find(msg_id) == log_entries.end())
            return; // ignore updates with no ID (it happens in the case of replacement requests) and updates for unpresent messages (can be generated from Shared memory)

        uint64_t core_id = log_entries[msg_id][(int)EntryId::CPU_ID][0];

        log_entries[msg_id][(int)entryId].push_back(core_clk_count[core_id]);

        if (entryId == EntryId::CPU_RX_CHECKPOINT)
            calculateLatencies(msg_id);
    }

    void Logger::calculateLatencies(uint64_t msg_id)
    {
        uint64_t effective_latency;
        uint64_t core_id = getEntry(msg_id, EntryId::CPU_ID, 0);

        this->prepareReportFile(core_id);

        // for(int i = 0; i < NUM_OF_ELEMENTS_PER_ENTRY; i++)
        // {
        //     cout << "i = " << i << ":";
        //     for(int j = 0; j < log_entries[msg_id][i].size(); j++)
        //         cout << log_entries[msg_id][i][j] << "\t";
        //     cout << endl;
        // }
        // cout << "___________________________" << endl;

        report_files[core_id] << getEntry(msg_id, EntryId::REQ_ID, 0) << ",";
        report_files[core_id] << std::hex << getEntry(msg_id, EntryId::REQ_ADDRESS, 0) << "," << std::dec;
        report_files[core_id] << getEntry(msg_id, EntryId::TRACE_CYCLE, 0) << ",";

        writeLatency(report_files[core_id], getEntry(msg_id, EntryId::CPU_CHECKPOINT, 0),
                        getEntry(msg_id, EntryId::TRACE_CYCLE, 0)); // CPU Latency
                     
        logMax(writeLatency(report_files[core_id], getEntry(msg_id, EntryId::CACHE_CHECKPOINT, 0),
                        getEntry(msg_id, EntryId::CPU_CHECKPOINT, 0)), // L1 Stall latency
               &worst_case_l1_stall[core_id]);

        logMax(writeLatency(report_files[core_id], getEntry(msg_id, EntryId::REQ_BUS_CHECKPOINT, 0),
                               getEntry(msg_id, EntryId::CACHE_CHECKPOINT, 0)), // Request Bus Latency
               &worst_case_req_bus_latency[core_id]);

        logMax(writeLatency(report_files[core_id], getEntry(msg_id, EntryId::CACHE_CHECKPOINT, 1),
                        getEntry(msg_id, EntryId::REQ_BUS_CHECKPOINT, 0)), // L2 Stall latency
               &worst_case_l2_stall[core_id]);

        
        //L2 Access Latency
        if(log_entries[msg_id][(int)EntryId::CACHE_CHECKPOINT].size() == 3)
        {
            if(log_entries[msg_id][(int)EntryId::RESP_BUS_CHECKPOINT].size() == 1) //L2 Hit
            {
                logMax(writeLatency(report_files[core_id], getEntry(msg_id, EntryId::CACHE_CHECKPOINT, 2),
                                getEntry(msg_id, EntryId::CACHE_CHECKPOINT, 1)), 
                       &worst_case_l2_access[core_id]);
            }
            else //L2 miss
            {
                logMax(writeLatency(report_files[core_id], getEntry(msg_id, EntryId::CACHE_CHECKPOINT, 2),
                                getEntry(msg_id, EntryId::RESP_BUS_CHECKPOINT, 1)), //L2 access checkpoint - (DRAM-L2) checkpoint
                       &worst_case_l2_access[core_id]); 
            }
        }
        else
            report_files[core_id] << 0 << ",";//L2 Access Latency

        //Response Bus Latency
        if(log_entries[msg_id][(int)EntryId::RESP_BUS_CHECKPOINT].size() == 1) //Other L1 hit
        {
            logMax(writeLatency(report_files[core_id], getEntry(msg_id, EntryId::RESP_BUS_CHECKPOINT, 0),
                                              getEntry(msg_id, EntryId::REQ_BUS_CHECKPOINT, 0)),
                    &worst_case_resp_bus_latency[core_id]);
        }
        else if(log_entries[msg_id][(int)EntryId::RESP_BUS_CHECKPOINT].size() == 1 && 
                log_entries[msg_id][(int)EntryId::CACHE_CHECKPOINT].size() == 3) //L2 Hit
        {
            logMax(writeLatency(report_files[core_id], getEntry(msg_id, EntryId::RESP_BUS_CHECKPOINT, 0),
                                              getEntry(msg_id, EntryId::CACHE_CHECKPOINT, 2)),
                    &worst_case_resp_bus_latency[core_id]);
        }
        else //L2 Miss
        {
            logMax(writeLatency(report_files[core_id], getEntry(msg_id, EntryId::RESP_BUS_CHECKPOINT, 2),
                                              getEntry(msg_id, EntryId::RESP_BUS_CHECKPOINT, 1)), //L2 access checkpoint - (DRAM-L2) checkpoint
                    &worst_case_resp_bus_latency[core_id]);
        }

        //L2-DRAM Bus Latency + DRAM latency
        if(log_entries[msg_id][(int)EntryId::RESP_BUS_CHECKPOINT].size() > 1)
        {
            logMax(writeLatency(report_files[core_id], getEntry(msg_id, EntryId::RESP_BUS_CHECKPOINT, 0),
                            getEntry(msg_id, EntryId::CACHE_CHECKPOINT, 1)), //L2-DRAM Bus Latency
                    &worst_case_l2_dram_bus[core_id]);

            logMax(writeLatency(report_files[core_id], getEntry(msg_id, EntryId::RESP_BUS_CHECKPOINT, 1),
                            getEntry(msg_id, EntryId::RESP_BUS_CHECKPOINT, 0)), //DRAM latency including L2-DRAM bus delay
                    &worst_case_dram_latency[core_id]);
        }
        else
        {
            report_files[core_id] << 0 << ","; //L2-DRAM Bus Latency
            report_files[core_id] << 0 << ","; //DRAM latency
        }

        logMax(writeLatency(report_files[core_id], getEntry(msg_id, EntryId::CPU_RX_CHECKPOINT, 0),
                                        getEntry(msg_id, EntryId::CPU_CHECKPOINT, 0)), // Total Latency
                    &worst_case_latency[core_id]);

        effective_latency = writeLatency(report_files[core_id], getEntry(msg_id, EntryId::CPU_RX_CHECKPOINT, 0),
                                            max(this->last_checkpoint[core_id], getEntry(msg_id, EntryId::CPU_CHECKPOINT, 0))); // Effective Latency
        logMax(effective_latency, &max_effective_latency[core_id]);
        
        average_latency[core_id] += effective_latency;
        num_request[core_id]++;

        report_files[core_id] << endl;
        this->last_checkpoint[core_id] = log_entries[msg_id][(int)EntryId::CPU_RX_CHECKPOINT][0];

        delete[] log_entries[msg_id];
        log_entries.erase(msg_id);
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

    uint64_t Logger::writeLatency(ofstream &file_handler, uint64_t *checkpoints, EntryId idx)
    {
        if (checkpoints[(int)idx] == 0)
            file_handler << 0 << ",";
        else
        {
            for (int i = (int)idx - 1; i >= 0; i--)
            {
                if (checkpoints[i] != 0)
                    return writeLatency(file_handler, checkpoints[(int)idx], checkpoints[i]);
            }
        }
        return 0;
    }

    uint64_t Logger::writeLatency(ofstream &file_handler, uint64_t checkpoint2, uint64_t checkpoint1)
    {
        uint64_t diff = ((((int64_t)checkpoint2 - (int64_t)checkpoint1) > 0) ? checkpoint2 - checkpoint1 : 0);
        file_handler << diff << ",";
        return diff;
    }

    uint64_t Logger::getEntry(uint64_t msg_id, EntryId entry_id, uint64_t entry_idx)
    {
        if(log_entries[msg_id][(int)entry_id].size() > entry_idx)
            return log_entries[msg_id][(int)entry_id][entry_idx];
        else
            return 0;
    }

    void Logger::logMax(uint64_t latency, uint64_t* max_latency)
    {
        *max_latency = max(latency, *max_latency);
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
            summary_file.close();
    }

    void Logger::setClkCount(uint64_t core_id, uint64_t clk)
    {
        core_clk_count[core_id] = clk;
    }
}