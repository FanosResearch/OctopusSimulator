/*
 * File  :      CPU.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 26, 2022
 */

#include "../header/CPU.h"
#include <sstream>
#include <algorithm>

using namespace std;

namespace octopus
{
    int CPU::s_finite_cores_running = 0;

    // private controller constructor
    CPU::CPU(ParametersMap map, int id, CommunicationInterface *upper_interface, string workload_file_name, string pname, string config_path, string name)
        : ClockedObj(0), Configurable(map, config_path, name, pname)
    {
        //Parameters initialization
        m_clk_period = std::get<int>(parameters.at(STRINGIFY(m_clk_period)).value);
        m_number_of_OoO_requests = std::get<int>(parameters.at(STRINGIFY(m_number_of_OoO_requests)).value);

        //Constructor
        m_id = id;
        m_clk_cycle = 1;
        m_sent_requests = 0;
        m_sample_in_progess = NULL;
        m_last_received_msg_cycle = 0;
        m_simulation_done = false;

        m_upper_interface = upper_interface;
        ClockManager::getClockManager()->registerCLKTrigger(this);

        dprint = new DebugPrint(getSubMap(STRINGIFY(dprint)), name + std::to_string(m_id), parent_name + "." + name);

        // Workload: a periodic task program (docs/Tasks.md), an access trace, or nothing
        // (an idle core -- e.g. the "alone" configuration of a real-time study).
        if (workload_file_name.size() > 9 && workload_file_name.compare(workload_file_name.size() - 9, 9, ".task.csv") == 0)
        {
            if (!loadTask(workload_file_name))
            {
                cout << "CPU " << m_id << ": cannot read task program " << workload_file_name << endl;
                exit(0);
            }
        }
        else if (workload_file_name.empty())
        {
            m_idle = true;
            cout << "CPU " << m_id << ": no workload -- idle core" << endl;
        }
        else
            m_workload_file.open(workload_file_name);

        if (!m_idle && m_jobs_total >= 0)
            s_finite_cores_running++;
    }

    CPU::~CPU()
    {
    }

    void CPU::cycleProcess()
    {
        if(m_simulation_done)
        {
            ClockManager::getClockManager()->stopClock(this);
            return;
        }

        Logger::getLogger()->setClkCount(this->m_id, this->m_clk_cycle);
        checkReceiveBuffer();
        if (m_periodic)
            periodicLogic();
        else
            processLogic();

        m_clk_cycle++;
    }

    void CPU::init()
    {
        if (m_idle)
        {
            m_simulation_done = true;
            return;
        }
        if (m_periodic)
        {
            m_rng = 0x9E3779B97F4A7C15ull ^ ((uint64_t)m_id * 0x1234567ull);
            m_job_index = 0; m_job_release = m_start; m_job_deadline = m_start + m_deadline_rel;
            expandJob();
            return;
        }
        m_sample_in_progess = new TraceSample;
        m_sample_in_progess->read_cycle = 0;
        readSampleFromWorkload(&m_sample_in_progess->msg);
        m_sample_in_progess->compute_time = m_sample_in_progess->msg.cycle;

        m_workload_file.seekg(0, ios::beg); //return to the beginning of the file
    }

    void CPU::processLogic()
    {
        if (m_sample_in_progess == NULL)
        {
            m_sample_in_progess = new TraceSample;
            m_sample_in_progess->read_cycle = m_clk_cycle;
            if(!readSampleFromWorkload(m_sample_in_progess))
            {
                delete m_sample_in_progess;
                m_sample_in_progess = NULL;
                if(m_sent_requests == 0)
                    coreDone();
                return;
            }
        }

        if (m_sent_requests < m_number_of_OoO_requests)
        {
            uint64_t issue_cycle = m_last_received_msg_cycle + m_sample_in_progess->compute_time;

            if (m_clk_cycle >= issue_cycle)
            {
                // The Logger reports the READY cycle (earliest possible issue: compute gap
                // elapsed and sample loaded) rather than the raw trace timestamp, which the
                // CPU model only uses for gaps and which is unrelated to sim time. The
                // difference issue - ready is then the own-core OoO-window wait.
                m_sample_in_progess->msg.cycle = std::max(issue_cycle, m_sample_in_progess->read_cycle);
                m_sample_in_progess->msg.kind = Message::K_DEMAND;
                if(m_upper_interface->pushMessage(m_sample_in_progess->msg))
                {
                    Logger::getLogger()->addRequest(this->m_id, m_sample_in_progess->msg);
                    Logger::getLogger()->event(m_sample_in_progess->msg.msg_id, Logger::Role::CPU,
                                               (uint32_t)this->m_id, Logger::Phase::ENTER);
                    Logger::getLogger()->trace(m_sample_in_progess->msg, Logger::Role::CPU, (uint32_t)this->m_id, Logger::Phase::ENTER);
                    delete m_sample_in_progess;
                    m_sample_in_progess = NULL;
                    m_sent_requests++;
                }
            }
        }
    }

    void CPU::checkReceiveBuffer()
    {
        Message msg;

        if (m_upper_interface->peekMessage(&msg))
        {
            m_upper_interface->popFrontMessage();
            if (m_periodic)
            {
                auto it = m_issue_cycle.find(msg.msg_id);
                if (it != m_issue_cycle.end())
                {
                    uint64_t lat = m_clk_cycle - it->second;
                    m_job_lat_sum += lat; if (lat > m_job_lat_max) m_job_lat_max = lat;
                    m_issue_cycle.erase(it);
                }
            }
            Logger::getLogger()->event(msg.msg_id, Logger::Role::CPU, (uint32_t)this->m_id, Logger::Phase::EXIT);
            Logger::getLogger()->trace(msg, Logger::Role::CPU, (uint32_t)this->m_id, Logger::Phase::EXIT);

            m_sent_requests--;
            if (m_sent_requests < 0)
            {
                std::cout << "error" << std::endl;
                exit(0);
            }

            m_last_received_msg_cycle = m_clk_cycle;
        }
    }

    void CPU::openWorkloadFile(std::string workload_file_name)
    {
        m_workload_file.open(workload_file_name);
    }

    bool CPU::readSampleFromWorkload(Message* out_msg)
    {
        if(!m_workload_file.is_open())
        {
            cout << "CPU: Error workload file is not opened!" << endl;
            exit(0);
        }

        string sample_line;
        if (!getline(m_workload_file, sample_line))
            return false;

        char sample_type = 'R';

        sscanf(sample_line.c_str(), "%llx %*d %c %lld", &out_msg->addr, &sample_type, &out_msg->cycle);

        out_msg->complementary_value = (sample_type == 'R') ? RequestType::READ : RequestType::WRITE;
        out_msg->msg_id = IdGenerator::nextReqId();
        out_msg->owner = m_id;
        out_msg->to.push_back(m_id);

        return true;
    }

    bool CPU::readSampleFromWorkload(TraceSample* out_sample)
    {
        bool ret;
        string sample_line1;
        streampos line2_position;
        uint64_t sample1_cycle = 0;

        getline(m_workload_file, sample_line1); //read line1
        line2_position = m_workload_file.tellg();

        sscanf(sample_line1.c_str(), "%*x %*d %*c %lld", &sample1_cycle);

        ret = readSampleFromWorkload(&out_sample->msg);
        out_sample->compute_time = out_sample->msg.cycle - sample1_cycle;

        m_workload_file.seekg(line2_position); //return one line up for next call
        return ret;
    }

    // ======================= periodic task mode (docs/Tasks.md) =======================

    // The core has nothing more to issue: final report row, and let looping cores know.
    void CPU::coreDone()
    {
        m_simulation_done = true;
        Logger::getLogger()->traceEnd(this->m_id);
        if (m_jobs_total >= 0)
            s_finite_cores_running--;
    }

    // Parse `<name>.task.csv`: column 1 = row kind (attr | COMPUTE | READ | WRITE | LINEAR |
    // RANDOM), then kind-specific columns; '#' lines, blank lines and the header are skipped.
    bool CPU::loadTask(const std::string &file_name)
    {
        ifstream f(file_name);
        if (!f.is_open())
            return false;
        m_periodic = true;
        m_deadline_rel = 0;
        string line;
        auto num = [](const string &s) -> uint64_t { return s.empty() ? 0 : strtoull(s.c_str(), nullptr, 0); };
        while (getline(f, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t p = line.find_first_not_of(" \t");
            if (p == string::npos || line[p] == '#') continue;
            vector<string> col; stringstream ss(line); string c;
            while (getline(ss, c, ',')) col.push_back(c);
            while (col.size() < 5) col.push_back("");
            const string &kind = col[0];
            if (kind == "kind") continue;                       // header row
            if (kind == "attr")
            {
                if      (col[1] == "period")   m_period = num(col[2]);
                else if (col[1] == "jobs")     m_jobs_total = (int64_t)strtoll(col[2].c_str(), nullptr, 0);
                else if (col[1] == "ooo")      m_number_of_OoO_requests = (uint32_t)num(col[2]);
                else if (col[1] == "deadline") m_deadline_rel = num(col[2]);
                else if (col[1] == "start")    m_start = num(col[2]);
                else if (col[1] == "base")     { /* documentation only */ }
                else { cout << "CPU " << m_id << ": unknown task attribute '" << col[1] << "'" << endl; return false; }
                continue;
            }
            Phase ph{Phase::COMPUTE, 0, 0, 0, 1.0};
            if      (kind == "COMPUTE") { ph.kind = Phase::COMPUTE;   ph.a = num(col[1]); }
            else if (kind == "READ")    { ph.kind = Phase::READ_ONE;  ph.a = num(col[1]); }
            else if (kind == "WRITE")   { ph.kind = Phase::WRITE_ONE; ph.a = num(col[1]); }
            else if (kind == "LINEAR")  { ph.kind = Phase::LINEAR; ph.a = num(col[1]); ph.b = num(col[2]); ph.c = num(col[3]); ph.d = col[4].empty() ? 1.0 : atof(col[4].c_str()); }
            else if (kind == "RANDOM")  { ph.kind = Phase::RANDOM; ph.a = num(col[1]); ph.b = num(col[2]); ph.c = num(col[3]); ph.d = col[4].empty() ? 1.0 : atof(col[4].c_str()); }
            else { cout << "CPU " << m_id << ": unknown task row kind '" << kind << "'" << endl; return false; }
            m_body.push_back(ph);
        }
        if (m_jobs_total >= 0 && m_period == 0)
        {
            cout << "CPU " << m_id << ": a periodic task needs attr,period (looping generators use attr,jobs,-1)" << endl;
            return false;
        }
        if (m_deadline_rel == 0) m_deadline_rel = m_period;        // implicit deadline
        if (m_jobs_total < 0 && m_period == 0) m_period = 1;       // free-running generator: back-to-back jobs
        cout << "CPU " << m_id << ": periodic task " << file_name << " (period " << m_period << ", jobs " << m_jobs_total
             << ", ooo " << m_number_of_OoO_requests << ", start " << m_start << ", " << m_body.size() << " body rows)" << endl;
        return true;
    }

    // Deterministic per-core generator (splitmix64) for the LINEAR / RANDOM rd_ratio and RANDOM addresses.
    double CPU::rnd()
    {
        uint64_t z = (m_rng += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return (double)(z >> 11) * (1.0 / 9007199254740992.0);
    }

    // Instantiate the body for the next job: one TraceSample per access, whose compute_time
    // is the COMPUTE budget accumulated since the previous access (or the job start).
    void CPU::expandJob()
    {
        m_job_samples.clear(); m_job_next = 0; m_job_first_access = true; m_job_accesses = 0;
        m_job_lat_sum = 0; m_job_lat_max = 0;
        uint64_t compute = 0;
        auto access = [&](uint64_t addr, bool write) {
            TraceSample s; s.compute_time = compute; s.read_cycle = 0; compute = 0;
            s.msg.addr = addr; s.msg.cycle = 0;
            s.msg.complementary_value = write ? RequestType::WRITE : RequestType::READ;
            s.msg.msg_id = IdGenerator::nextReqId(); s.msg.owner = m_id; s.msg.to.push_back(m_id);
            m_job_samples.push_back(s);
        };
        for (const Phase &ph : m_body)
        {
            switch (ph.kind)
            {
            case Phase::COMPUTE:   compute += ph.a; break;
            case Phase::READ_ONE:  access(ph.a, false); break;
            case Phase::WRITE_ONE: access(ph.a, true); break;
            case Phase::LINEAR:
                for (uint64_t i = 0; i < ph.b; i++) access(ph.a + i * ph.c, rnd() >= ph.d);
                break;
            case Phase::RANDOM:
            {
                uint64_t lines = std::max<uint64_t>(1, ph.b / 64);
                for (uint64_t i = 0; i < ph.c; i++) access(ph.a + (uint64_t)(rnd() * lines) * 64, rnd() >= ph.d);
                break;
            }
            }
        }
        m_trailing_compute = compute;
        m_job_accesses = (uint32_t)m_job_samples.size();
    }

    // Job j finished at `finish_cycle`: report it, then schedule job j+1 (skipping the periods a
    // long overrun swallowed, each counted as a missed deadline), or end the core.
    void CPU::finishJob(uint64_t finish_cycle)
    {
        if (m_jobs_total < 0)
        {
            // free-running generator (attr,jobs,-1): no period, no deadline, back-to-back
            // bodies; it stops as soon as the last finite core is done
            Logger::getLogger()->jobReport(this->m_id, m_job_index, m_job_release, finish_cycle, 0, false, m_job_accesses, 0,
                                           m_job_accesses ? m_job_lat_sum / m_job_accesses : 0, m_job_lat_max);
            m_job_index++;
            if (s_finite_cores_running == 0) { coreDone(); return; }
            m_job_release = finish_cycle;
            expandJob();
            return;
        }
        bool miss = finish_cycle > m_job_deadline;
        // periods whose whole window [kT, kT+T) ended before we finished cannot be released at all
        uint64_t skipped = 0, k = m_job_index + 1;
        while (m_start + k * m_period + m_period <= finish_cycle && (int64_t)k < m_jobs_total) { skipped++; k++; }
        m_missed_deadlines += skipped;
        Logger::getLogger()->jobReport(this->m_id, m_job_index, m_job_release, finish_cycle, m_job_deadline, miss,
                                       m_job_accesses, skipped,
                                       m_job_accesses ? m_job_lat_sum / m_job_accesses : 0, m_job_lat_max);
        m_job_index += 1 + skipped;
        if ((int64_t)m_job_index >= m_jobs_total) { coreDone(); return; }
        m_job_release = std::max(finish_cycle, m_start + m_job_index * m_period);
        m_job_deadline = m_start + m_job_index * m_period + m_deadline_rel;
        expandJob();
    }

    void CPU::periodicLogic()
    {
        if (m_clk_cycle < m_job_release)
            return;                                                     // idle until the release
        if (m_sample_in_progess == NULL)
        {
            if (m_job_next < m_job_samples.size())
            {
                m_sample_in_progess = new TraceSample(m_job_samples[m_job_next++]);
                m_sample_in_progess->read_cycle = m_clk_cycle;
            }
            else if (m_sent_requests == 0)
            {
                // body issued and every access returned: finish = last completion (+ trailing compute)
                uint64_t done_at = (m_job_accesses ? m_last_received_msg_cycle : m_job_release) + m_trailing_compute;
                if (m_clk_cycle < done_at) return;
                finishJob(done_at);
                return;
            }
            else
                return;                                                 // draining the last accesses
        }

        if (m_sent_requests < m_number_of_OoO_requests)
        {
            // compute budget counts from the job release for the first access, else from the
            // last returned data (in-order core semantics; an approximation when ooo > 1)
            uint64_t base = m_job_first_access ? m_job_release : m_last_received_msg_cycle;
            uint64_t issue_cycle = base + m_sample_in_progess->compute_time;
            if (m_clk_cycle >= issue_cycle)
            {
                m_sample_in_progess->msg.cycle = std::max(issue_cycle, m_sample_in_progess->read_cycle);
                m_sample_in_progess->msg.kind = Message::K_DEMAND;
                if (m_upper_interface->pushMessage(m_sample_in_progess->msg))
                {
                    Logger::getLogger()->addRequest(this->m_id, m_sample_in_progess->msg);
                    Logger::getLogger()->event(m_sample_in_progess->msg.msg_id, Logger::Role::CPU, (uint32_t)this->m_id, Logger::Phase::ENTER);
                    Logger::getLogger()->trace(m_sample_in_progess->msg, Logger::Role::CPU, (uint32_t)this->m_id, Logger::Phase::ENTER);
                    delete m_sample_in_progess;
                    m_sample_in_progess = NULL;
                    m_issue_cycle[m_job_samples[m_job_next - 1].msg.msg_id] = m_clk_cycle;
                    m_sent_requests++;
                    m_job_first_access = false;
                }
            }
        }
    }
}
