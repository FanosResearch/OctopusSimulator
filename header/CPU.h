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
#include "Configurable.h"
#include "DebugPrint.h"
#include "CommunicationInterface.h"
#include "CacheXml.h"
#include "Logger.h"
#include "IdGenerator.h"

#include <map>
#include <string>
#include <vector>
#include <fstream>

namespace octopus
{
    enum RequestType
    {
        READ = 0,
        WRITE = 1,
        SETUP_WRITE = 2,
        SETUP_READ = 3,
    };

    class CPU : public ClockedObj, Configurable
    {
        struct TraceSample
        {
            uint64_t compute_time;
            uint64_t read_cycle;   // sim cycle the sample was loaded (earliest it could issue)
            Message msg;
        };

        // ---- periodic task mode (docs/Tasks.md) ----
        // A `<workload>/task_C<n>.task.csv` replaces the access trace: a body of COMPUTE /
        // READ / WRITE / LINEAR / RANDOM rows re-run every `period` cycles, with the job
        // timing (release, finish, deadline, miss) reported by the simulator itself.
        struct Phase
        {
            enum Kind { COMPUTE, READ_ONE, WRITE_ONE, LINEAR, RANDOM } kind;
            uint64_t a, b, c;   // COMPUTE: cycles | READ/WRITE: addr | LINEAR: base,count,stride | RANDOM: base,range,count
            double d;           // rd_ratio for LINEAR / RANDOM
        };

    protected:
        int m_id;
        uint64_t m_clk_cycle;
        uint32_t m_number_of_OoO_requests;
        uint64_t m_last_received_msg_cycle; // 64-bit: wraps past 2^32 on giant traces
        int32_t m_sent_requests;
        bool m_simulation_done;

        TraceSample* m_sample_in_progess;
        std::ifstream  m_workload_file;

        CommunicationInterface *m_upper_interface; // A pointer to the upper Interface FIFO
        DebugPrint* dprint;

        // periodic task state (all unused on the legacy trace path)
        bool m_periodic = false, m_idle = false;
        uint64_t m_period = 0, m_deadline_rel = 0;     // relative deadline (defaults to period)
        uint64_t m_start = 0;                          // release of job 0 (attr,start): the task's phase
        int64_t m_jobs_total = 0;                      // -1 = loop until every finite core is done
        uint64_t m_job_index = 0, m_job_release = 0, m_job_deadline = 0;
        uint64_t m_trailing_compute = 0, m_missed_deadlines = 0;
        uint32_t m_job_accesses = 0;
        // per-job access latency as the CPU observes it (issue -> response received); the
        // stage breakdown stays in LatencyReport. Lets a live reader use JobReport alone.
        uint64_t m_job_lat_sum = 0, m_job_lat_max = 0;
        std::map<uint64_t, uint64_t> m_issue_cycle;   // msg_id -> issue cycle, for accesses in flight
        bool m_job_first_access = true;
        std::vector<Phase> m_body;
        std::vector<TraceSample> m_job_samples;
        size_t m_job_next = 0;
        uint64_t m_rng = 0;
        static int s_finite_cores_running;             // cores with a finite job count / trace still running

        virtual void cycleProcess();
        virtual void processLogic();
        virtual void checkReceiveBuffer();
        virtual bool readSampleFromWorkload(Message* out_msg);
        virtual bool readSampleFromWorkload(TraceSample* out_sample);

        bool loadTask(const std::string &file_name);
        void expandJob();
        void finishJob(uint64_t finish_cycle);
        void coreDone();
        double rnd();
        void periodicLogic();

    public:
        CPU(ParametersMap map, int id, CommunicationInterface *upper_interface,
            string workload_file_name,
            string pname = "",
            string config_path = string(CONFIGURATION_PATH),
            string name = STRINGIFY(CPU));
        ~CPU();

        virtual void init();
        virtual void openWorkloadFile(std::string workload_file_name);
        bool periodic() const { return m_periodic; }
    };
}

#endif /* _CPU_H */
