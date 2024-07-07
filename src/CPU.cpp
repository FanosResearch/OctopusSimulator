/*
 * File  :      CPU.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 26, 2022
 */

#include "../header/CPU.h"

using namespace std;

namespace octopus
{
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

        m_workload_file.open(workload_file_name);
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
        processLogic();

        m_clk_cycle++;
    }

    void CPU::init()
    {
        m_sample_in_progess = new TraceSample;
        readSampleFromWorkload(&m_sample_in_progess->msg);
        m_sample_in_progess->compute_time = m_sample_in_progess->msg.cycle;

        m_workload_file.seekg(0, ios::beg); //return to the beginning of the file
    }

    void CPU::processLogic()
    {
        if (m_sample_in_progess == NULL)
        {
            m_sample_in_progess = new TraceSample;
            if(!readSampleFromWorkload(m_sample_in_progess))
            {
                delete m_sample_in_progess;
                m_sample_in_progess = NULL;
                if(m_sent_requests == 0)
                {    
                    m_simulation_done = true;
                    Logger::getLogger()->traceEnd(this->m_id);
                }
                return;
            }
        }

        if (m_sent_requests < m_number_of_OoO_requests)
        {
            uint64_t issue_cycle = m_last_received_msg_cycle + m_sample_in_progess->compute_time;

            if (m_clk_cycle >= issue_cycle)
            {
                if(m_upper_interface->pushMessage(m_sample_in_progess->msg))
                {
                    Logger::getLogger()->addRequest(this->m_id, m_sample_in_progess->msg);
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
            Logger::getLogger()->updateRequest(msg.msg_id, Logger::EntryId::CPU_RX_CHECKPOINT);
            
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
}