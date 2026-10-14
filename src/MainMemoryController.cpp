/*
 * File  :      MainMemoryController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On May 23, 2022
 */

#include "../header/MainMemoryController.h"
#include "../header/Logger.h"
#include "../header/MemHazard.h"

namespace octopus
{
    // private controller constructor
    MainMemoryController::MainMemoryController(ParametersMap map, CommunicationInterface *lower_interface,
        string pname, string config_path, string name) : ClockedObj(0), Configurable(map, config_path, name, pname)
    {
        //Parameters initialization
        m_clk_period = std::get<int>(parameters.at(STRINGIFY(m_clk_period)).value);
        m_id = std::get<int>(parameters.at(STRINGIFY(m_id)).value);
        m_llc_id = std::get<int>(parameters.at(STRINGIFY(m_llc_id)).value);
        m_memory_latency = std::get<int>(parameters.at(STRINGIFY(m_memory_latency)).value);

        // Optional bound on the request buffer. Read from config when present,
        // otherwise fall back to a finite realistic default. A value of -1
        // restores the previous unbounded behavior.
        int processing_queue_size = DEFAULT_MEM_PROCESSING_QUEUE_SIZE;
        if (parameters.find(STRINGIFY(processing_queue_size)) != parameters.end())
            processing_queue_size = std::get<int>(parameters.at(STRINGIFY(processing_queue_size)).value);

        //Constructor
        m_clk_cycle = 1;

        m_read_count = 0;
        m_write_count = 0;

        m_lower_interface = lower_interface;

        dprint = new DebugPrint(getSubMap(STRINGIFY(dprint)), name + std::to_string(m_id), parent_name + "." + name);

        m_processing_queue = new FRFCFS_Buffer<Message, MainMemoryController>(&MainMemoryController::getRequestState, this, processing_queue_size);
    }

    MainMemoryController::~MainMemoryController()
    {
    }

    void MainMemoryController::cycleProcess()
    {
        processLogic();
        m_clk_cycle++;
    }

    void MainMemoryController::init()
    {
    }

    void MainMemoryController::flushOut()
    {
        // push only into a free response slot (a network interface is bounded; a bus reports room)
        while (!m_out.empty() && m_lower_interface->txFreeSlots(MessageType::DATA_RESPONSE) > 0 &&
               m_lower_interface->pushMessage(m_out.front(), m_clk_cycle, MessageType::DATA_RESPONSE))
            m_out.pop_front();
    }

    void MainMemoryController::processLogic()
    {
        flushOut();
        addRequests2ProcessingQueue(*m_processing_queue);

        Message ready_msg;
        if (m_processing_queue->getFirstReady(&ready_msg) == false)
            return;

        if (ready_msg.data == NULL) //Read message
        {
            m_read_count++;
            MemHazard::readCompleted(ready_msg.msg_id, ready_msg.addr, m_clk_cycle);
            // Design B: DRAM finished servicing this read -> data leaves DRAM.
            Logger::getLogger()->event(ready_msg.msg_id, Logger::Role::DRAM, (uint32_t)m_id, Logger::Phase::EXIT);
            Logger::getLogger()->trace(ready_msg, Logger::Role::DRAM, (uint32_t)m_id, Logger::Phase::EXIT);
            uint8_t return_data[64] = {0};

            Message msg = Message(ready_msg.msg_id,    // Id
                                  ready_msg.addr,      // Addr
                                  m_clk_cycle,         // Cycle
                                  0,                   // Complementary_value
                                  ready_msg.owner);    // Owner
            msg.to.push_back(ready_msg.from ? ready_msg.from : (uint16_t)m_llc_id);     // To: the bank that asked (m_llc_id when nothing stamped the sender)
            msg.kind = Message::K_FILL;
            msg.copy(return_data);
                    
            m_out.push_back(msg);
            flushOut();
        }
        else 
        {
            m_write_count++;
            MemHazard::writePerformed(ready_msg.addr);
            // cout << "MainMemoryController: write msg id = " << ready_msg.msg_id;
            // cout << ", count is " << m_write_count;
            // cout << " and clk is " << m_clk_cycle << endl;
        }
    }
    
    void MainMemoryController::addRequests2ProcessingQueue(FRFCFS_Buffer<Message, MainMemoryController> &buf)
    {
        Message msg;

        if (m_lower_interface->peekMessage(&msg))
        {
            msg.source = Message::Source::LOWER_INTERCONNECT;
            msg.cycle = m_clk_cycle;
            // a write is always accepted (its number is bounded by the LLCs' write-back buffers, which
            // hold every write until it is acknowledged); a read waits for room (spec s25.3)
            if (buf.pushBack(msg, FRFCFS_State::NonReady, /*force=*/msg.data != NULL))
            {
                m_lower_interface->popFrontMessage();
                if (msg.data == NULL) MemHazard::readArrived(msg.msg_id, msg.addr, m_clk_cycle);
                else                  MemHazard::writeArrived(msg.addr);
                if (msg.kind == Message::K_MEM_WRITE)
                {
                    // accepted: this queue is now the point of order for the line, so the LLC may
                    // release it (docs/NoC_Memory_Plan.md s2.3). Reads behind it in this queue are
                    // served in arrival order, after it.
                    Message ack(0, msg.addr, m_clk_cycle, 0, (uint16_t)m_id);
                    ack.to.push_back(msg.from ? msg.from : (uint16_t)m_llc_id);
                    ack.kind = Message::K_MEM_WACK;
                    m_out.push_back(ack);
                    flushOut();
                }
                // Design B: request has arrived at DRAM (crossed the mem bus in).
                Logger::getLogger()->event(msg.msg_id, Logger::Role::DRAM, (uint32_t)m_id, Logger::Phase::ENTER);
                Logger::getLogger()->trace(msg, Logger::Role::DRAM, (uint32_t)m_id, Logger::Phase::ENTER);
            }
        }
    }

    FRFCFS_State MainMemoryController::getRequestState(const Message &msg, FRFCFS_State current_state)
    {
        if(current_state == FRFCFS_State::NonReady)
        {
            if(msg.cycle + m_memory_latency <= m_clk_cycle)
                return FRFCFS_State::Ready;
        }

        return FRFCFS_State::NonReady;
    }    
}