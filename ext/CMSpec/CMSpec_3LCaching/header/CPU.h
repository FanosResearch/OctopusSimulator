/*
 * File  :      CPU.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 26, 2022
 */

#ifndef _CPU_H
#define _CPU_H

#include "ClockManager.h"
#include "CommunicationInterface.h"
#include "CacheXml.h"
#include "Logger.h"
#include "IdGenerator.h"

#include <map>
#include <string>
#include <fstream>

namespace ns3
{
    enum RequestType
    {
        READ = 0,
        WRITE = 1,
        SETUP_WRITE = 2,
        SETUP_READ = 3,
    };

    class CPU : public ClockedObj
    {
        struct TraceSample
        {
            uint64_t compute_time;
            Message msg;
        };

    protected:
        int m_id;
        uint64_t m_clk_cycle;
        uint32_t m_number_of_OoO_requests;
        uint32_t m_sent_requests;
        uint32_t m_last_received_msg_cycle;
        bool m_simulation_done; 

        TraceSample* m_sample_in_progess;
        std::ifstream  m_workload_file;

        CommunicationInterface *m_upper_interface; // A pointer to the upper Interface FIFO
        
        virtual void cycleProcess();
        virtual void processLogic();
        virtual void checkReceiveBuffer();
        virtual bool readSampleFromWorkload(Message* out_msg);
        virtual bool readSampleFromWorkload(TraceSample* out_sample);

    public:
        CPU(CacheXml &xml, uint32_t max_OoO_requests, CommunicationInterface *upper_interface);
        CPU(CacheXml &xml, uint32_t max_OoO_requests, CommunicationInterface *upper_interface, std::string workload_file_name);
        ~CPU();

        virtual void init();
        virtual void openWorkloadFile(std::string workload_file_name);
    };
}

#endif /* _CPU_H */
