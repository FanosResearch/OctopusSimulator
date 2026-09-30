/*
 * File  :      ExternalCPU.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Sept 6, 2022
 */

#include "../header/ExternalCPU.h"
#include "../header/CacheControllers/BaseController.h"

using namespace std;

namespace octopus
{
    namespace
    {
        std::string resolveConfigPath(const std::string &config_path)
        {
            return config_path.empty() ? std::string(CONFIGURATION_PATH) : config_path;
        }
    }

    ExternalCPU::ExternalCPU(ParametersMap map, int id, CommunicationInterface *upper_interface,
                             std::string pname, std::string config_path, std::string name)
        : ClockedObj(0), Configurable(map, resolveConfigPath(config_path), name, pname)
    {
        m_clk_period = std::get<int>(parameters.at(STRINGIFY(m_clk_period)).value);

        m_id = id;
        m_clk_cycle = 1;
        m_upper_interface = upper_interface;
        m_processing_queue = new FRFCFS_Buffer<Message, ExternalCPU>(&ExternalCPU::getRequestState, this);
    }

    ExternalCPU::~ExternalCPU()
    {
    }

    void ExternalCPU::cycleProcess()
    {
        processLogic();
        m_clk_cycle++;
    }

    void ExternalCPU::init()
    {
    }

    void ExternalCPU::processLogic()
    {
        addRequests2ProcessingQueue(*m_processing_queue);

        Message ready_msg;
        while (true)
        {
            if (m_processing_queue->getFirstReady(&ready_msg) == false)
                return;

            if(ready_msg.source == Message::Source::LOWER_INTERCONNECT)
            {
                ready_msg.cycle = this->m_clk_cycle;
                if (!m_upper_interface->pushMessage(ready_msg, m_clk_cycle, MessageType::REQUEST))
                {
                    // Link FIFO full. Keep the request, in order, and retry
                    // next cycle. An embedder that honours isBlocked() never
                    // gets here; without one this is back-pressure rather
                    // than an abort.
                    m_processing_queue->pushFront(ready_msg);
                    m_link_full_holds++;
                    return;
                }
            }
            else if(ready_msg.source == Message::Source::UPPER_INTERCONNECT)
            {
                if(m_cpu_callback != NULL)
                    (*m_cpu_callback)(ready_msg.msg_id, ready_msg.addr, this->m_clk_cycle,
                                      (RequestType)ready_msg.complementary_value, ready_msg.data);
                else
                {
                    cout << "ExternalCPU(id = " << this->m_id << "): ";
                    cout << "Error CPU Callback is invalid" << endl;
                    exit(0);
                }
            }
        }
    }

    void ExternalCPU::addRequests2ProcessingQueue(FRFCFS_Buffer<Message, ExternalCPU> &buf)
    {
        Message msg;

        while (m_upper_interface->peekMessage(&msg))
        {
            msg.source = Message::Source::UPPER_INTERCONNECT;
            msg.cycle = m_clk_cycle;
            if (buf.pushBack(msg))
                m_upper_interface->popFrontMessage();
            else
                break;
        }
    }

    FRFCFS_State ExternalCPU::getRequestState(const Message &msg, FRFCFS_State current_state)
    {
        return FRFCFS_State::Ready;
    }

    int ExternalCPU::portVersion()
    {
        return kOctopusPortVersion;
    }

    void ExternalCPU::registerCPUCallback(CpuCompletionCallback* cpu_callback)
    {
        this->m_cpu_callback = cpu_callback;
    }

    void ExternalCPU::registerCommitCallback(CpuCommitCallback* commit_callback)
    {
        m_commit_callback = commit_callback;
    }

    void ExternalCPU::registerInvalidateCallback(CpuInvalidateCallback* invalidate_callback)
    {
        m_invalidate_callback = invalidate_callback;
    }

    void ExternalCPU::registerInitializableMemory(Initializable *component)
    {
        m_memory_component = component;
    }

    void ExternalCPU::commit(uint64_t msg_id, uint64_t address)
    {
        if (m_commit_callback != NULL)
            (*m_commit_callback)(msg_id, address);
    }

    void ExternalCPU::invalidate(uint64_t address)
    {
        if (m_invalidate_callback != NULL)
            (*m_invalidate_callback)(address);
    }

    bool ExternalCPU::isBlocked(int outstanding) const
    {
        if (m_cache == NULL)
            return false;
        return m_cache->demandAdmissionBlocked(outstanding);
    }

    int ExternalCPU::outstandingLimit() const
    {
        return m_cache == NULL ? -1 : m_cache->demandQueueSize();
    }

    uint64_t ExternalCPU::addRequest(uint64_t address, RequestType type, uint8_t* data, int size)
    {
        uint64_t issued_id = 0;

        if(type == RequestType::SETUP_WRITE)
        {
            if(data == NULL)
            {
                cout << "ExternalCPU(id = " << this->m_id << "): ";
                cout << "Error addRequest without data" << endl;
                exit(0);
            }
            else if(m_memory_component == NULL)
            {
                cout << "ExternalCPU(id = " << this->m_id << "): ";
                cout << "Error no registered setup function" << endl;
                exit(0);
            }

            m_memory_component->initialize(address, data, size);
        }
        else if(type == RequestType::SETUP_READ)
        {
            m_memory_component->read(address, data);
        }
        else
        {
            Message request_msg(IdGenerator::nextReqId(), // id
                                address,                  // Addr
                                0,                        // Cycle
                                (uint64_t)type,           // Complementary_value
                                this->m_id);              // Owner
            request_msg.source = Message::Source::LOWER_INTERCONNECT;
            request_msg.data_size = size;
            if(type == RequestType::WRITE && data != NULL)
                request_msg.copy(data, size);

            m_processing_queue->pushBack(request_msg, FRFCFS_State::NonReady);
            issued_id = request_msg.msg_id;
        }

        return issued_id;
    }

    std::map<int, ExternalCPU*> ExternalCPU::ext_CPUs;

    std::map<int, ExternalCPU*>* ExternalCPU::getExtCPUs()
    {
        return &ExternalCPU::ext_CPUs;
    }
}
